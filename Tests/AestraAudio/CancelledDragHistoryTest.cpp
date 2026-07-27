// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// User cancellation is not project history (#551).
//
// TrackManagerUI::cancelInstantClipDrag used to put the clip back by pushing a
// MoveClipCommand. That is undoable in exactly the wrong direction: the command
// captures the clip's CURRENT position as its "original", and at cancel time the
// current position is the dragged one. So the revert executed correctly, then sat
// on the undo stack waiting to re-apply the movement the user had just cancelled.
// Pushing it also cleared the redo stack, so cancelling a drag destroyed whatever
// the user still had to redo.
//
// The live drag never went through the history in the first place —
// updateInstantClipDrag moves the model directly for smoothness — so a cancel has
// nothing to undo, only a position to restore.
//
// TrackManagerUI is not headless-testable. What is asserted here is the shape the
// cancel path now has, and the shape it used to have, against the same model the
// UI drives: the first block shows a command-based revert leaving the cancelled
// position on the undo stack, the second shows a direct restore leaving history
// untouched.

#include "Commands/AddClipCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Models/TrackManager.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

using namespace Aestra::Audio;

constexpr double kOriginalBeat = 0.0;
constexpr double kDraggedBeat = 8.0;

struct Fixture {
    std::unique_ptr<TrackManager> tracks = std::make_unique<TrackManager>();
    PlaylistLaneID laneA;
    PlaylistLaneID laneB;
    ClipInstanceID clipId;

    Fixture() {
        auto& playlist = tracks->getPlaylistModel();
        laneA = playlist.createLane("A");
        laneB = playlist.createLane("B");

        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.name = "Dragged";
        clip.startBeat = kOriginalBeat;
        clip.durationBeats = 4.0;
        clipId = playlist.addClip(laneA, clip);
        tracks->setModified(false);
    }

    // What updateInstantClipDrag does every frame: move the model directly, no
    // command, so the drag stays smooth and leaves no history behind.
    void dragTo(double beat, const PlaylistLaneID& lane) {
        tracks->getPlaylistModel().moveClip(clipId, beat, lane);
    }

    double beat() const {
        const auto* clip = tracks->getPlaylistModel().getClip(clipId);
        return clip ? clip->startBeat : -1.0;
    }
};

} // namespace

int main() {
    // --- why the command-based revert was wrong --------------------------------
    // Reverting through the history does restore the position, but it leaves the
    // cancelled movement on the undo stack, one Ctrl+Z away from coming back.
    {
        Fixture f;
        auto& playlist = f.tracks->getPlaylistModel();
        auto& history = f.tracks->getCommandHistory();

        f.dragTo(kDraggedBeat, f.laneB);

        auto revert = std::make_shared<MoveClipCommand>(playlist, f.clipId, kOriginalBeat, f.laneA);
        history.pushAndExecute(revert);
        require(f.beat() == kOriginalBeat, "The command-based revert did not restore the position");

        require(history.canUndo(), "Precondition: the revert command is on the undo stack");
        require(history.undo(), "Undo of the revert command failed");
        require(f.beat() == kDraggedBeat,
                "Precondition for this whole test: undoing a command-based revert resurrects the "
                "cancelled drag. If this ever stops being true, the cancel path can be simplified.");
    }

    // --- the cancel path: restore, record nothing ------------------------------
    {
        Fixture f;
        auto& playlist = f.tracks->getPlaylistModel();
        auto& history = f.tracks->getCommandHistory();

        f.dragTo(kDraggedBeat, f.laneB);
        require(f.beat() == kDraggedBeat, "Precondition: the drag moved the clip");

        // cancelInstantClipDrag
        playlist.moveClip(f.clipId, kOriginalBeat, f.laneA);

        require(f.beat() == kOriginalBeat, "Cancelling a drag must restore the original position");
        require(playlist.findClipLane(f.clipId) == f.laneA, "Cancelling a drag must restore the original lane");
        require(!history.canUndo(), "A cancelled drag must not put anything on the undo stack");
        require(!f.tracks->isModified(),
                "A cancelled drag leaves the project exactly as it was, so it is not a modification");
    }

    // --- cancelling must not destroy the user's redo ---------------------------
    // pushAndExecute clears the redo stack. Reverting through the history therefore
    // threw away redo state that had nothing to do with the drag.
    {
        Fixture f;
        auto& playlist = f.tracks->getPlaylistModel();
        auto& history = f.tracks->getCommandHistory();

        // An unrelated edit, undone, so there is something to redo.
        ClipInstance other;
        other.id = ClipInstanceID::generate();
        other.name = "Other";
        other.startBeat = 16.0;
        other.durationBeats = 4.0;
        history.pushAndExecute(std::make_shared<AddClipCommand>(playlist, f.laneB, other));
        require(history.undo(), "Setup: undoing the unrelated edit failed");
        require(history.canRedo(), "Setup: the unrelated edit should be redoable");

        f.dragTo(kDraggedBeat, f.laneB);
        playlist.moveClip(f.clipId, kOriginalBeat, f.laneA); // cancelInstantClipDrag

        require(history.canRedo(), "Cancelling a drag must not discard the user's redo history");
        require(history.redo(), "Redo after a cancelled drag failed");
        require(playlist.getClip(other.id) != nullptr, "Redo after a cancelled drag lost the unrelated edit");
    }

    std::cout << "[PASS] CancelledDragHistoryTest\n";
    return 0;
}
