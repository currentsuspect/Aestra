// © 2026 Aestra Studios — All Rights Reserved.

#include "TimelineInteractionPolicy.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++failures;
    }
}

void testStableSelectionLifecycle() {
    const PlaylistLaneID first = PlaylistLaneID::generate();
    const PlaylistLaneID second = PlaylistLaneID::generate();
    const PlaylistLaneID removed = PlaylistLaneID::generate();
    TimelineTrackSelection selection;

    selection.apply(first, TrackSelectionIntent::Replace);
    selection.apply(second, TrackSelectionIntent::Add);
    check(selection.size() == 2, "additive selection keeps both stable lane identities");

    selection.apply(first, TrackSelectionIntent::Toggle);
    check(!selection.contains(first) && selection.contains(second), "toggle removes an already selected lane");
    selection.apply(first, TrackSelectionIntent::Toggle);
    check(selection.contains(first), "toggle adds an unselected lane");

    selection.apply(removed, TrackSelectionIntent::Add);
    selection.retainOnly({first, second});
    check(selection.size() == 2 && !selection.contains(removed),
          "widget rebuild pruning removes deleted lanes without touching surviving selection");

    selection.selectAll({first, second, PlaylistLaneID{}});
    check(selection.size() == 2, "select all ignores invalid lane identities");
    selection.clear();
    check(selection.empty(), "clear removes every selected lane");
}

void testModifierSelectionIntent() {
    check(trackSelectionIntentForModifierState(false, false) == TrackSelectionIntent::Replace,
          "plain clicks replace selection");
    check(trackSelectionIntentForModifierState(false, true) == TrackSelectionIntent::Add, "Shift adds to selection");
    check(trackSelectionIntentForModifierState(true, false) == TrackSelectionIntent::Toggle,
          "the platform toggle modifier toggles selection");
    check(trackSelectionIntentForModifierState(true, true) == TrackSelectionIntent::Toggle,
          "the toggle modifier takes precedence over Shift");
}

void testProjectLoopPolicy() {
    check(timelineLoopPresetFromId(6) == TimelineLoopPreset::Project,
          "project preset id resolves to the single typed authority");
    check(timelineLoopPresetFromId(99) == TimelineLoopPreset::Off, "invalid preset ids fail closed to Off");
    check(std::abs(resolveProjectLoopEndBeat(23.5, 4) - 23.5) < 1e-9,
          "non-empty projects loop to their arrangement end");
    check(std::abs(resolveProjectLoopEndBeat(0.0, 4) - 64.0) < 1e-9,
          "empty 4/4 projects use the documented 16-bar range");
    check(std::abs(resolveProjectLoopEndBeat(0.0, 0) - 16.0) < 1e-9,
          "invalid time signatures still produce a bounded loop range");
}

} // namespace

int main() {
    testStableSelectionLifecycle();
    testModifierSelectionIntent();
    testProjectLoopPolicy();

    if (failures == 0) {
        std::cout << "Timeline interaction policy tests passed\n";
        return 0;
    }
    std::cout << failures << " timeline interaction policy test(s) failed\n";
    return 1;
}
