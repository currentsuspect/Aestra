// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace CorrelationLeds {

/**
 * @brief The phase-correlation meter drawn as the mixer's dotted LEDs (MeterLeds.h): a row of 3px segments on a 4px
 *        pitch in a 7px well, -1 (out of phase) at the left, +1 (mono) at the right, a centre marker at 0. The
 *        count is always odd so there is a centre segment. A reading lights the segments between the centre and
 *        itself. Pure functions: the renderer only walks the numbers they give.
 */
inline constexpr float kPitch = 4.0f;
inline constexpr float kSegW = 3.0f;
inline constexpr float kSegH = 3.0f;
inline constexpr float kPad = 2.0f;
inline constexpr float kWellH = kSegH + kPad * 2.0f; // 7
inline constexpr float kWellRadius = 3.0f;

/// How many segments fit a lane @p laneWidthPx wide: an odd number, so the middle one marks zero.
inline int segmentCount(float laneWidthPx) {
    if (!(laneWidthPx >= kSegW))
        return 0;
    int n = static_cast<int>(std::floor((laneWidthPx + (kPitch - kSegW)) / kPitch));
    if (n % 2 == 0)
        --n;
    return std::max(n, 0);
}

/// Index of the centre (zero) segment.
inline int centreIndex(int count) { return count / 2; }

/// Left edge of segment @p index within a lane @p laneWidthPx wide, the row centred in it.
inline float segmentX(int index, int count, float laneWidthPx) {
    const float rowW = static_cast<float>(count) * kPitch - (kPitch - kSegW);
    return (laneWidthPx - rowW) * 0.5f + static_cast<float>(index) * kPitch;
}

struct Range {
    int from; // first lit segment (never the centre)
    int to;   // last lit segment, inclusive; to < from means none
};

/// The segments a reading @p value (-1..+1) lights, counted out from the centre. Not a number reads as zero.
inline Range lit(float value, int count) {
    if (count < 3 || !std::isfinite(value))
        return {1, 0};
    const int half = count / 2;
    const int steps = std::min(half, static_cast<int>(std::floor(std::min(std::fabs(value), 1.0f) * static_cast<float>(half) + 0.5f)));
    if (steps <= 0)
        return {1, 0};
    const int c = centreIndex(count);
    return value > 0.0f ? Range{c + 1, c + steps} : Range{c - steps, c - 1};
}

} // namespace CorrelationLeds
} // namespace AestraUI
