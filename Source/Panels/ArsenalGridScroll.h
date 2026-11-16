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

} // namespace ArsenalGridScroll
} // namespace Audio
} // namespace Aestra
