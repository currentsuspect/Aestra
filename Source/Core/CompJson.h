// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// A track's comp as project JSON (SPEC-022): which lane is the master and which take plays where. The master lane's
// clips are ordinary lane clips and ride in the lane list; only the comp itself is written here. Kept out of
// ProjectSerializer.cpp, which is size-pinned.

#include "AestraJSON.h"
#include "AestraUUID.h"
#include "Models/Track.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Aestra {
namespace Audio {

/// Null JSON when the track has no comp worth saving.
inline JSON compToJson(const Track& track) {
    if (!track.compLaneId.isValid() || track.comp.empty()) return JSON();
    JSON comp = JSON::object();
    comp.set("laneId", JSON(track.compLaneId.toString()));
    JSON ranges = JSON::array();
    for (const CompRange& r : track.comp.ranges()) {
        JSON range = JSON::object();
        range.set("start", JSON(r.startBeat));
        range.set("end", JSON(r.endBeat));
        range.set("lane", JSON(r.laneId.toString()));
        ranges.push(range);
    }
    comp.set("ranges", ranges);
    return comp;
}

/// Read a comp onto `track`. A comp whose master lane the file lacks is dropped, never guessed at; so is any range
/// that is malformed or names a lane the file lacks.
inline void compFromJson(const JSON& trackJson, Track& track, const std::function<bool(const PlaylistLaneID&)>& laneExists) {
    constexpr size_t kMaxRanges = 4096;
    if (!trackJson.has("comp") || !trackJson["comp"].isObject()) return;
    const JSON& comp = trackJson["comp"];
    AestraUUID master;
    if (!comp.has("laneId") || !comp["laneId"].isString() || !AestraUUID::tryParse(comp["laneId"].asString(), master)) return;
    if (!laneExists(PlaylistLaneID(master)) || !comp.has("ranges") || !comp["ranges"].isArray()) return;
    track.compLaneId = PlaylistLaneID(master);
    const JSON& ranges = comp["ranges"];
    for (size_t i = 0; i < ranges.size() && i < kMaxRanges; ++i) {
        const JSON& range = ranges[i];
        AestraUUID take;
        if (!range.isObject() || !range.has("lane") || !range["lane"].isString() || !AestraUUID::tryParse(range["lane"].asString(), take)) continue;
        if (!range.has("start") || !range["start"].isNumber() || !range.has("end") || !range["end"].isNumber()) continue;
        const double a = range["start"].asNumber(), b = range["end"].asNumber();
        if (!std::isfinite(a) || !std::isfinite(b) || a < 0.0 || b > 1.0e9 || !laneExists(PlaylistLaneID(take))) continue;
        track.comp.set(a, b, PlaylistLaneID(take));
    }
    if (track.comp.empty()) track.compLaneId = PlaylistLaneID{};
}

} // namespace Audio
} // namespace Aestra
