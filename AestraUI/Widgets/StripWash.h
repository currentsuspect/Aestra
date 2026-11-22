// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace StripWash {

/**
 * @brief The Wash top (Design Spec, Mixer, board p-mixer2 locked): the track's colour starts strong behind the name
 *        and fades down the whole strip, so the strip is one body with no seam. Opacity of the colour: 26% at the top,
 *        13% at 60px, 7% at 170px, 3% at 300px and nothing at the strip's foot. Pure functions.
 */
inline constexpr int kStopCount = 5;
inline constexpr float kStopAlpha[kStopCount] = {0.26f, 0.13f, 0.07f, 0.03f, 0.0f};

/// Where stop @p i sits, in px from the strip's top: 0, 60, 170, 300, then the strip's foot. On a strip shorter than
/// 300px the later stops are pulled in (never past the foot), so the wash still ends at nothing.
inline float stopY(int i, float heightPx) {
    constexpr float kAt[kStopCount - 1] = {0.0f, 60.0f, 170.0f, 300.0f};
    if (i >= kStopCount - 1)
        return std::max(0.0f, heightPx);
    return std::min(kAt[i], std::max(0.0f, heightPx));
}

/// The colour's opacity @p y px down a strip @p heightPx tall.
inline float alphaAt(float y, float heightPx) {
    if (!(heightPx > 0.0f) || !std::isfinite(y))
        return 0.0f;
    if (y <= 0.0f)
        return kStopAlpha[0];
    if (y >= heightPx)
        return 0.0f;
    for (int i = 1; i < kStopCount; ++i) {
        const float y0 = stopY(i - 1, heightPx);
        const float y1 = stopY(i, heightPx);
        if (y <= y1 && y1 > y0) {
            const float t = (y - y0) / (y1 - y0);
            return kStopAlpha[i - 1] + t * (kStopAlpha[i] - kStopAlpha[i - 1]);
        }
    }
    return 0.0f;
}

/// How far in from each side row @p row of the wash starts, so the top rows follow the strip's rounded corners
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
