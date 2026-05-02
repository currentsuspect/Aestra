#include "GarbageCollector.h"
#include "Plugin/SamplerPlugin.h"

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

struct TrackedObject {
    explicit TrackedObject(std::atomic<int>& counter) : destructorCounter(counter) {}
    ~TrackedObject() { destructorCounter.fetch_add(1, std::memory_order_relaxed); }

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

} // namespace

int main() {
    std::cout << "Starting GarbageCollector tests...\n";

    deferredDestructionWaitsForExternalReference();
    poppedQueueSlotsDoNotRetainSharedPtrReferences();
    queueFullFallbackRetainsAndEventuallyCollects();
    multipleNonRtProducersAreSerializedSafely();
    multipleCollectPassesAreStable();
    nullReleaseIsHarmless();
    samplerSampleReplacementAndShutdownDoNotCrash();

    std::cout << "GarbageCollector tests passed.\n";
    return 0;
}
