#include "GarbageCollector.h"
#include "Core/AudioEngine.h"
#include "Plugin/SamplerPlugin.h"
#include "Plugin/EffectChain.h"
#include "RealtimeThreadGuard.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

std::atomic<int> g_realtimeMisuseCount{0};

void countRealtimeMisuse(const char*) noexcept {
    g_realtimeMisuseCount.fetch_add(1, std::memory_order_relaxed);
}

struct TrackedObject {
    explicit TrackedObject(std::atomic<int>& counter) : destructorCounter(counter) {}
    ~TrackedObject() { destructorCounter.fetch_add(1, std::memory_order_relaxed); }

    std::atomic<int>& destructorCounter;
};

struct TrackedMeterSnapshotBuffer : MeterSnapshotBuffer {
    explicit TrackedMeterSnapshotBuffer(std::atomic<int>& counter) : destructorCounter(counter) {}
    ~TrackedMeterSnapshotBuffer() { destructorCounter.fetch_add(1, std::memory_order_relaxed); }

    std::atomic<int>& destructorCounter;
};

struct TrackedContinuousParamBuffer : ContinuousParamBuffer {
    explicit TrackedContinuousParamBuffer(std::atomic<int>& counter) : destructorCounter(counter) {}
    ~TrackedContinuousParamBuffer() { destructorCounter.fetch_add(1, std::memory_order_relaxed); }

    std::atomic<int>& destructorCounter;
};

struct TrackedChannelSlotMap : ChannelSlotMap {
    explicit TrackedChannelSlotMap(std::atomic<int>& counter) : destructorCounter(counter) {}
    ~TrackedChannelSlotMap() { destructorCounter.fetch_add(1, std::memory_order_relaxed); }

    std::atomic<int>& destructorCounter;
};

void writeUint32(std::ofstream& out, uint32_t value) {
    out.put(static_cast<char>(value & 0xFF));
    out.put(static_cast<char>((value >> 8) & 0xFF));
    out.put(static_cast<char>((value >> 16) & 0xFF));
    out.put(static_cast<char>((value >> 24) & 0xFF));
}

void writeUint16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>(value & 0xFF));
    out.put(static_cast<char>((value >> 8) & 0xFF));
}

void writeTestWav(const std::filesystem::path& path, const std::vector<int16_t>& samples) {
    std::ofstream wav(path, std::ios::binary);
    const uint16_t audioFormat = 1;
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t sampleRate = 44100;
    const uint32_t bytesPerSample = bitsPerSample / 8;
    const uint32_t dataChunkSize = static_cast<uint32_t>(samples.size() * bytesPerSample);
    const uint32_t fmtChunkSize = 16;
    const uint32_t riffChunkSize = 4 + (8 + fmtChunkSize) + (8 + dataChunkSize);

    wav.write("RIFF", 4);
    writeUint32(wav, riffChunkSize);
    wav.write("WAVE", 4);
    wav.write("fmt ", 4);
    writeUint32(wav, fmtChunkSize);
    writeUint16(wav, audioFormat);
    writeUint16(wav, channels);
    writeUint32(wav, sampleRate);
    writeUint32(wav, sampleRate * channels * bytesPerSample);
    writeUint16(wav, channels * bytesPerSample);
    writeUint16(wav, bitsPerSample);
    wav.write("data", 4);
    writeUint32(wav, dataChunkSize);
    for (int16_t sample : samples) {
        writeUint16(wav, static_cast<uint16_t>(sample));
    }
}

void deferredDestructionWaitsForExternalReference() {
    BasicGarbageCollector<4> gc;
    std::atomic<int> destroyed{0};

    auto externalRef = std::make_shared<TrackedObject>(destroyed);
    gc.release(externalRef, "test.externalRef");
    gc.collect();

    assert(destroyed.load(std::memory_order_relaxed) == 0);
    assert(gc.zombieCount() == 1);

    externalRef.reset();
    gc.collect();

    assert(destroyed.load(std::memory_order_relaxed) == 1);
    assert(gc.zombieCount() == 0);
    assert(gc.stats().totalCollected == 1);
}

void poppedQueueSlotsDoNotRetainSharedPtrReferences() {
    BasicGarbageCollector<4> gc;
    std::atomic<int> destroyed{0};

    auto audioSideRef = std::make_shared<TrackedObject>(destroyed);
    gc.release(audioSideRef);
    gc.collect();
    assert(destroyed.load(std::memory_order_relaxed) == 0);

    audioSideRef.reset();
    gc.collect();

    assert(destroyed.load(std::memory_order_relaxed) == 1);
    assert(gc.zombieCount() == 0);
}

void queueFullFallbackRetainsAndEventuallyCollects() {
    BasicGarbageCollector<2> gc; // usable queue capacity is one entry.
    std::atomic<int> destroyed{0};

    gc.release(std::make_shared<TrackedObject>(destroyed), "queued");
    gc.release(std::make_shared<TrackedObject>(destroyed), "overflow");

    assert(destroyed.load(std::memory_order_relaxed) == 0);
    assert(gc.zombieCount() == 2);

    const auto beforeCollect = gc.stats();
    assert(beforeCollect.incomingQueueFullCount == 1);
    assert(beforeCollect.overflowCount == 1);

    gc.collect();

    assert(destroyed.load(std::memory_order_relaxed) == 2);
    assert(gc.zombieCount() == 0);

    const auto afterCollect = gc.stats();
    assert(afterCollect.totalCollected == 2);
    assert(afterCollect.totalOverflowDrained == 1);
    assert(afterCollect.overflowCount == 0);
}

void statsRemainSaneAfterReleaseCollectAndDrain() {
    BasicGarbageCollector<4> gc;
    std::atomic<int> destroyed{0};

    gc.release(std::make_shared<TrackedObject>(destroyed), "stats.labeled");
    gc.release(std::make_shared<TrackedObject>(destroyed));

    const auto afterRelease = gc.stats();
    assert(afterRelease.totalReleased == 2);
    assert(afterRelease.totalCollected == 0);
    assert(afterRelease.currentlyTracked == 2);
    assert(afterRelease.maxZombieCount >= 2);

    const size_t passes = gc.drainUntilStable(8);
    assert(passes >= 1);
    assert(destroyed.load(std::memory_order_relaxed) == 2);

    const auto afterDrain = gc.stats();
    assert(afterDrain.totalReleased == 2);
    assert(afterDrain.totalCollected == 2);
    assert(afterDrain.currentlyTracked == 0);
    assert(afterDrain.incomingQueueFullCount == 0);
}

void realtimeReleaseMisuseIsReported() {
    BasicGarbageCollector<4> gc;
    std::atomic<int> destroyed{0};
    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);
    auto previousHandler = setRealtimeMisuseHandler(countRealtimeMisuse);

    // Snapshot state before RT call
    auto preStats = gc.stats();
    size_t preZombies = gc.zombieCount();

    {
        ScopedRealtimeAudioThread realtimeScope;
        gc.release(std::make_shared<TrackedObject>(destroyed), "rt.release");
    }

    setRealtimeMisuseHandler(previousHandler);
    assert(g_realtimeMisuseCount.load(std::memory_order_relaxed) == 1);

    // Verify GC state unchanged - release() refused to mutate
    auto postStats = gc.stats();
    size_t postZombies = gc.zombieCount();
    assert(postStats.totalReleased == preStats.totalReleased);
    assert(postStats.currentlyTracked == preStats.currentlyTracked);
    assert(postZombies == preZombies);

    // Now do proper non-RT release and verify it works
    gc.release(std::make_shared<TrackedObject>(destroyed), "non-rt");
    gc.collect();
    assert(destroyed.load(std::memory_order_relaxed) == 1);
}

void realtimeCollectMisuseIsReported() {
    BasicGarbageCollector<4> gc;
    std::atomic<int> destroyed{0};
    auto externalRef = std::make_shared<TrackedObject>(destroyed);
    gc.release(externalRef, "rt.collect");

    // Snapshot state before RT call
    auto preStats = gc.stats();
    size_t preZombies = gc.zombieCount();

    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);
    auto previousHandler = setRealtimeMisuseHandler(countRealtimeMisuse);

    {
        ScopedRealtimeAudioThread realtimeScope;
        gc.collect();
    }

    setRealtimeMisuseHandler(previousHandler);
    assert(g_realtimeMisuseCount.load(std::memory_order_relaxed) == 1);
    assert(destroyed.load(std::memory_order_relaxed) == 0);

    // Verify GC state unchanged - collect() refused to mutate
    auto postStats = gc.stats();
    size_t postZombies = gc.zombieCount();
    assert(postStats.totalCollected == preStats.totalCollected);
    assert(postStats.currentlyTracked == preStats.currentlyTracked);
    assert(postZombies == preZombies);

    // Now do proper non-RT collect and verify it works
    externalRef.reset();
    gc.collect();
    assert(destroyed.load(std::memory_order_relaxed) == 1);
}

void multipleNonRtProducersAreSerializedSafely() {
    BasicGarbageCollector<8> gc;
    std::atomic<int> destroyed{0};

    auto producer = [&gc, &destroyed]() {
        for (int i = 0; i < 64; ++i) {
            gc.release(std::make_shared<TrackedObject>(destroyed), "producer");
        }
    };

    std::thread first(producer);
    std::thread second(producer);
    first.join();
    second.join();

    assert(gc.zombieCount() == 128);
    gc.drainUntilStable(8);
    assert(destroyed.load(std::memory_order_relaxed) == 128);
    assert(gc.zombieCount() == 0);
}

void multipleCollectPassesAreStable() {
    BasicGarbageCollector<4> gc;
    std::atomic<int> destroyed{0};

    gc.release(std::make_shared<TrackedObject>(destroyed));
    const size_t passes = gc.drainUntilStable(4);
    assert(passes >= 1);
    assert(destroyed.load(std::memory_order_relaxed) == 1);

    gc.collect();
    gc.collect();
    assert(destroyed.load(std::memory_order_relaxed) == 1);
    assert(gc.zombieCount() == 0);
}

void nullReleaseIsHarmless() {
    BasicGarbageCollector<4> gc;
    std::shared_ptr<TrackedObject> empty;

    gc.release(empty);
    gc.collect();

    const auto stats = gc.stats();
    assert(stats.totalReleased == 0);
    assert(stats.totalCollected == 0);
    assert(stats.currentlyTracked == 0);
}

void samplerSampleReplacementAndShutdownDoNotCrash() {
    namespace fs = std::filesystem;

    const fs::path firstPath = fs::temp_directory_path() / "Aestra_gc_sampler_first.wav";
    const fs::path secondPath = fs::temp_directory_path() / "Aestra_gc_sampler_second.wav";
    writeTestWav(firstPath, {0, 8192, -8192, 0});
    writeTestWav(secondPath, {0, 16384, -16384, 0});

    Aestra::Audio::Plugins::SamplerPlugin sampler;
    assert(sampler.initialize(44100.0, 64));
    assert(sampler.loadSample(firstPath.string()));
    assert(sampler.loadSample(secondPath.string()));
    assert(sampler.normalizeSample());
    assert(sampler.reverseSample());
    sampler.shutdown();

    GarbageCollector::instance().drainUntilStable(8);

    std::error_code ec;
    fs::remove(firstPath, ec);
    ec.clear();
    fs::remove(secondPath, ec);
}

void productionMaintenanceCadenceCollectsDeferredResources() {
    GarbageCollector::instance().drainUntilStable(8);
    AudioEngine engine;
    std::atomic<int> destroyed{0};

    auto externalRef = std::make_shared<TrackedObject>(destroyed);
    GarbageCollector::instance().release(externalRef, "engine.maintenance");

    engine.performNonRealtimeMaintenance();

    const auto afterFirstMaintenance = GarbageCollector::instance().stats();
    assert(afterFirstMaintenance.currentlyTracked == 1);
    assert(destroyed.load(std::memory_order_relaxed) == 0);

    externalRef.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    engine.performNonRealtimeMaintenance();

    assert(destroyed.load(std::memory_order_relaxed) == 1);
    assert(GarbageCollector::instance().stats().currentlyTracked == 0);
}

void shutdownDrainCollectsDeferredResources() {
    GarbageCollector::instance().drainUntilStable(8);
    AudioEngine engine;
    std::atomic<int> destroyed{0};

    auto externalRef = std::make_shared<TrackedObject>(destroyed);
    GarbageCollector::instance().release(externalRef, "engine.shutdown");
    engine.performNonRealtimeMaintenance();

    externalRef.reset();
    engine.drainDeferredResourcesForShutdown();

    assert(destroyed.load(std::memory_order_relaxed) == 1);
    assert(GarbageCollector::instance().stats().currentlyTracked == 0);
}

void maintenanceMethodsRejectRealtimeContext() {
    GarbageCollector::instance().drainUntilStable(8);
    AudioEngine engine;
    std::atomic<int> destroyed{0};

    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);
    auto previousHandler = setRealtimeMisuseHandler(countRealtimeMisuse);

    {
        ScopedRealtimeAudioThread realtimeScope;
        engine.performNonRealtimeMaintenance();
        engine.drainDeferredResourcesForShutdown();
    }

    setRealtimeMisuseHandler(previousHandler);
    assert(g_realtimeMisuseCount.load(std::memory_order_relaxed) == 2);
    assert(destroyed.load(std::memory_order_relaxed) == 0);
}

void audioEngineSnapshotSettersRetireOldResourcesThroughGc() {
    GarbageCollector::instance().drainUntilStable(8);

    AudioEngine engine;

    std::atomic<int> meterDestroyed{0};
    auto meterRetired = std::make_shared<TrackedMeterSnapshotBuffer>(meterDestroyed);
    auto meterReplacement = std::make_shared<MeterSnapshotBuffer>();
    engine.setMeterSnapshots(meterRetired);
    engine.setMeterSnapshots(meterReplacement);
    GarbageCollector::instance().collect();
    assert(meterDestroyed.load(std::memory_order_relaxed) == 0);
    meterRetired.reset();
    GarbageCollector::instance().collect();
    assert(meterDestroyed.load(std::memory_order_relaxed) == 1);

    std::atomic<int> paramsDestroyed{0};
    auto paramsRetired = std::make_shared<TrackedContinuousParamBuffer>(paramsDestroyed);
    auto paramsReplacement = std::make_shared<ContinuousParamBuffer>();
    engine.setContinuousParams(paramsRetired);
    engine.setContinuousParams(paramsReplacement);
    GarbageCollector::instance().collect();
    assert(paramsDestroyed.load(std::memory_order_relaxed) == 0);
    paramsRetired.reset();
    GarbageCollector::instance().collect();
    assert(paramsDestroyed.load(std::memory_order_relaxed) == 1);

    std::atomic<int> slotMapDestroyed{0};
    auto slotMapRetired = std::make_shared<TrackedChannelSlotMap>(slotMapDestroyed);
    auto slotMapReplacement = std::make_shared<ChannelSlotMap>();
    engine.setChannelSlotMap(slotMapRetired);
    engine.setChannelSlotMap(slotMapReplacement);
    GarbageCollector::instance().collect();
    assert(slotMapDestroyed.load(std::memory_order_relaxed) == 0);
    slotMapRetired.reset();
    GarbageCollector::instance().collect();
    assert(slotMapDestroyed.load(std::memory_order_relaxed) == 1);
}

void audioEngineSnapshotSettersRejectRealtimeContext() {
    GarbageCollector::instance().drainUntilStable(8);

    AudioEngine engine;
    auto previousHandler = setRealtimeMisuseHandler(countRealtimeMisuse);
    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);

    {
        ScopedRealtimeAudioThread realtimeScope;
        engine.setMeterSnapshots(std::make_shared<MeterSnapshotBuffer>());
        engine.setContinuousParams(std::make_shared<ContinuousParamBuffer>());
        engine.setChannelSlotMap(std::make_shared<ChannelSlotMap>());
    }

    setRealtimeMisuseHandler(previousHandler);
    assert(g_realtimeMisuseCount.load(std::memory_order_relaxed) == 3);
}

void effectChainInsertPluginRejectsRealtimeContext() {
    auto previousHandler = setRealtimeMisuseHandler(countRealtimeMisuse);
    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);

    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Aestra::Audio::Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);

    {
        ScopedRealtimeAudioThread realtimeScope;
        bool result = chain.insertPlugin(0, plugin);
        assert(result == false);
    }

    setRealtimeMisuseHandler(previousHandler);
    assert(g_realtimeMisuseCount.load(std::memory_order_relaxed) == 1);
}

void effectChainRemovePluginRejectsRealtimeContext() {
    auto previousHandler = setRealtimeMisuseHandler(countRealtimeMisuse);
    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);

    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Aestra::Audio::Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    plugin->activate();
    chain.insertPlugin(0, plugin);

    {
        ScopedRealtimeAudioThread realtimeScope;
        auto removed = chain.removePlugin(0);
        assert(removed == nullptr);
    }

    setRealtimeMisuseHandler(previousHandler);
    assert(g_realtimeMisuseCount.load(std::memory_order_relaxed) == 1);
}

void effectChainClearRejectsRealtimeContext() {
    auto previousHandler = setRealtimeMisuseHandler(countRealtimeMisuse);
    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);

    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Aestra::Audio::Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    plugin->activate();
    chain.insertPlugin(0, plugin);

    {
        ScopedRealtimeAudioThread realtimeScope;
        chain.clear();
    }

    setRealtimeMisuseHandler(previousHandler);
    assert(g_realtimeMisuseCount.load(std::memory_order_relaxed) == 1);
}

void effectChainNonRtInsertRemoveStillWorks() {
    auto previousHandler = setRealtimeMisuseHandler(nullptr);
    g_realtimeMisuseCount.store(0, std::memory_order_relaxed);

    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Aestra::Audio::Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);

    bool insertResult = chain.insertPlugin(0, plugin);
    assert(insertResult == true);
    assert(chain.getActiveSlotCount() == 1);

    auto removed = chain.removePlugin(0);
    assert(removed != nullptr);
    assert(chain.getActiveSlotCount() == 0);

    setRealtimeMisuseHandler(previousHandler);
}

} // namespace

int main() {
    std::cout << "Starting GarbageCollector tests...\n";

    deferredDestructionWaitsForExternalReference();
    poppedQueueSlotsDoNotRetainSharedPtrReferences();
    queueFullFallbackRetainsAndEventuallyCollects();
    statsRemainSaneAfterReleaseCollectAndDrain();
    realtimeReleaseMisuseIsReported();
    realtimeCollectMisuseIsReported();
    multipleNonRtProducersAreSerializedSafely();
    productionMaintenanceCadenceCollectsDeferredResources();
    shutdownDrainCollectsDeferredResources();
    maintenanceMethodsRejectRealtimeContext();
    audioEngineSnapshotSettersRetireOldResourcesThroughGc();
    audioEngineSnapshotSettersRejectRealtimeContext();
    effectChainInsertPluginRejectsRealtimeContext();
    effectChainRemovePluginRejectsRealtimeContext();
    effectChainClearRejectsRealtimeContext();
    effectChainNonRtInsertRemoveStillWorks();
    multipleCollectPassesAreStable();
    nullReleaseIsHarmless();
    samplerSampleReplacementAndShutdownDoNotCrash();

    std::cout << "GarbageCollector tests passed.\n";
    return 0;
}
