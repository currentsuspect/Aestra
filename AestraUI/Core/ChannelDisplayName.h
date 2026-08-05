// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>

namespace AestraUI {

/**
 * @brief Default label for a mixer channel that has no explicit name.
 *
 * Numbered from the *stable* channel id, matching TrackManager's own default.
 * Numbering from a dense list position instead drifts the moment a channel is
 * deleted or an id is restored, so the same channel would be labelled one way
 * in the strip, another in the routing map and a third in the inspector.
 *
 * "Channel", not "Insert": an insert is an effect slot *on* a channel, and
 * using the word for both makes the routing UI unreadable.
 */
inline std::string channelFallbackLabel(uint32_t channelId)
{
    return (channelId == 0) ? "MASTER" : ("Channel " + std::to_string(channelId));
}

/// User-facing name for a channel: its explicit name, or the stable fallback.
inline std::string channelDisplayName(uint32_t channelId, const std::string& name)
{
    return name.empty() ? channelFallbackLabel(channelId) : name;
}

} // namespace AestraUI
