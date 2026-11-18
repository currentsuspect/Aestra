// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

// Where the Arsenal's step-grid scroll lands when the wheel moves one notch (scroll mode).
//
// The clamp that keeps the grid inside the loop leaves the last offset between two
// bar lines (a 4-bar loop in a 2.75-bar viewport stops at 1.25 bars). Rounding that
// to "the bar I am on" and stepping one further back skips the bar behind it: from
// 1.25 bars, round(1.25) is 1 and reverse heads to 0, losing the bar at 1. The step
// therefore leaves the line the offset is sitting on in the direction of travel.

namespace Aestra {
namespace Audio {
namespace ArsenalGridScroll {

inline float nextBarOffset(float scrollX, float barWidth, float maxScroll, int direction) {
    if (!(barWidth > 0.0f) || direction == 0) return scrollX;
    // A pixel of float drift still counts as being on the line.
    constexpr float kOnBarEpsilon = 0.01f;
    const float barIndex = scrollX / barWidth;
    const int from = direction > 0 ? static_cast<int>(std::floor(barIndex + kOnBarEpsilon))
                                   : static_cast<int>(std::ceil(barIndex - kOnBarEpsilon));
    const int next = std::max(0, from + direction);
    const float offset = static_cast<float>(next) * barWidth;
    return offset > maxScroll ? maxScroll : offset;
}

// Distance in, bars out. Only whole bars' worth is taken from the accumulator, so the leftover keeps
// counting: a single 40 px notch and two 20 px trackpad deltas of the same total move the same number
// of bars, instead of the split arriving as two.
inline int wheelBars(float& accum, float deltaPx, float barPx) {
    if (!(barPx > 0.0f)) return 0;
    accum += deltaPx;
    const int bars = static_cast<int>(std::floor(std::abs(accum) / barPx));
    if (bars <= 0) return 0;
    const int direction = accum > 0.0f ? 1 : -1;
    accum -= static_cast<float>(direction * bars) * barPx;
    return direction * bars;
}

} // namespace ArsenalGridScroll
} // namespace Audio
} // namespace Aestra
