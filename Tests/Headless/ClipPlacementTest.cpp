// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-W6: placing a clip goes through the command history as ONE undo step, with the lane
// (and its owning Track, FD-14) it may need. Undo removes both, redo restores both, the
// project is marked modified, and a failed placement leaves no history and no lane.

#include "Commands/ClipPlacement.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

ClipInstance makeClip(double startBeat) {
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = startBeat;
    clip.durationBeats = 4.0;
    return clip;
}

void testEmptyProjectGetsLaneTrackAndClipAsOneStep() {
    TrackManager manager;
    auto& playlist = manager.getPlaylistModel();
    auto& history = manager.getCommandHistory();
    const size_t tracksBefore = manager.getTracks().size();
    manager.setModified(false);
    const ClipInstance clip = makeClip(2.0);

    check(placeClipAsOneUndoStep(manager, PlaylistLaneID{}, clip, "Sample Lane", "Add Sample Clip"),
          "placing into an empty project succeeds");
    check(playlist.getLaneCount() == 1 && playlist.getClip(clip.id) != nullptr, "a lane and the clip exist");
    check(manager.getTracks().size() == tracksBefore + 1, "the new lane has its owning Track (FD-14)");
    check(manager.isModified(), "the project is marked modified");
    check(history.canUndo(), "it is on the undo stack");

    history.undo();
    check(playlist.getLaneCount() == 0 && playlist.getClip(clip.id) == nullptr,
          "ONE undo removes both the clip and the lane it needed");
    check(manager.getTracks().size() == tracksBefore, "and the Track");
    check(!history.canUndo(), "nothing else was recorded");

    history.redo();
    check(playlist.getLaneCount() == 1 && playlist.getClip(clip.id) != nullptr, "redo restores both");
}

void testExistingLaneRecordsOnlyTheClip() {
    TrackManager manager;
    auto& playlist = manager.getPlaylistModel();
    const PlaylistLaneID lane = playlist.createLane("Lane");
    const ClipInstance clip = makeClip(0.0);

    check(placeClipAsOneUndoStep(manager, lane, clip, "unused", "Add Clip"), "placing into a lane succeeds");
    check(playlist.getLaneCount() == 1, "no lane is created when one is given");
    manager.getCommandHistory().undo();
    check(playlist.getClip(clip.id) == nullptr && playlist.getLaneCount() == 1, "undo removes only the clip");
}

void testFailedPlacementLeavesNothing() {
    TrackManager manager;
    auto& playlist = manager.getPlaylistModel();
    const PlaylistLaneID missing = PlaylistLaneID::generate(); // valid id, not in the playlist
    check(!placeClipAsOneUndoStep(manager, missing, makeClip(0.0), "Lane", "Add Clip"),
          "placing on a lane that does not exist fails");
    check(!manager.getCommandHistory().canUndo(), "a failed placement is not history");
    check(playlist.getLaneCount() == 0, "and creates no lane");
}

void testClipWithoutIdStillSucceeds() {
    TrackManager manager;
    ClipInstance clip = makeClip(0.0);
    clip.id = ClipInstanceID{};
    check(placeClipAsOneUndoStep(manager, PlaylistLaneID{}, clip, "Lane", "Add Clip"),
          "a clip with no id is given one, not reported as a failure");
    check(manager.getPlaylistModel().getLaneCount() == 1, "and is placed");
}

} // namespace

int main() {
    testEmptyProjectGetsLaneTrackAndClipAsOneStep();
    testExistingLaneRecordsOnlyTheClip();
    testFailedPlacementLeavesNothing();
    testClipWithoutIdStillSucceeds();
    if (g_failures) {
        std::cout << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ClipPlacementTest: all checks passed\n";
    return 0;
}
