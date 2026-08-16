// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/ClipInstance.h"
#include "Core/MixerChannel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief The user's recording and arrangement identity (FD-14).
 *
 * A Track owns its lanes and references its mixer Channel as the routing
 * destination. Ownership is by STABLE ID only — never by lane index, track
 * index, or channel index (the positional pairings FD-14 forbids).
 *
 *   Track ──── owns ────> Lane(s)
 *   Track ──── routes ──> Channel
 *   Lane  ──── belongs ──> Track
 */
struct Track {
    /** Stable track identity (serialized; never positional). */
    uint64_t trackId{0};
    /** Mixer routing destination (MASTER_MIXER_CHANNEL_ID = master). */
    uint32_t channelId{MASTER_MIXER_CHANNEL_ID};
    /** User-facing name. */
    std::string name;
    /** Recording arm state — the authoritative recording arm (FD-14 #6). */
    bool armed{false};
    /** Primary/visible lane when the track is collapsed. */
    PlaylistLaneID activeLaneId;
    /** Owned lanes in display order; track-local numbering by position. */
    std::vector<PlaylistLaneID> laneIds;

    /** Track-local lane number (1-based position in laneIds), or 0 when the
     * lane is not owned by this track. */
    int laneNumber(PlaylistLaneID laneId) const {
        for (size_t i = 0; i < laneIds.size(); ++i) {
            if (laneIds[i] == laneId) {
                return static_cast<int>(i) + 1;
            }
        }
        return 0;
    }
};

} // namespace Audio
} // namespace Aestra
