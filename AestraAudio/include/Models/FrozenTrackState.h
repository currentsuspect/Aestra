// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/ClipInstance.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief What a frozen track needs to be thawed (V83-E11).
 *
 * Freeze renders the track's own output into one clip, mutes the clips it was
 * made from, bypasses the inserts and mutes their parameter automation. All of
 * that is ordinary project state that already saves, EXCEPT the knowledge of
 * which of it freeze did: that is this struct, kept on the mixer channel and
 * saved with it. Thaw reads it, never the undo stack, so a frozen track thaws
 * after a save and reopen. Only what freeze itself switched is listed, so
 * thawing never touches something the user had already muted or bypassed.
 */
struct FrozenTrackState {
    /** One automation curve freeze muted, found again by lane, plugin instance and parameter. */
    struct MutedCurve {
        PlaylistLaneID lane;
        uint64_t deviceInstanceId{0};
        uint32_t paramId{0};
        bool operator==(const MutedCurve& o) const {
            return lane == o.lane && deviceInstanceId == o.deviceInstanceId && paramId == o.paramId;
        }
    };

    ClipInstanceID frozenClip;                 ///< the rendered clip that stands in for the track
    std::vector<ClipInstanceID> mutedClips;    ///< source clips freeze muted (kept in the project)
    std::vector<uint32_t> bypassedSlots;       ///< insert slots freeze bypassed
    std::vector<MutedCurve> mutedCurves;       ///< insert-parameter curves freeze muted

    bool active() const { return frozenClip.isValid(); }
    void clear() { *this = FrozenTrackState{}; }

    /** @brief One line of text for the project file; empty when not frozen. */
    std::string toText() const;
    /** @brief Inverse of toText. An empty or unreadable string gives a not-frozen state. */
    static FrozenTrackState fromText(const std::string& text);
};

} // namespace Audio
} // namespace Aestra
