// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace StripWash {

/**
 * @brief The strip's top tint (owner, 22 Nov: "solid, and only the top half so it's not reading as busy"): a flat solid
 *        tint of the track colour (the master's textSecondary) over the top half of the strip, a hard edge at 50% of
 *        its height, nothing below: the lower half is the plain strip. Buses and returns follow the same rule.
 *        Replaces the board's fading Wash (26/13/7/3%). Pure functions.
 */
inline constexpr float kTopFraction = 0.5f;
inline constexpr float kAlpha = 0.18f;

/// Height of the tinted top, in px, of a strip @p heightPx tall.
inline float topHeight(float heightPx) { return heightPx > 0.0f ? std::round(heightPx * kTopFraction) : 0.0f; }

/// The tint's opacity @p y px down a strip @p heightPx tall: kAlpha down to the edge, nothing from it on.
inline float alphaAt(float y, float heightPx) {
    if (!(heightPx > 0.0f) || !std::isfinite(y))
        return 0.0f;
    return (y < topHeight(heightPx)) ? kAlpha : 0.0f;
}

/// How far in from each side row @p row of the tint starts, so the top rows follow the strip's rounded corners
/// (circle edge at the row's centre, to whole pixels). Zero past the radius.
inline float cornerInset(int row, float radius) {
    const float centre = static_cast<float>(row) + 0.5f;
    if (!(centre < radius))
        return 0.0f;
    const float dy = radius - centre;
    return std::floor(radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy)));
}

} // namespace StripWash
} // namespace AestraUI
