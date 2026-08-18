// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Clip Movement Contract (vault: Track Lane Channel Ownership Contract §23,
// freeze 2026-08-18 @ 30d28e4).
//
// A lane is not an owner of a clip; the track is the placement owner. Moving
// a clip between lanes of the same track, or between tracks, NEVER changes
// the clip's identity — id, pattern, and metadata survive; the placement
// track derives from the destination lane. Lane-attached automation curves
// are de-facto track-scoped (§23.3) and must not transfer, copy, or clone
// when clips move.

#include "Commands/CreateLaneCommand.h"
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

ClipInstance makeClip() {
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.name = "Contract clip";
    clip.startBeat = 0.0;
    clip.durationBeats = 4.0;
    return clip;
}

} // namespace

int main() {
    // --- cross-track move: identity stable, placement track changes --------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        auto t1PrimaryCmd = std::make_shared<CreateLaneCommand>(playlist, "Track 1");
        t1PrimaryCmd->execute();
        const PlaylistLaneID t1Primary = t1PrimaryCmd->getLaneId();
        const uint64_t track1 = tracks.createTrack(t1Primary, "Track 1");

        auto t1TakeCmd = std::make_shared<CreateLaneCommand>(playlist, "Take 1");
        t1TakeCmd->execute();
        const PlaylistLaneID t1Take = t1TakeCmd->getLaneId();
        require(tracks.attachLaneToTrack(track1, t1Take), "Take lane did not attach");

        auto t2PrimaryCmd = std::make_shared<CreateLaneCommand>(playlist, "Track 2");
        t2PrimaryCmd->execute();
        const PlaylistLaneID t2Primary = t2PrimaryCmd->getLaneId();
        const uint64_t track2 = tracks.createTrack(t2Primary, "Track 2");

        // Lane-attached automation bag, de-facto track-scoped (§23.3): must
        // not follow the clip.
        auto* takeLane = playlist.getLane(t1Take);
        takeLane->automationCurves.emplace_back("Take gain", AutomationTarget::Custom);
        const size_t curvesBefore = takeLane->automationCurves.size();

        const ClipInstance clip = makeClip();
        require(playlist.addClip(t1Take, clip).isValid(), "Clip was not placed on the take lane");

        // Track 1 / Lane take -> Track 2 / Lane primary.
        playlist.moveClip(clip.id, 8.0, t2Primary);

        const ClipInstance* moved = playlist.getClip(clip.id);
        require(moved != nullptr, "Cross-track move destroyed the clip identity");
        require(moved->id == clip.id, "Cross-track move changed the clip id");
        require(moved->startBeat == 8.0, "Cross-track move lost the arrangement position");
        require(playlist.findClipLane(clip.id) == t2Primary, "Clip landed on the wrong lane");
        const auto* destLane = playlist.getLane(t2Primary);
        require(destLane && destLane->trackId == track2, "Placement track is not the destination track");
        require(playlist.getLane(t1Take)->clips.empty(), "Clip still present in the source lane");
        takeLane = playlist.getLane(t1Take);
        require(takeLane->automationCurves.size() == curvesBefore, "Automation moved with the clip");

        // Undo of the cross-track move restores the original placement.
        auto moveCmd =
            std::make_shared<MoveClipCommand>(playlist, clip.id, 0.0, t1Take, 8.0, t2Primary);
        history.pushAndExecute(moveCmd);
        require(history.undo(), "Undo of the cross-track move failed");
        require(playlist.findClipLane(clip.id) == t1Take, "Undo did not restore the source lane");
        require(playlist.getClip(clip.id)->startBeat == 0.0, "Undo did not restore the source position");
        require(playlist.getClip(clip.id)->id == clip.id, "Undo changed the clip id");
    }

    // --- same-track lane move: identity stable, automation untouched -------
    {
        auto tracksOwner = std::make_unique<TrackManager>();
        auto& tracks = *tracksOwner;
        auto& playlist = tracks.getPlaylistModel();
        auto& history = tracks.getCommandHistory();

        auto primaryCmd = std::make_shared<CreateLaneCommand>(playlist, "Track 1");
        primaryCmd->execute();
        const PlaylistLaneID primary = primaryCmd->getLaneId();
        const uint64_t track1 = tracks.createTrack(primary, "Track 1");

        auto takeCmd = std::make_shared<CreateLaneCommand>(playlist, "Take 1");
        takeCmd->execute();
        const PlaylistLaneID take = takeCmd->getLaneId();
        require(tracks.attachLaneToTrack(track1, take), "Take lane did not attach");

        auto* takeLane = playlist.getLane(take);
        takeLane->automationCurves.emplace_back();
        const size_t curvesBefore = takeLane->automationCurves.size();

        const ClipInstance clip = makeClip();
        require(playlist.addClip(take, clip).isValid(), "Clip was not placed on the take lane");

        // Same track: take lane -> primary lane.
        playlist.moveClip(clip.id, 2.0, primary);

        const ClipInstance* moved = playlist.getClip(clip.id);
        require(moved != nullptr && moved->id == clip.id, "Same-track move changed the clip identity");
        require(playlist.findClipLane(clip.id) == primary, "Same-track move landed on the wrong lane");
        require(playlist.getLane(primary)->trackId == track1, "Placement track changed on a same-track move");
        takeLane = playlist.getLane(take);
        require(takeLane->automationCurves.size() == curvesBefore, "Automation moved with the clip");
        require(takeLane->clips.empty(), "Clip still present in the source lane");

        auto moveCmd = std::make_shared<MoveClipCommand>(playlist, clip.id, 0.0, take, 2.0, primary);
        history.pushAndExecute(moveCmd);
        require(history.undo(), "Undo of the same-track move failed");
        require(playlist.findClipLane(clip.id) == take, "Undo did not restore the take lane");
        require(playlist.getLane(take)->clips.size() == 1, "Undo did not restore the clip");
    }

    std::cout << "[PASS] ClipMovementContractTest\n";
    return 0;
}