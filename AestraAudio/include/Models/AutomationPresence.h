// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AutomationCurve.h"
#include "PlaylistModel.h"

#include <cstdint>
#include <unordered_map>

namespace Aestra {
namespace Audio {

/**
 * @brief Read-only automation presence summary for one mixer channel (FD-16).
 *
 * Derived data only — counts curves addressing the channel by stable
 * mixerChannelId and unions their target classes into a bitmask:
 * bit0 = Volume, bit1 = Pan, bit2 = Custom/unrecognized (targets 2–254 are
 * preserved values the renderer skips; they surface here under the generic
 * bit so presence never lies by omission).
 */
struct AutomationPresence {
    int curveCount{0};
    uint32_t targetMask{0};
};

/// Bit for a single target class. Custom bucket absorbs unrecognized ids.
inline uint32_t automationTargetBit(AutomationTarget target) noexcept {
    switch (target) {
        case AutomationTarget::Volume: return 1u << 0;
        case AutomationTarget::Pan:    return 1u << 1;
        default:                       return 1u << 2;
    }
}

using AutomationPresenceMap = std::unordered_map<uint32_t, AutomationPresence>;

/**
 * @brief Aggregate per-channel automation presence across all playlist lanes.
 *
 * Curves address channels directly via mixerChannelId (Automation Identity
 * Contract). Zero means unassigned — never master, whose MixerChannel carries
 * its own non-zero stable id — so unassigned curves match no channel and are
 * skipped rather than misattributed.
 */
inline AutomationPresenceMap computeAutomationPresence(
    const std::vector<PlaylistLane>& lanes) {
    AutomationPresenceMap presence;
    for (const auto& lane : lanes) {
        for (const auto& curve : lane.automationCurves) {
            const uint32_t channelId = curve.mixerChannelId;
            if (channelId == 0) {
                continue;
            }
            auto& entry = presence[channelId];
            ++entry.curveCount;
            entry.targetMask |= automationTargetBit(curve.target);
        }
    }
    return presence;
}

} // namespace Audio
} // namespace Aestra
