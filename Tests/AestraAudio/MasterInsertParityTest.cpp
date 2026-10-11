// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V8-S4: the master chain works like a channel chain. Same operations through the mixer
// view model, on a channel and on the master, must have the same effect on the engine's
// chain and on what the view shows afterwards: bypass, dry/wet, reorder, remove, undo.
// No pixels: real TrackManager + MixerViewModel.

#include "MixerViewModel.h"

#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/SamplerPlugin.h"

#include <iostream>
#include <memory>
#include <string>

namespace {
using namespace Aestra;
int g_failures = 0;
void expect(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}
Audio::PluginInstancePtr makeSampler() {
    auto plugin = std::make_shared<Audio::Plugins::SamplerPlugin>();
    plugin->initialize(48000.0, 512);
    return plugin;
}

void runOn(const char* label, Audio::TrackManager& tm, Audio::MixerChannel& channel, uint32_t id) {
    std::cout << "[" << label << "]\n";
    auto& chain = channel.getEffectChain();
    for (size_t slot = 0; slot < 3; ++slot)
        chain.insertPlugin(slot, makeSampler());
    const uint64_t first = chain.getSlotInstanceId(0);
    const uint64_t third = chain.getSlotInstanceId(2);

    auto slotMap = tm.getChannelSlotMapShared();
    MixerViewModel vm;
    vm.setCommandHistory(&tm.getCommandHistory());
    vm.setTrackManager(&tm);
    vm.syncFromEngine(tm, *slotMap);
    auto viewOf = [&]() { return id == 0 ? vm.getMaster() : vm.getChannelById(id); };
    expect(viewOf() && !viewOf()->inserts[0].isEmpty && !viewOf()->inserts[2].isEmpty, "the view shows the three inserts");

    vm.setInsertBypass(id, 1, true);
    expect(chain.isSlotBypassed(1), "bypass reaches the engine chain");
    vm.syncFromEngine(tm, *slotMap);
    expect(viewOf()->inserts[1].bypassed, "and the view shows it");
    vm.setInsertBypass(id, 1, false);
    expect(!chain.isSlotBypassed(1), "un-bypass reaches the engine chain");

    vm.setInsertMix(id, 0, 0.25f);
    expect(std::abs(chain.getSlotDryWetMix(0) - 0.25f) < 1e-6f, "dry/wet reaches the engine chain");

    vm.moveInsert(id, 0, 2);
    expect(chain.getSlotInstanceId(2) == first && chain.getSlotInstanceId(0) == third, "reorder swaps the instances in the chain");
    tm.getCommandHistory().undo();
    expect(chain.getSlotInstanceId(0) == first, "undo puts the reorder back");

    vm.removeInsert(id, 2);
    expect(chain.isSlotEmpty(2), "remove empties the slot in the chain");
    tm.getCommandHistory().undo();
    vm.syncFromEngine(tm, *slotMap);
    expect(!chain.isSlotEmpty(2) && !viewOf()->inserts[2].isEmpty, "undo brings the plugin back, and the view shows it");
}
} // namespace

int main() {
    Audio::TrackManager tm;
    Audio::MixerChannel* plain = tm.addChannel("plain");
    if (!plain || !tm.getMasterChannel() || !tm.getChannelSlotMapShared()) {
        std::cerr << "[FAIL] setup\n";
        return 1;
    }
    runOn("channel", tm, *plain, plain->getChannelId());
    runOn("master", tm, *tm.getMasterChannel(), 0);
    std::cout << (g_failures == 0 ? "[PASS] MasterInsertParityTest\n" : "[FAIL] MasterInsertParityTest\n");
    return g_failures == 0 ? 0 : 1;
}
