// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// #866: projects saved by builds before #864 carry doubled track records. The old loader failed to restore a track,
// created a fresh one per lane, and every later save wrote both. Two shapes exist in real files:
//
//   A. Each lane owner routes to the master, and a record with the same name and no lanes holds the real mixer
//      channel. Restored as-is, every track loses its routing.
//   B. Each lane owner routes correctly, and a second record points only at lanes that no longer exist.
//
// The repair runs on the parsed records before they are restored. It merges A (the owner takes the channel, the
// lane-less twin goes) and drops B's dead records. It never guesses: a name that matches more than one candidate is
// left alone, and a lane owner on the master with no twin stays on the master (the user may have routed it there).

#include "Models/UnitManager.h" // MASTER_MIXER_CHANNEL_ID, which Track.h uses
#include "Models/Track.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

struct LegacyTrackRepairReport {
    size_t channelsMerged = 0; ///< shape A: lane owners that took their twin's channel
    size_t deadDropped = 0;    ///< shape B: records whose every lane no longer exists
};

/// @param laneExists  callable(const PlaylistLaneID&) -> bool: does the project have this lane?
template <class LaneExists>
LegacyTrackRepairReport repairLegacyTrackRecords(std::vector<Track>& records, LaneExists&& laneExists) {
    LegacyTrackRepairReport report;
    const auto ownsLiveLane = [&](const Track& t) {
        return std::any_of(t.laneIds.begin(), t.laneIds.end(), [&](const PlaylistLaneID& id) { return laneExists(id); });
    };

    // How many lane-less records and how many live lane owners carry each name.
    std::map<std::string, size_t> laneLessByName, ownersByName;
    for (const Track& t : records) {
        if (t.laneIds.empty())
            ++laneLessByName[t.name];
        else if (ownsLiveLane(t))
            ++ownersByName[t.name];
    }

    std::vector<bool> drop(records.size(), false);
    // Shape A: one owner on the master + exactly one lane-less twin of the same name.
    for (size_t i = 0; i < records.size(); ++i) {
        Track& owner = records[i];
        if (owner.laneIds.empty() || !ownsLiveLane(owner) || owner.channelId != MASTER_MIXER_CHANNEL_ID)
            continue;
        if (laneLessByName[owner.name] != 1 || ownersByName[owner.name] != 1)
            continue;
        for (size_t j = 0; j < records.size(); ++j) {
            const Track& twin = records[j];
            if (j == i || drop[j] || !twin.laneIds.empty() || twin.name != owner.name)
                continue;
            if (twin.channelId == MASTER_MIXER_CHANNEL_ID)
                break; // nothing to take
            owner.channelId = twin.channelId;
            drop[j] = true;
            ++report.channelsMerged;
            break;
        }
    }
    // Shape B: a record that names lanes, none of which exists.
    for (size_t i = 0; i < records.size(); ++i) {
        if (!drop[i] && !records[i].laneIds.empty() && !ownsLiveLane(records[i])) {
            drop[i] = true;
            ++report.deadDropped;
        }
    }

    size_t kept = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        if (drop[i])
            continue;
        if (kept != i) // never self-move: a moved-onto-itself string or vector may come back empty
            records[kept] = std::move(records[i]);
        ++kept;
    }
    records.resize(kept);
    return report;
}

} // namespace Audio
} // namespace Aestra
