// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A drop that appends a lane is one undo step (#551).
//
// TrackManagerUI::onDrop used to call playlist.createLane() directly and then push
// the AddClipCommand on its own. The lane was therefore outside history: undoing the
// drop removed the clip and left an empty orphan lane the user never asked for and
// could not undo away. It now builds a CommandTransaction holding the lane command
// and the clip command, executed stepwise and adopted via markExecuted() +
// pushExecuted().
//
// Grouping them exposed a second defect. CreateLaneCommand::redo() used to mint a
// fresh lane ID ("the ID will be different on redo since we can't reuse the old
// ID"), but AddClipCommand still holds the lane it was constructed with — so redo
// re-created an empty lane and dropped the clip onto an ID that no longer existed.
//
// This test drives the commands directly. The UI layer is not headless-testable, but
// the transaction shape it now builds is exactly what is asserted here.

#include "Commands/AddClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "Commands/CreateLaneCommand.h"
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

ClipInstance makeClip() {
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.name = "Dropped";
    clip.startBeat = 4.0;
    clip.durationBeats = 4.0;
    return clip;
}

} // namespace

int main() {
    // --- a drop onto an appended lane undoes and redoes as one step -------------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        const size_t lanesBefore = playlist.getLaneCount();

        // What onDrop does: append the lane through a command, place the clip, then
        // adopt both as a single already-executed transaction.
        auto laneCommand = std::make_shared<CreateLaneCommand>(playlist, "Lane 1");
        laneCommand->execute();
        const PlaylistLaneID laneId = laneCommand->getLaneId();
        require(laneId.isValid(), "Lane command did not produce a lane");

        const ClipInstance clip = makeClip();
        auto clipCommand = std::make_shared<AddClipCommand>(playlist, laneId, clip);
        clipCommand->execute();
        require(playlist.getClip(clip.id) != nullptr, "Clip was not placed on the appended lane");

        auto transaction = std::make_shared<CommandTransaction>("Add Pattern Clip");
        transaction->add(laneCommand);
        transaction->add(clipCommand);
        transaction->markExecuted();
        require(history.pushExecuted(transaction), "The drop transaction was refused by the history");

        require(playlist.getLaneCount() == lanesBefore + 1, "Drop did not append its lane");

        // One Ctrl+Z removes the clip AND the lane it had to create.
        require(history.undo(), "Undo of the drop transaction failed");
        require(playlist.getClip(clip.id) == nullptr, "Undo left the dropped clip behind");
        require(playlist.getLaneCount() == lanesBefore,
                "Undo left an orphan lane behind — the lane was not part of the undo step");

        // Redo must restore the lane's ORIGINAL identity, or the clip command
        // re-adds to a lane that no longer exists and the clip is silently lost.
        require(history.redo(), "Redo of the drop transaction failed");
        require(playlist.getLaneCount() == lanesBefore + 1, "Redo did not restore the lane");
        require(laneCommand->getLaneId() == laneId, "Redo minted a new lane ID instead of restoring the original");
        require(playlist.getClip(clip.id) != nullptr, "Redo lost the dropped clip");
        require(playlist.findClipLane(clip.id) == laneId, "Redo restored the clip onto the wrong lane");
    }

    // --- a drop onto an existing lane records only the clip ---------------------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        const PlaylistLaneID existing = playlist.createLane("Existing");
        const size_t lanesBefore = playlist.getLaneCount();

        const ClipInstance clip = makeClip();
        auto clipCommand = std::make_shared<AddClipCommand>(playlist, existing, clip);
        clipCommand->execute();

        auto transaction = std::make_shared<CommandTransaction>("Add Pattern Clip");
        transaction->add(clipCommand); // no lane command: the lane was reused
        transaction->markExecuted();
        require(history.pushExecuted(transaction), "The drop transaction was refused by the history");

        require(history.undo(), "Undo of the drop transaction failed");
        require(playlist.getClip(clip.id) == nullptr, "Undo left the dropped clip behind");
        require(playlist.getLaneCount() == lanesBefore, "Undo removed a lane the drop did not create");
    }

    // --- a failed drop is not history, and leaves nothing behind ----------------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        const size_t lanesBefore = playlist.getLaneCount();

        auto laneCommand = std::make_shared<CreateLaneCommand>(playlist, "Lane 1");
        laneCommand->execute();
        require(playlist.getLaneCount() == lanesBefore + 1, "Lane command did not append a lane");

        // The clip cannot be placed — this stands in for a pattern that could not be
        // resolved or a source that decoded to nothing. onDrop rolls the lane back
        // directly, without recording anything.
        const ClipInstance clip = makeClip();
        auto clipCommand = std::make_shared<AddClipCommand>(playlist, PlaylistLaneID::generate(), clip);
        clipCommand->execute();
        require(playlist.getClip(clip.id) == nullptr, "The failed drop unexpectedly placed its clip");

        laneCommand->undo();

        require(playlist.getLaneCount() == lanesBefore, "A failed drop left its appended lane behind");
        require(!history.canUndo(), "A failed drop was recorded in the undo history");
    }

    std::cout << "[PASS] DropTransactionTest\n";
    return 0;
}
