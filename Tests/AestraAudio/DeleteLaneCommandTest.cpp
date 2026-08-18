// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-14 phase-5: deleting a take lane is undoable and identity-stable.
//
// Take lanes accumulate (one per take) and the UI exposes "Delete Lane" from
// the track context menu. The command must detach the lane from its owning
// Track, remove the lane AND its clips, and — on undo — restore the lane with
// its ORIGINAL id, clips, and ownership so nothing silently renumbers.

#include "Commands/DeleteLaneCommand.h"
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
    clip.name = "Take clip";
    clip.startBeat = 4.0;
    clip.durationBeats = 4.0;
    return clip;
}

} // namespace

int main() {
    // --- deleting an owned take lane detaches, removes, restores on undo ------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        auto primaryCmd = std::make_shared<CreateLaneCommand>(playlist, "Track 1");
        primaryCmd->execute();
        const PlaylistLaneID primaryId = primaryCmd->getLaneId();
        const uint64_t trackId = tracks.createTrack(primaryId, "Track 1");

        auto takeCmd = std::make_shared<CreateLaneCommand>(playlist, "Take 1");
        takeCmd->execute();
        const PlaylistLaneID takeId = takeCmd->getLaneId();
        require(tracks.attachLaneToTrack(trackId, takeId), "Take lane did not attach");

        // A second take AFTER the deleted one, so laneIds order can prove the
        // restored lane lands back in the middle — not appended at the end.
        auto take3Cmd = std::make_shared<CreateLaneCommand>(playlist, "Take 3");
        take3Cmd->execute();
        const PlaylistLaneID take3Id = take3Cmd->getLaneId();
        require(tracks.attachLaneToTrack(trackId, take3Id), "Third lane did not attach");

        // An unowned lane AFTER everything, so playlist order can prove the
        // restored lane returns to its original row — not the bottom.
        auto tailCmd = std::make_shared<CreateLaneCommand>(playlist, "Orphan Tail");
        tailCmd->execute();
        const PlaylistLaneID tailId = tailCmd->getLaneId();

        const ClipInstance clip = makeClip();
        require(playlist.addClip(takeId, clip).isValid(), "Clip was not placed on the take lane");

        // Playlist order: [primary, take, take3, tail]. laneIds: [primary, take, take3].
        require(playlist.getLaneId(0) == primaryId && playlist.getLaneId(1) == takeId &&
                    playlist.getLaneId(2) == take3Id && playlist.getLaneId(3) == tailId,
                "Fixture playlist order is wrong");

        history.pushAndExecute(std::make_shared<DeleteLaneCommand>(tracks, takeId));
        require(playlist.getLane(takeId) == nullptr, "Take lane still present after delete");
        require(playlist.getClip(clip.id) == nullptr, "Take clip still present after delete");
        const Track* track = tracks.getTrack(trackId);
        require(track && track->laneIds.size() == 2 && track->laneIds[0] == primaryId &&
                    track->laneIds[1] == take3Id,
                "Lane was not detached from the track");

        require(history.undo(), "Undo of the lane delete failed");
        const PlaylistLane* restored = playlist.getLane(takeId);
        require(restored != nullptr, "Undo did not restore the take lane");
        require(restored->id == takeId, "Undo did not restore the lane's original id");
        require(restored->trackId == trackId, "Undo did not restore lane ownership");
        require(restored->clips.size() == 1 && restored->clips[0].id == clip.id,
                "Undo did not restore the take clip");
        const Track* trackAfterUndo = tracks.getTrack(trackId);
        require(trackAfterUndo && trackAfterUndo->laneIds.size() == 3 &&
                    trackAfterUndo->laneIds[0] == primaryId && trackAfterUndo->laneIds[1] == takeId &&
                    trackAfterUndo->laneIds[2] == take3Id,
                "Undo did not restore the lane's position within the track");
        require(playlist.getLaneId(0) == primaryId && playlist.getLaneId(1) == takeId &&
                    playlist.getLaneId(2) == take3Id && playlist.getLaneId(3) == tailId,
                "Undo did not restore the lane's playlist position");

        require(history.redo(), "Redo of the lane delete failed");
        require(playlist.getLane(takeId) == nullptr, "Redo did not remove the lane again");
        require(playlist.getClip(clip.id) == nullptr, "Redo did not remove the clip again");
    }

    // --- deleting an unowned lane removes it outright, undo restores it -------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        auto laneCmd = std::make_shared<CreateLaneCommand>(playlist, "Orphan");
        laneCmd->execute();
        const PlaylistLaneID laneId = laneCmd->getLaneId();

        const ClipInstance clip = makeClip();
        require(playlist.addClip(laneId, clip).isValid(), "Clip was not placed on the orphan lane");

        history.pushAndExecute(std::make_shared<DeleteLaneCommand>(tracks, laneId));
        require(playlist.getLane(laneId) == nullptr, "Orphan lane still present after delete");

        require(history.undo(), "Undo of the orphan delete failed");
        const PlaylistLane* restored = playlist.getLane(laneId);
        require(restored != nullptr, "Undo did not restore the orphan lane");
        require(restored->id == laneId, "Undo did not restore the orphan lane's id");
        require(restored->trackId == 0, "Undo re-attached the orphan lane to a track");
        require(restored->clips.size() == 1 && restored->clips[0].id == clip.id,
                "Undo did not restore the orphan clip");
    }

    // --- deleting an unknown lane is a safe no-op -----------------------------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        const size_t lanesBefore = playlist.getLaneCount();
        history.pushAndExecute(std::make_shared<DeleteLaneCommand>(tracks, PlaylistLaneID::generate()));
        require(playlist.getLaneCount() == lanesBefore, "A no-op delete changed the playlist");
        require(history.undo(), "Undo of a no-op delete must succeed cleanly");
        require(playlist.getLaneCount() == lanesBefore, "Undo of a no-op delete changed the playlist");
    }

    std::cout << "[PASS] DeleteLaneCommandTest\n";
    return 0;
}
