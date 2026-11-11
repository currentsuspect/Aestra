// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Comping (SPEC-022): the pieces every command that touches a comp shares. SetCompRangeCommand makes a swipe;
// DeleteLaneCommand keeps the comp true when a take or the master goes. Both capture the track's comp state before they
// change it, and put it back on undo.

#include "Models/TrackManager.h" // first: Track.h needs the mixer channel header ahead of it
#include "Models/CompRanges.h"
#include "Models/PlaylistModel.h"
#include "Models/Track.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

struct CompSnapshot {
    CompRanges comp;
    PlaylistLaneID compLaneId;
    std::vector<ClipInstance> clips;                    // the master lane's clips
    std::vector<std::pair<PlaylistLaneID, bool>> muted; // every lane the track owns
};

inline CompSnapshot captureComp(PlaylistModel& playlist, const Track& track) {
    CompSnapshot s;
    s.comp = track.comp;
    s.compLaneId = track.compLaneId;
    if (const PlaylistLane* master = track.compLaneId.isValid() ? playlist.getLane(track.compLaneId) : nullptr) s.clips = master->clips;
    for (const PlaylistLaneID& id : track.laneIds)
        if (const PlaylistLane* lane = playlist.getLane(id)) s.muted.emplace_back(id, lane->muted);
    return s;
}

inline void setMasterClips(PlaylistModel& playlist, PlaylistLaneID master, const std::vector<ClipInstance>& clips) {
    if (!master.isValid()) return;
    const PlaylistLane* lane = playlist.getLane(master);
    if (!lane) return;
    std::vector<ClipInstanceID> old;
    for (const ClipInstance& c : lane->clips) old.push_back(c.id);
    for (const ClipInstanceID& id : old) playlist.removeClip(id);
    for (const ClipInstance& c : clips) playlist.addClip(master, c);
}

inline void restoreComp(PlaylistModel& playlist, Track& track, const CompSnapshot& s) {
    track.comp = s.comp;
    track.compLaneId = s.compLaneId;
    for (const auto& [id, muted] : s.muted)
        if (PlaylistLane* lane = playlist.getLane(id)) lane->muted = muted;
    setMasterClips(playlist, track.compLaneId, s.clips);
}

/// The master lane plays the comp (each piece named for its take); the takes it reads from are silent.
inline void rebuildComp(PlaylistModel& playlist, Track& track) {
    std::vector<CompSourceLane> sources;
    for (const PlaylistLaneID& id : track.laneIds) {
        if (id == track.compLaneId) continue;
        PlaylistLane* lane = playlist.getLane(id);
        if (!lane) continue;
        lane->muted = true;
        sources.push_back({id, &lane->clips});
    }
    std::vector<ClipInstance> pieces = compClipsFor(track.comp, sources, playlist.getBPM());
    for (ClipInstance& piece : pieces)
        piece.name = "Take " + std::to_string(track.laneNumber(track.comp.laneAt(piece.startBeat + kCompEpsilon)));
    setMasterClips(playlist, track.compLaneId, pieces);
}

/// True when the comp plays something from `laneId`.
inline bool compUsesLane(const Track& track, PlaylistLaneID laneId) {
    return std::any_of(track.comp.ranges().begin(), track.comp.ranges().end(), [&](const CompRange& r) { return r.laneId == laneId; });
}

/// A lane of the track is being removed: keep the comp true. Deleting the master ends the comp and the takes play
/// again; deleting a take the comp uses leaves a gap (silence, never a guess) where it played.
inline void compAfterLaneRemoved(PlaylistModel& playlist, Track& track, PlaylistLaneID removed, bool wasMaster) {
    if (wasMaster) {
        track.comp.clear();
        track.compLaneId = PlaylistLaneID{};
        for (const PlaylistLaneID& id : track.laneIds)
            if (PlaylistLane* lane = playlist.getLane(id)) lane->muted = false;
        return;
    }
    if (!track.compLaneId.isValid() || !compUsesLane(track, removed)) return;
    track.comp.removeLane(removed);
    rebuildComp(playlist, track);
}

} // namespace Audio
} // namespace Aestra
