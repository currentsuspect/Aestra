// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace StripWash {

/**
 * @brief The strip's top tint (owner, 22 Nov: "solid, and only the top"; then "the tint ends at the meter's top"):
 *        a flat solid tint of the track colour (the master's textSecondary) over the strip's header zone (name,
 *        output, inserts, sends, pan, M/S/IN) and nothing below it. It ends 4px (kLift) above where the lane starts, the top
 *        of the meter well / the +6 mark, so the whole lane stays the plain strip. The edge comes from the layout,
 *        never from a percentage. Buses and returns follow the same rule as channels. Pure functions.
 */
inline constexpr float kAlpha = 0.18f;
inline constexpr float kLift = 4.0f; // the tint ends this far above the lane (owner, 22 Nov: "push the colour up by 4px")

/// Height of the tinted top, in px (the lane top less kLift), of a strip @p heightPx tall whose lane starts @p laneTopPx below the strip's top.
inline float tintHeight(float laneTopPx, float heightPx) {
    if (!(heightPx > 0.0f) || !std::isfinite(laneTopPx))
        return 0.0f;
    return std::min(heightPx, std::max(0.0f, std::round(laneTopPx) - kLift));
}

/// The tint's opacity @p y px down the strip: kAlpha down to the lane's top, nothing from it on.
inline float alphaAt(float y, float laneTopPx, float heightPx) {
    if (!std::isfinite(y))
        return 0.0f;
    return (y < tintHeight(laneTopPx, heightPx)) ? kAlpha : 0.0f;
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
