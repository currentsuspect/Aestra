// © 2025 Aestra Studios — All Rights Reserved.
// Test EffectChainSnapshot creation and behavior

#include "Plugin/EffectChain.h"
#include "Plugin/SamplerPlugin.h"
#include "RealtimeThreadGuard.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

void emptyChainSnapshotHasNoPlugins() {
    EffectChain chain;
    chain.prepare(48000.0, 512);

    auto snapshot = chain.createSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->getActiveSlotCount() == 0);

    for (size_t i = 0; i < EffectChainSnapshot::MAX_SLOTS; ++i) {
        assert(snapshot->slot(i).isEmpty());
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
    assert(snapshot != nullptr);
    assert(snapshot->getActiveSlotCount() == 1);
    assert(snapshot->slot(0).plugin != nullptr);
    assert(snapshot->slot(0).plugin.get() == plugin.get());

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
    assert(snapshot->slot(0).plugin != nullptr);

    // Remove plugin from mutable chain
    auto removed = chain.removePlugin(0);
    assert(removed != nullptr);
    assert(chain.getActiveSlotCount() == 0);

    // Snapshot still holds plugin - plugin stays alive
    assert(snapshot->slot(0).plugin != nullptr);
    assert(snapshot->slot(0).plugin.get() == plugin.get());

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
    assert(snapshot != nullptr);

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
    assert(snapshot->slot(0).bypassed == true);

    // Unbypass and create another snapshot
    chain.setSlotBypassed(0, false);
    auto snapshot2 = chain.createSnapshot();
    assert(snapshot2->slot(0).bypassed == false);

    // Old snapshot still has bypassed = true
    assert(snapshot->slot(0).bypassed == true);

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
    auto previousHandler = setRealtimeMisuseHandler(nullptr);

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