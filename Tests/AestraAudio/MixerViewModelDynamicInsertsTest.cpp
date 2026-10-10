// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// V8-S3 seam test: the mixer's insert list follows the chain's real slot count.
// An ordinary channel still shows ten inserts (nothing changes for existing
// projects); a channel whose chain has grown past ten shows as many as it holds,
// including the master. No pixels: real TrackManager + MixerViewModel sync.

#include "MixerViewModel.h"

#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

using namespace Aestra;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    std::cout << "  " << (condition ? "PASS " : "FAIL ") << what << "\n";
    if (!condition) {
        ++g_failures;
    }
}

Audio::PluginInstancePtr makeSampler() {
    auto plugin = std::make_shared<Audio::Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    return plugin;
}

} // namespace

int main() {
    Audio::TrackManager tm;
    Audio::MixerChannel* plain = tm.addChannel("plain");
    Audio::MixerChannel* grown = tm.addChannel("grown");
    if (!plain || !grown || !tm.getMasterChannel()) {
        std::cerr << "[FAIL] setup\n";
        return 1;
    }
    const uint32_t plainId = plain->getChannelId();
    const uint32_t grownId = grown->getChannelId();

    grown->getEffectChain().insertPlugin(11, makeSampler());
    tm.getMasterChannel()->getEffectChain().insertPlugin(12, makeSampler());

    auto slotMap = tm.getChannelSlotMapShared();
    if (!slotMap) {
        std::cerr << "[FAIL] no slot map\n";
        return 1;
    }

    Aestra::MixerViewModel vm;
    vm.syncFromEngine(tm, *slotMap);

    const auto* vmPlain = vm.getChannelById(plainId);
    const auto* vmGrown = vm.getChannelById(grownId);
    const auto* vmMaster = vm.getMaster();
    if (!vmPlain || !vmGrown || !vmMaster) {
        std::cerr << "[FAIL] view models missing\n";
        return 1;
    }

    expect(vmPlain->inserts.size() == Audio::EffectChain::kInitialSlots,
           "a channel that never grew still shows the ten inserts it always did");
    expect(vmGrown->inserts.size() == 12, "a channel with a plugin at slot 11 shows twelve inserts");
    expect(!vmGrown->inserts[11].isEmpty, "the plugin past the tenth appears in the list");
    expect(vmGrown->inserts[10].isEmpty, "the gap before it is empty");
    expect(vmMaster->inserts.size() == 13, "the master chain follows its own size too");
    expect(!vmMaster->inserts[12].isEmpty, "the master's plugin past the tenth appears");

    // The chain only ever shrinks through a project load; the view follows that too.
    std::vector<uint8_t> plainState = plain->getEffectChain().saveState();
    auto& manager = Audio::PluginManager::getInstance();
    manager.initialize();
    expect(grown->getEffectChain().loadState(plainState, manager), "a ten-slot state loads over the grown chain");
    vm.syncFromEngine(tm, *slotMap);
    vmGrown = vm.getChannelById(grownId);
    expect(vmGrown && vmGrown->inserts.size() == Audio::EffectChain::kInitialSlots,
           "the insert list shrinks back with the chain");

    std::cout << (g_failures == 0 ? "[PASS] MixerViewModelDynamicInsertsTest\n" : "[FAIL] MixerViewModelDynamicInsertsTest\n");
    return g_failures == 0 ? 0 : 1;
}
