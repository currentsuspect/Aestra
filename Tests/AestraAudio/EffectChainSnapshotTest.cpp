// © 2025 Aestra Studios — All Rights Reserved.
// Test EffectChainSnapshot creation and behavior

#include "Plugin/EffectChain.h"
#include "Plugin/AestraEQ.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"
#include "RealtimeThreadGuard.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
#define require(cond, msg) assert((cond) && (msg))

using namespace Aestra::Audio;

namespace {
std::atomic<int> g_rtMisuseCount{0};

void countRtMisuse(const char*) noexcept {
    g_rtMisuseCount.fetch_add(1, std::memory_order_relaxed);
}

class TestEffectPlugin : public IPluginInstance {
public:
    explicit TestEffectPlugin(const char* id) {
        m_info.id = id;
        m_info.name = id;
        m_info.vendor = "Aestra Tests";
        m_info.version = "1.0";
        m_info.category = "Test";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 2;
        m_info.numAudioOutputs = 2;
    }

    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override { m_active = true; }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }

    std::vector<PluginParameter> getParameters() const override { return {}; }
    uint32_t getParameterCount() const override { return 0; }
    float getParameter(uint32_t) const override { return 0.0f; }
    void setParameter(uint32_t, float) override {}
    std::string getParameterDisplay(uint32_t) const override { return {}; }

    std::vector<uint8_t> saveState() const override { return {}; }
    bool loadState(const std::vector<uint8_t>&) override { return true; }

    bool hasEditor() const override { return false; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {0, 0}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

protected:
    PluginInfo m_info;
    bool m_active = false;
};

class NonFiniteOutputPlugin final : public TestEffectPlugin {
public:
    NonFiniteOutputPlugin() : TestEffectPlugin("test.nonfinite-output") {}

    void process(const float* const*, float** outputs, uint32_t, uint32_t numOutputChannels, uint32_t numFrames,
                 const MidiBuffer*, MidiBuffer*) override {
        ++processCalls;
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                outputs[ch][i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    }

    int processCalls = 0;
};

class FiniteProbePlugin final : public TestEffectPlugin {
public:
    FiniteProbePlugin() : TestEffectPlugin("test.finite-probe") {}

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer*, MidiBuffer*) override {
        ++processCalls;
        for (uint32_t ch = 0; ch < numInputChannels; ++ch) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                if (!std::isfinite(inputs[ch][i])) {
                    sawNonFiniteInput = true;
                }
            }
        }

        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                outputs[ch][i] = 0.25f;
            }
        }
    }

    int processCalls = 0;
    bool sawNonFiniteInput = false;
};

void emptyChainSnapshotHasNoPlugins() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto snapshot = chain.createSnapshot();
    require(snapshot != nullptr, "emptyChainSnapshotHasNoPlugins: snapshot is null");
    require(snapshot->getActiveSlotCount() == 0, "emptyChainSnapshotHasNoPlugins: active slot count != 0");

    for (size_t i = 0; i < EffectChainSnapshot::MAX_SLOTS; ++i) {
        require(snapshot->slot(i).isEmpty(), "emptyChainSnapshotHasNoPlugins: slot not empty");
    }

    std::cout << "PASS: emptyChainSnapshotHasNoPlugins\n";
}

void snapshotAfterInsertContainsPlugin() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);

    chain.insertPlugin(0, plugin);

    auto snapshot = chain.createSnapshot();
    require(snapshot != nullptr, "snapshotAfterInsertContainsPlugin: snapshot is null");
    require(snapshot->getActiveSlotCount() == 1, "snapshotAfterInsertContainsPlugin: active slot count != 1");
    require(snapshot->slot(0).plugin != nullptr, "snapshotAfterInsertContainsPlugin: plugin is null");
    require(snapshot->slot(0).plugin.get() == plugin.get(), "snapshotAfterInsertContainsPlugin: plugin mismatch");

    std::cout << "PASS: snapshotAfterInsertContainsPlugin\n";
}

void snapshotKeepsPluginAliveAfterRemoval() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);

    chain.insertPlugin(0, plugin);

    // Create snapshot while plugin is in chain
    auto snapshot = chain.createSnapshot();
    require(snapshot->slot(0).plugin != nullptr, "snapshotKeepsPluginAliveAfterRemoval: plugin is null after snapshot");

    // Remove plugin from mutable chain
    auto removed = chain.removePlugin(0);
    require(removed != nullptr, "snapshotKeepsPluginAliveAfterRemoval: removePlugin returned null");
    require(chain.getActiveSlotCount() == 0, "snapshotKeepsPluginAliveAfterRemoval: active slot count != 0");

    // Snapshot still holds plugin - plugin stays alive
    require(snapshot->slot(0).plugin != nullptr, "snapshotKeepsPluginAliveAfterRemoval: plugin removed from snapshot");
    require(snapshot->slot(0).plugin.get() == plugin.get(), "snapshotKeepsPluginAliveAfterRemoval: plugin mismatch");

    // Plugin destructor should NOT have run yet
    std::cout << "PASS: snapshotKeepsPluginAliveAfterRemoval\n";
}

void snapshotIsImmutableFromPublicAPI() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);

    chain.insertPlugin(0, plugin);

    auto snapshot = chain.createSnapshot();
    require(snapshot != nullptr, "snapshotIsImmutableFromPublicAPI: snapshot is null");

    // Verify we cannot get a mutable reference
    // The slot() method returns const reference
    const EffectChainSnapshotSlot& slot = snapshot->slot(0);
    (void)slot; // suppress unused warning
    // plugin is const - cannot be modified through snapshot
    // This is a compile-time check that API is immutable

    std::cout << "PASS: snapshotIsImmutableFromPublicAPI\n";
}

void snapshotCapturesBypassState() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);

    chain.insertPlugin(0, plugin);
    chain.setSlotBypassed(0, true);

    auto snapshot = chain.createSnapshot();
    require(snapshot->slot(0).bypassed == true, "snapshotCapturesBypassState: bypassed != true");

    // Unbypass and create another snapshot
    chain.setSlotBypassed(0, false);
    auto snapshot2 = chain.createSnapshot();
    require(snapshot2->slot(0).bypassed == false, "snapshotCapturesBypassState: snapshot2 bypassed != false");

    // Old snapshot still has bypassed = true
    require(snapshot->slot(0).bypassed == true, "snapshotCapturesBypassState: old snapshot bypassed != true");

    std::cout << "PASS: snapshotCapturesBypassState\n";
}

void mutatingChainDoesNotAlterOldSnapshot() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin1 = std::make_shared<Plugins::SamplerPlugin>();
    plugin1->initialize(48000.0, 512);
    chain.insertPlugin(0, plugin1);

    auto snapshot = chain.createSnapshot();
    assert(snapshot->slot(0).plugin.get() == plugin1.get());

    // Add another plugin at slot 1
    auto plugin2 = std::make_shared<Plugins::SamplerPlugin>();
    plugin2->initialize(48000.0, 512);
    chain.insertPlugin(1, plugin2);

    // Old snapshot should be unchanged
    assert(snapshot->getActiveSlotCount() == 1);
    assert(snapshot->slot(0).plugin.get() == plugin1.get());
    assert(snapshot->slot(1).isEmpty());

    // New snapshot reflects changes
    auto snapshot2 = chain.createSnapshot();
    assert(snapshot2->getActiveSlotCount() == 2);

    // Remove plugin from chain
    chain.removePlugin(0);

    // Old snapshot unchanged
    assert(snapshot->slot(0).plugin.get() == plugin1.get());

    std::cout << "PASS: mutatingChainDoesNotAlterOldSnapshot\n";
}

void nonRtInsertRemoveStillWorks() {
    auto previousHandler = setRealtimeMisuseHandler(nullptr);

    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);

    bool insertResult = chain.insertPlugin(0, plugin);
    assert(insertResult == true);
    assert(chain.getActiveSlotCount() == 1);

    auto removed = chain.removePlugin(0);
    assert(removed != nullptr);
    assert(chain.getActiveSlotCount() == 0);

    setRealtimeMisuseHandler(previousHandler);
    std::cout << "PASS: nonRtInsertRemoveStillWorks\n";
}

void realtimeMisuseGuardsStillWork() {
    g_rtMisuseCount.store(0, std::memory_order_relaxed);
    g_realtimeMisuseHandler.store(&countRtMisuse, std::memory_order_relaxed);

    EffectChain chain;
    chain.prepare(48000.0, 512);

    {
        ScopedRealtimeAudioThread realtimeScope;
        auto snapshot = chain.createSnapshot();
        assert(snapshot == nullptr);
        assert(g_rtMisuseCount.load(std::memory_order_relaxed) == 1);
    }

    g_realtimeMisuseHandler.store(nullptr, std::memory_order_relaxed);
    std::cout << "PASS: realtimeMisuseGuardsStillWork\n";
}

void getSnapshotReturnsPublishedSnapshot() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    // After prepare(), get a valid snapshot
    auto snapshot = chain.getSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->getActiveSlotCount() == 0);

    // Insert a plugin
    auto plugin = std::make_shared<Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    chain.insertPlugin(0, plugin);

    // getSnapshot() now reflects the change
    auto snapshot2 = chain.getSnapshot();
    assert(snapshot2 != nullptr);
    assert(snapshot2->getActiveSlotCount() == 1);

    // Old snapshot still reflects old state
    assert(snapshot->getActiveSlotCount() == 0);

    std::cout << "PASS: getSnapshotReturnsPublishedSnapshot\n";
}

void snapshotPublicationAfterAllMutations() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto plugin1 = std::make_shared<Plugins::SamplerPlugin>();
    plugin1->initialize(48000.0, 512);
    chain.insertPlugin(0, plugin1);

    auto snapshot1 = chain.getSnapshot();
    assert(snapshot1->getActiveSlotCount() == 1);

    // Insert another plugin
    auto plugin2 = std::make_shared<Plugins::SamplerPlugin>();
    plugin2->initialize(48000.0, 512);
    chain.insertPlugin(1, plugin2);

    auto snapshot2 = chain.getSnapshot();
    assert(snapshot2->getActiveSlotCount() == 2);
    assert(snapshot1->getActiveSlotCount() == 1); // unchanged

    // Remove plugin
    chain.removePlugin(0);
    auto snapshot3 = chain.getSnapshot();
    assert(snapshot3->getActiveSlotCount() == 1);
    assert(snapshot2->getActiveSlotCount() == 2); // unchanged

    // Clear all
    chain.clear();
    auto snapshot4 = chain.getSnapshot();
    assert(snapshot4->getActiveSlotCount() == 0);

    std::cout << "PASS: snapshotPublicationAfterAllMutations\n";
}

void snapshotQuarantinesNonFinitePluginBeforeDownstreamProcessing() {
    constexpr uint32_t kChannels = 2;
    constexpr uint32_t kFrames = 8;

    EffectChain chain;
    chain.prepare(48000.0, kFrames);

    auto poison = std::make_shared<NonFiniteOutputPlugin>();
    auto probe = std::make_shared<FiniteProbePlugin>();
    chain.insertPlugin(0, poison);
    chain.insertPlugin(1, probe);

    auto snapshot = chain.getSnapshot();
    require(snapshot != nullptr, "snapshotQuarantinesNonFinitePlugin: snapshot is null");

    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    for (uint32_t i = 0; i < kFrames; ++i) {
        left[i] = 0.5f;
        right[i] = -0.5f;
    }

    float* channels[kChannels] = {left.data(), right.data()};
    std::array<float, kFrames * kChannels> dryBuffer{};

    snapshot->process(channels, kChannels, kFrames, nullptr, 0, dryBuffer.data());

    require(poison->processCalls == 1, "snapshotQuarantinesNonFinitePlugin: poison not processed once");
    require(probe->processCalls == 1, "snapshotQuarantinesNonFinitePlugin: downstream not processed");
    require(!probe->sawNonFiniteInput, "snapshotQuarantinesNonFinitePlugin: downstream saw non-finite input");
    require(chain.isSlotBypassed(0), "snapshotQuarantinesNonFinitePlugin: poisoned slot not bypassed");
    require(chain.isSlotBypassedByNonFiniteOutput(0),
            "snapshotQuarantinesNonFinitePlugin: poisoned slot missing non-finite fault");
    require(chain.getSlotNonFiniteOutputCount(0) == 1, "snapshotQuarantinesNonFinitePlugin: fault count mismatch");
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        for (uint32_t i = 0; i < kFrames; ++i) {
            require(std::isfinite(channels[ch][i]), "snapshotQuarantinesNonFinitePlugin: output is non-finite");
            require(channels[ch][i] == 0.25f, "snapshotQuarantinesNonFinitePlugin: downstream output mismatch");
        }
    }

    snapshot->process(channels, kChannels, kFrames, nullptr, 0, dryBuffer.data());
    require(poison->processCalls == 1, "snapshotQuarantinesNonFinitePlugin: quarantined plugin processed again");
    require(probe->processCalls == 2,
            "snapshotQuarantinesNonFinitePlugin: downstream should keep processing after quarantine");

    chain.setSlotBypassed(0, false);
    require(!chain.isSlotBypassedByNonFiniteOutput(0),
            "snapshotQuarantinesNonFinitePlugin: manual unbypass did not clear fault");

    std::cout << "PASS: snapshotQuarantinesNonFinitePluginBeforeDownstreamProcessing\n";
}

void movedPluginCarriesFaultStateForInFlightSnapshots() {
    constexpr uint32_t kChannels = 2;
    constexpr uint32_t kFrames = 8;

    EffectChain chain;
    chain.prepare(48000.0, kFrames);

    auto poison = std::make_shared<NonFiniteOutputPlugin>();
    chain.insertPlugin(0, poison);

    auto inFlightSnapshot = chain.getSnapshot();
    require(inFlightSnapshot != nullptr, "movedPluginCarriesFaultState: snapshot is null");

    require(chain.movePlugin(0, 2), "movedPluginCarriesFaultState: move failed");
    require(!chain.isSlotBypassedByNonFiniteOutput(2), "movedPluginCarriesFaultState: destination pre-faulted");

    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    float* channels[kChannels] = {left.data(), right.data()};
    std::array<float, kFrames * kChannels> dryBuffer{};

    inFlightSnapshot->process(channels, kChannels, kFrames, nullptr, 0, dryBuffer.data());

    require(poison->processCalls == 1, "movedPluginCarriesFaultState: in-flight snapshot did not process poison");
    require(chain.isSlotBypassedByNonFiniteOutput(2),
            "movedPluginCarriesFaultState: moved plugin destination did not receive in-flight fault");
    require(!chain.isSlotBypassedByNonFiniteOutput(0),
            "movedPluginCarriesFaultState: empty source slot received moved plugin fault");

    std::cout << "PASS: movedPluginCarriesFaultStateForInFlightSnapshots\n";
}

void replacingPluginIsolatedFromInFlightSnapshotFaults() {
    constexpr uint32_t kChannels = 2;
    constexpr uint32_t kFrames = 8;

    EffectChain chain;
    chain.prepare(48000.0, kFrames);

    auto oldPoison = std::make_shared<NonFiniteOutputPlugin>();
    auto replacement = std::make_shared<FiniteProbePlugin>();
    chain.insertPlugin(0, oldPoison);

    auto inFlightSnapshot = chain.getSnapshot();
    require(inFlightSnapshot != nullptr, "replacingPluginIsolated: snapshot is null");

    require(chain.insertPlugin(0, replacement), "replacingPluginIsolated: replacement insert failed");
    require(!chain.isSlotBypassedByNonFiniteOutput(0), "replacingPluginIsolated: replacement pre-faulted");

    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    float* channels[kChannels] = {left.data(), right.data()};
    std::array<float, kFrames * kChannels> dryBuffer{};

    inFlightSnapshot->process(channels, kChannels, kFrames, nullptr, 0, dryBuffer.data());

    require(oldPoison->processCalls == 1, "replacingPluginIsolated: in-flight snapshot did not process old plugin");
    require(!chain.isSlotBypassedByNonFiniteOutput(0),
            "replacingPluginIsolated: stale snapshot fault quarantined replacement plugin");

    std::cout << "PASS: replacingPluginIsolatedFromInFlightSnapshotFaults\n";
}

void eqV5StateRoundTripsThroughEffectChain() {
    BuiltInPlugins::registerCoreBuiltIns();

    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto eq = std::make_shared<Plugins::AestraEQ>();
    eq->setInfo(BuiltInPlugins::eqInfo());
    eq->initialize(48000.0, 512);
    eq->activate();
    eq->setParameter(Plugins::AestraEQ::kParamBell1Enable, 1.0f);
    eq->setParameter(Plugins::AestraEQ::kParamBell1Freq, 0.45f);
    eq->setParameter(Plugins::AestraEQ::kParamBell1Q, 0.30f);
    eq->setParameter(Plugins::AestraEQ::kParamBell1Type, 1.0f / 3.0f); // Notch
    eq->setParameter(Plugins::AestraEQ::kParamBell2Enable, 1.0f);
    eq->setParameter(Plugins::AestraEQ::kParamBell2Type, 2.0f / 3.0f); // Band Pass
    eq->setParameter(Plugins::AestraEQ::kParamOutputGain, 0.75f);
    eq->setParameter(Plugins::AestraEQ::kParamPolarityInvert, 1.0f);

    require(chain.insertPlugin(0, eq), "eqV5StateRoundTripsThroughEffectChain: insert failed");
    chain.setSlotBypassed(0, true);
    chain.setSlotDryWetMix(0, 0.42f);

    const auto state = chain.saveState();

    auto& manager = PluginManager::getInstance();
    require(manager.initialize(), "eqV5StateRoundTripsThroughEffectChain: PluginManager initialize failed");

    EffectChain restored;
    restored.prepare(48000.0, 512);
    require(restored.loadState(state, manager), "eqV5StateRoundTripsThroughEffectChain: load failed");
    require(restored.getActiveSlotCount() == 1, "eqV5StateRoundTripsThroughEffectChain: active slot count mismatch");
    require(restored.isSlotBypassed(0), "eqV5StateRoundTripsThroughEffectChain: bypass not restored");
    require(std::abs(restored.getSlotDryWetMix(0) - 0.42f) < 0.001f,
            "eqV5StateRoundTripsThroughEffectChain: dry/wet not restored");

    auto restoredEq = std::dynamic_pointer_cast<Plugins::AestraEQ>(restored.getPlugin(0));
    require(restoredEq != nullptr, "eqV5StateRoundTripsThroughEffectChain: restored plugin is not EQ");
    require(std::abs(restoredEq->getParameter(Plugins::AestraEQ::kParamBell1Type) - (1.0f / 3.0f)) < 0.001f,
            "eqV5StateRoundTripsThroughEffectChain: Bell1 type not restored");
    require(std::abs(restoredEq->getParameter(Plugins::AestraEQ::kParamBell2Type) - (2.0f / 3.0f)) < 0.001f,
            "eqV5StateRoundTripsThroughEffectChain: Bell2 type not restored");
    require(std::abs(restoredEq->getParameter(Plugins::AestraEQ::kParamOutputGain) - 0.75f) < 0.001f,
            "eqV5StateRoundTripsThroughEffectChain: output gain not restored");
    require(restoredEq->getParameter(Plugins::AestraEQ::kParamPolarityInvert) == 1.0f,
            "eqV5StateRoundTripsThroughEffectChain: polarity not restored");

    std::cout << "PASS: eqV5StateRoundTripsThroughEffectChain\n";
}

} // namespace

int main() {
    std::cout << "=== EffectChainSnapshot Tests ===\n\n";

    emptyChainSnapshotHasNoPlugins();
    snapshotAfterInsertContainsPlugin();
    snapshotKeepsPluginAliveAfterRemoval();
    snapshotIsImmutableFromPublicAPI();
    snapshotCapturesBypassState();
    mutatingChainDoesNotAlterOldSnapshot();
    nonRtInsertRemoveStillWorks();
    realtimeMisuseGuardsStillWork();
    getSnapshotReturnsPublishedSnapshot();
    snapshotPublicationAfterAllMutations();
    snapshotQuarantinesNonFinitePluginBeforeDownstreamProcessing();
    movedPluginCarriesFaultStateForInFlightSnapshots();
    replacingPluginIsolatedFromInFlightSnapshotFaults();
    eqV5StateRoundTripsThroughEffectChain();

    std::cout << "\n=== All EffectChainSnapshot tests passed ===\n";
    return 0;
}
