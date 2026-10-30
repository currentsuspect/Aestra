// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-T2: only tracks nobody has touched fold into "N empty tracks · show". Folding must never hide work.

#include "Components/TimelineFold.h"

#include <cassert>
#include <cstdio>

using namespace Aestra::Audio;

int main() {
    PlaylistLane fresh;
    fresh.name = "Track 7";
    assert(isUntouchedTrackLane(fresh, 1) && "a new, unnamed, empty track folds");

    PlaylistLane renamed = fresh;
    renamed.name = "Vocals";
    assert(!isUntouchedTrackLane(renamed, 1) && "a renamed track stays (you meant to use it)");

    PlaylistLane withClip = fresh;
    withClip.clips.emplace_back();
    assert(!isUntouchedTrackLane(withClip, 1) && "a track with a clip stays");

    PlaylistLane withAutomation = fresh;
    withAutomation.automationCurves.emplace_back();
    assert(isUntouchedTrackLane(withAutomation, 1) && "an automation curve with no points is still untouched");
    withAutomation.automationCurves.back().addPoint(0.0, 0.5f, 22050.0);
    assert(!isUntouchedTrackLane(withAutomation, 1) && "a track with automation points stays");

    assert(!isUntouchedTrackLane(fresh, 2) && "a track with several lanes stays");

    for (const char* name : {"Track", "Track ", "Track 7b", "track 7", "Track 007x"})
        assert(!isDefaultTrackName(name));
    assert(isDefaultTrackName("Track 12"));
    std::printf("[PASS] only untouched tracks fold: renamed, used, automated or multi-lane tracks always show\n");
    return 0;
}
