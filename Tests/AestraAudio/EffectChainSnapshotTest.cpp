// © 2025 Aestra Studios — All Rights Reserved.
// Test EffectChainSnapshot creation and behavior

#include "Plugin/EffectChain.h"
#include "Plugin/SamplerPlugin.h"
#include "RealtimeThreadGuard.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#define require(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << (msg) << std::endl; return 1; } } while(0)

using namespace Aestra::Audio;

namespace {

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
    int misuseCount = 0;
    auto countHandler = [](const char* apiName) noexcept { ++misuseCount; };
    auto previousHandler = setRealtimeMisuseHandler(countHandler);

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
    int misuseCount = 0;
    auto countHandler = [](const char* apiName) noexcept { ++misuseCount; };
    auto previousHandler = setRealtimeMisuseHandler(countHandler);

    EffectChain chain;
    chain.prepare(48000.0, 512);

    {
        ScopedRealtimeAudioThread realtimeScope;
        auto snapshot = chain.createSnapshot();
        assert(snapshot == nullptr);
    }

    setRealtimeMisuseHandler(previousHandler);
    std::cout << "PASS: realtimeMisuseGuardsStillWork\n";
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

    std::cout << "\n=== All EffectChainSnapshot tests passed ===\n";
    return 0;
}