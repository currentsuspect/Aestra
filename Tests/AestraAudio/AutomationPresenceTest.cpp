// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// FD-16: automation presence aggregation — per-channel curve counts and
// target masks derived from persisted playlist lanes. Read-only exposure;
// authoring remains parked (FD-13).

#include "Models/AutomationPresence.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using namespace Aestra;
using namespace Aestra::Audio;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

Audio::PlaylistLane makeLane() {
    return Audio::PlaylistLane(0);
}

} // namespace

int main() {
    // Volume bit, Pan bit, Custom bit are distinct and stable.
    expect(Audio::automationTargetBit(Audio::AutomationTarget::Volume) == 1u,
           "Volume target = bit0");
    expect(Audio::automationTargetBit(Audio::AutomationTarget::Pan) == 2u,
           "Pan target = bit1");
    expect(Audio::automationTargetBit(Audio::AutomationTarget::Custom) == 4u,
           "Custom target = bit2");
    // Unrecognized in-range ids (2–254) bucket under the generic bit.
    expect(Audio::automationTargetBit(static_cast<Audio::AutomationTarget>(7)) == 4u,
           "unrecognized target buckets with custom");

    {
        // Empty lanes → no entries at all.
        std::vector<Audio::PlaylistLane> lanes{makeLane(), makeLane()};
        const auto presence = Audio::computeAutomationPresence(lanes);
        expect(presence.empty(), "no curves → empty presence map");
    }

    {
        // Curves accumulate per channel; masks union across curves.
        auto lane = makeLane();
        Audio::AutomationCurve volume("Vol", Audio::AutomationTarget::Volume);
        volume.mixerChannelId = 42;
        volume.addPoint(0.0, 0.5, 1.0);
        Audio::AutomationCurve pan("Pan", Audio::AutomationTarget::Pan);
        pan.mixerChannelId = 42;
        pan.addPoint(1.0, 0.25, 1.0);
        lane.automationCurves.push_back(volume);
        lane.automationCurves.push_back(pan);

        Audio::AutomationCurve other("Cst", Audio::AutomationTarget::Custom);
        other.mixerChannelId = 7;
        other.addPoint(2.0, 1.0, 1.0);
        lane.automationCurves.push_back(other);

        const auto presence = Audio::computeAutomationPresence({lane});
        expect(presence.size() == 2, "two distinct channels present");

        const auto ch42 = presence.find(42);
        expect(ch42 != presence.end(), "channel 42 present");
        if (ch42 != presence.end()) {
            expect(ch42->second.curveCount == 2, "channel 42 counts both curves");
            expect(ch42->second.targetMask == (1u | 2u),
                   "channel 42 mask unions Volume|Pan");
        }

        const auto ch7 = presence.find(7);
        expect(ch7 != presence.end(), "channel 7 present");
        if (ch7 != presence.end()) {
            expect(ch7->second.curveCount == 1, "channel 7 single curve");
            expect(ch7->second.targetMask == 4u, "channel 7 custom bit");
        }
    }

    {
        // Unassigned curves (mixerChannelId == 0) match no channel — zero is
        // unassigned, never master.
        auto lane = makeLane();
        Audio::AutomationCurve unassigned("Orphan", Audio::AutomationTarget::Volume);
        unassigned.mixerChannelId = 0;
        lane.automationCurves.push_back(unassigned);

        const auto presence = Audio::computeAutomationPresence({lane});
        expect(presence.empty(), "unassigned channel id is skipped");
        expect(presence.find(0) == presence.end(), "id 0 is never a key");
    }

    {
        // Curves on multiple lanes targeting the same channel aggregate.
        auto laneA = makeLane();
        auto laneB = makeLane();
        Audio::AutomationCurve a("A", Audio::AutomationTarget::Volume);
        a.mixerChannelId = 9;
        laneA.automationCurves.push_back(a);
        Audio::AutomationCurve b("B", Audio::AutomationTarget::Custom);
        b.mixerChannelId = 9;
        laneB.automationCurves.push_back(b);

        const auto presence =
            Audio::computeAutomationPresence({laneA, laneB});
        const auto ch9 = presence.find(9);
        expect(ch9 != presence.end(), "cross-lane channel present");
        if (ch9 != presence.end()) {
            expect(ch9->second.curveCount == 2, "cross-lane curves sum");
            expect(ch9->second.targetMask == (1u | 4u), "cross-lane mask unions");
        }
    }

    if (g_failures == 0) {
        std::cout << "[PASS] AutomationPresenceTest\n";
        return 0;
    }
    std::cerr << "[FAIL] AutomationPresenceTest: " << g_failures << " failure(s)\n";
    return 1;
}
