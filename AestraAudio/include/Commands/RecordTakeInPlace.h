// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Record where the user armed: a finished take goes onto the track's own lane, as a clip you can see land under the
// playhead. A take lane (a new row under the track) used to be made every time, so the recording left the lane it was
// drawn on and the view followed it. A take lane is now the fallback, for when the lane already has something where the
// take goes: nothing is overwritten or hidden underneath.

#include "AddClipCommand.h"
#include "CommandHistory.h"
#include "CommandTransaction.h"
#include "../Models/PlaylistModel.h"
#include "../Models/UnitManager.h"
#include "../Models/Track.h"

#include <algorithm>
#include <memory>

namespace Aestra {
namespace Audio {

/// True when `clip` fits on the lane without touching any clip already there.
inline bool takeFitsOnLane(const PlaylistLane& lane, const ClipInstance& clip) {
    const double end = clip.startBeat + clip.durationBeats;
    return std::none_of(lane.clips.begin(), lane.clips.end(), [&](const ClipInstance& other) {
        return other.startBeat < end - 1e-6 && other.startBeat + other.durationBeats > clip.startBeat + 1e-6;
    });
}

/// Put the take on the track's own (active) lane as one undoable "Record Take". Returns false, with nothing changed,
/// when there is no such lane, the span is taken, or history refuses it; the caller then makes a take lane.
inline bool recordTakeInPlace(PlaylistModel& playlist, CommandHistory& history, const Track& track, const ClipInstance& clip) {
    const PlaylistLane* lane = track.activeLaneId.isValid() ? playlist.getLane(track.activeLaneId) : nullptr;
    if (!lane || lane->trackId != track.trackId || !takeFitsOnLane(*lane, clip)) return false;
    auto add = std::make_shared<AddClipCommand>(playlist, track.activeLaneId, clip);
    add->execute();
    if (!playlist.getClip(clip.id)) return false;
    auto transaction = std::make_shared<CommandTransaction>("Record Take");
    transaction->add(add);
    transaction->markExecuted();
    if (history.pushExecuted(transaction)) return true;
    add->undo(); // history refused it: take it back out
    return false;
}

} // namespace Audio
} // namespace Aestra
