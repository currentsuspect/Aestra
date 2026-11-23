// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace MeterLeds {

/**
 * @brief The stepped LED meter's geometry (Design Spec, Mixer, board p-mixer2 locked).
 *
 * Two 6px columns with a 2px gap sit in a well padded 2px all round, so the meter is 18 wide. Each LED is 3px lit
 * and followed by 1px of well, a 4px pitch, stacked up from the lane's bottom. Pure functions: the renderer only
 * walks the numbers they give.
 */
inline constexpr float kColumnW = 6.0f;
inline constexpr float kColumnGap = 2.0f;
inline constexpr float kPad = 2.0f;
inline constexpr float kWellW = kPad + kColumnW + kColumnGap + kColumnW + kPad; // 18
inline constexpr float kWellRadius = 3.0f;
inline constexpr float kLedH = 3.0f;
inline constexpr float kPitch = 4.0f; // 3px on + 1px off

/// How many LEDs fit a lane of @p laneHeightPx. The last one needs no trailing gap, so a lane of 4n-1 holds n.
inline int ledCount(float laneHeightPx) {
    if (!(laneHeightPx >= kLedH))
        return 0;
    return static_cast<int>(std::floor((laneHeightPx + (kPitch - kLedH)) / kPitch));
}

/// How many of @p count LEDs a level (0..1 of the lane) lights: rounded, so it never lights a segment the level
/// has not half reached. Anything not above zero (or not a number) lights none.
inline int litCount(float level01, int count) {
    if (!(level01 > 0.0f) || count <= 0)
        return 0;
    return std::min(count, static_cast<int>(std::floor(std::min(level01, 1.0f) * static_cast<float>(count) + 0.5f)));
}

/// Top edge of LED @p index (0 = bottom), measured from the top of a lane @p laneHeightPx tall. LED 0 sits flush
/// on the lane's bottom edge.
inline float ledTop(int index, float laneHeightPx) {
    return laneHeightPx - kPitch * static_cast<float>(index) - kLedH;
}

/// Where LED @p index sits on the scale, 0..1: its centre as a fraction of the LED stack. This is what
/// MeterZones::meterZone() takes, so a segment's colour never depends on the level being drawn.
inline float ledFraction(int index, int count) {
    return count > 0 ? (static_cast<float>(index) + 0.5f) / static_cast<float>(count) : 0.0f;
}

} // namespace MeterLeds
} // namespace AestraUI
