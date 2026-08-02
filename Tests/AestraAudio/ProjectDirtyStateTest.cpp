// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Project dirty-state integrity (#551).
//
// The invariant under test: a TrackManager marks its project modified whenever
// something runs through its own command history — with no application-layer
// wiring whatsoever. Dirty tracking used to be installed by AestraApp from
// inside connectAudioToUI(), so it existed only when the audio device came up,
// and individual edit paths had to remember to call markModified() by hand.
// Move-completion, cut and delete never did, so those edits could be lost
// without a save prompt.
//
// Everything below constructs a bare TrackManager. If dirty tracking is not
// owned by TrackManager itself, these assertions fail.

#include "Commands/AddClipCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/RemoveClipCommand.h"
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

ClipInstance makeClip(double startBeat) {
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.name = "Clip";
    clip.startBeat = startBeat;
    clip.durationBeats = 4.0;
    return clip;
}

// Seed a lane holding one clip, leaving the project clean.
ClipInstanceID seedClip(TrackManager& tracks, PlaylistLaneID& laneOut, double startBeat = 0.0) {
    auto& playlist = tracks.getPlaylistModel();
    laneOut = playlist.createLane("Lane");
    const ClipInstanceID clipId = playlist.addClip(laneOut, makeClip(startBeat));
    tracks.setModified(false);
    return clipId;
}

} // namespace

int main() {
    // A freshly constructed project has nothing to save.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        require(!tracksOwner->isModified(), "A new TrackManager must not start out modified");
    }

    // Adding a clip through the history dirties the project. This is the path
    // that already worked (paste/duplicate/paint called markModified() by hand),
    // so it pins the baseline the other cases are compared against.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        const PlaylistLaneID laneId = playlist.createLane("Lane");
        tracks.setModified(false);

        tracks.getCommandHistory().pushAndExecute(
            std::make_shared<AddClipCommand>(playlist, laneId, makeClip(0.0)));
        require(tracks.isModified(), "Adding a clip must mark the project modified");
    }

    // Move completion (TrackManagerUI::finishInstantClipDrag) pushes a
    // MoveClipCommand and nothing else. Dragging a clip to a new position is an
    // edit; it must survive as unsaved work.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        PlaylistLaneID laneId;
        const ClipInstanceID clipId = seedClip(tracks, laneId);

        tracks.getCommandHistory().pushAndExecute(
            std::make_shared<MoveClipCommand>(playlist, clipId, 8.0, laneId));
        require(tracks.isModified(), "Completing a clip move must mark the project modified");
    }

    // Cut (TrackManagerUI::cutSelectedClip) and delete
    // (TrackManagerUI::deleteSelectedClip) both push a RemoveClipCommand.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        PlaylistLaneID laneId;
        const ClipInstanceID clipId = seedClip(tracks, laneId);

        tracks.getCommandHistory().pushAndExecute(std::make_shared<RemoveClipCommand>(playlist, clipId));
        require(tracks.isModified(), "Cutting or deleting a clip must mark the project modified");
    }

    // Undo and redo move the project away from whatever was last written to
    // disk, so both are unsaved changes in their own right.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        PlaylistLaneID laneId;
        const ClipInstanceID clipId = seedClip(tracks, laneId);

        tracks.getCommandHistory().pushAndExecute(std::make_shared<RemoveClipCommand>(playlist, clipId));

        tracks.setModified(false);
        require(tracks.getCommandHistory().undo(), "Undo of a clip removal should succeed");
        require(tracks.isModified(), "Undo must mark the project modified");

        tracks.setModified(false);
        require(tracks.getCommandHistory().redo(), "Redo of a clip removal should succeed");
        require(tracks.isModified(), "Redo must mark the project modified");
    }

    // An operation that fails is not a project change. AddClipCommand reports
    // itself non-undoable when execute() could not place the clip, so the
    // history never records it and never reports a state change. The edit paths
    // used to call markModified() by hand right after pushAndExecute(), which
    // dirtied the project even when the paste had failed.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        playlist.createLane("Lane");
        tracks.setModified(false);

        // No lane was ever created with this ID, so the add cannot land.
        tracks.getCommandHistory().pushAndExecute(
            std::make_shared<AddClipCommand>(playlist, PlaylistLaneID::generate(), makeClip(0.0)));
        require(!tracks.isModified(), "A failed clip add must not mark the project modified");
    }

    // markModified() forwards to the application-layer hook (autosave) on every
    // history-driven edit, not just on the paths that called it explicitly.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        PlaylistLaneID laneId;
        const ClipInstanceID clipId = seedClip(tracks, laneId);

        int onModifiedCount = 0;
        tracks.setOnModified([&onModifiedCount]() { onModifiedCount++; });

        tracks.getCommandHistory().pushAndExecute(
            std::make_shared<MoveClipCommand>(playlist, clipId, 8.0, laneId));
        require(onModifiedCount == 1, "The modified hook must fire once per history-driven edit");
    }

    // Panels register their own history listeners. Dirty tracking is not
    // allowed to be a listener that a later registration can displace, and a
    // panel listener is not allowed to displace dirty tracking either — the
    // two must coexist. (CommandHistory deliberately has no replace-all entry
    // point; this pins the composition behaviour that replaces it.)
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        PlaylistLaneID laneId;
        const ClipInstanceID clipId = seedClip(tracks, laneId);

        int panelRefreshCount = 0;
        tracks.getCommandHistory().addOnStateChanged([&panelRefreshCount]() { panelRefreshCount++; });

        tracks.getCommandHistory().pushAndExecute(std::make_shared<RemoveClipCommand>(playlist, clipId));
        require(panelRefreshCount == 1, "A panel listener must be notified of history changes");
        require(tracks.isModified(),
                "Registering a panel listener must not displace project dirty tracking");
    }

    // -------------------------------------------------------------------
    // Building default content is not an edit (#653).
    //
    // AestraContent's constructor calls addDemoTracks(), which calls
    // addChannel() fifty times. addChannel legitimately marks the project
    // modified, so construction left every fresh launch dirty and closing
    // without touching anything prompted "Unsaved Changes" — confirmed by
    // instrumenting all eight dirty-write sites: exactly one transition,
    // addChannel during ContentConstruction, never cleared.
    //
    // AestraContent cannot be constructed headlessly, so this pins the
    // TrackManager half of the contract: the population-then-clear sequence
    // the constructor now performs must leave the project clean, AND a real
    // edit afterwards must still dirty it.
    //
    // The second half is the important one. It fails if anyone ever "fixes"
    // this class of defect with a startup-wide suppression guard or by
    // weakening addChannel — both of which would make the first assertion
    // pass and this one fail.
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;

        require(!tracks.isModified(), "A newly constructed TrackManager is not modified");

        // What addDemoTracks() does.
        for (int i = 1; i <= 50; ++i) {
            tracks.addChannel("Channel " + std::to_string(i));
        }
        require(tracks.isModified(),
                "addChannel must still mark the project modified — the defect is not "
                "that addChannel dirties, it is that construction never cleared");

        // What the constructor now does after populating.
        tracks.setModified(false);
        require(!tracks.isModified(), "Default content, once built, leaves the project clean");

        // A real user edit through the very same path must dirty it again.
        tracks.addChannel("User added this one");
        require(tracks.isModified(),
                "A channel added after construction must mark the project modified");
    }

    std::cout << "[PASS] ProjectDirtyStateTest\n";
    return 0;
}
