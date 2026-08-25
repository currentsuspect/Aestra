// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// FD-16 seam test: persisted lane curves must reach ChannelViewModel through
// MixerViewModel::syncFromEngine — the exact path the routing-map badge reads.
// Exercises real TrackManager + PlaylistModel + ChannelSlotMap, no pixels.
// Authoring stays parked (FD-13); curves here enter through the same public
// model API a loader or future authoring UI would use.

#include "MixerViewModel.h"

#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Aestra;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    Audio::TrackManager tm;
    Audio::MixerChannel* ch = tm.addChannel("presence-src");
    expect(ch != nullptr, "channel created");
    if (!ch) {
        return 1;
    }
    const uint32_t channelId = ch->getChannelId();

    auto& playlist = tm.getPlaylistModel();
    const auto laneId = playlist.createLane("lane0");
    Audio::PlaylistLane* lane = playlist.getLane(laneId);
    expect(lane != nullptr, "lane created");
    if (!lane) {
        return 1;
    }

    // One Volume curve addressing the channel, entered via the public model API.
    Audio::AutomationCurve curve("vol", Audio::AutomationTarget::Volume);
    curve.mixerChannelId = channelId;
    curve.addPoint(0.0, 0.75, 1.0);
    lane->automationCurves.push_back(curve);

    auto slotMapShared = tm.getChannelSlotMapShared();
    expect(slotMapShared != nullptr, "slot map available");
    if (!slotMapShared) {
        return 1;
    }

    Aestra::MixerViewModel vm;
    vm.syncFromEngine(tm, *slotMapShared);

    const auto* vmChannel = vm.getChannelById(channelId);
    expect(vmChannel != nullptr, "channel view model synced");
    if (vmChannel) {
        expect(vmChannel->automationCurveCount == 1,
               "curve reaches the channel view model");
        expect(vmChannel->automationTargetMask == 1u,
               "Volume target maps to bit0");
    }

    const auto* vmMaster = vm.getMaster();
    expect(vmMaster != nullptr, "master synced");
    if (vmMaster) {
        expect(vmMaster->automationCurveCount == 0,
               "untargeted master stays at zero");
    }

    // Curves removed → presence clears on the next sync (no stale badges).
    lane->automationCurves.clear();
    vm.syncFromEngine(tm, *slotMapShared);
    const auto* cleared = vm.getChannelById(channelId);
    expect(cleared != nullptr, "channel still present after clear");
    if (cleared) {
        expect(cleared->automationCurveCount == 0,
               "cleared curves drop out on next sync");
        expect(cleared->automationTargetMask == 0,
               "mask resets with the count");
    }

    if (g_failures == 0) {
        std::cout << "[PASS] MixerViewModelAutomationPresenceTest\n";
        return 0;
    }
    std::cerr << "[FAIL] MixerViewModelAutomationPresenceTest: " << g_failures
              << " failure(s)\n";
    return 1;
}
