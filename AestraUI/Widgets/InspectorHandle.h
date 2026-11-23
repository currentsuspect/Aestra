// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>

namespace AestraUI {
namespace InspectorHandle {

/**
 * @brief The inspector's collapse handle (owner, 22 Nov: variant B): a 14x40 round-cornered tab centred on the rail
 *        (the inspector's leading edge expanded, the thin strip collapsed). Only the tab reacts: hover, click and
 *        the drawn tab share one rect, so nothing else along the edge does anything. The hit area is the tab plus
 *        a few px of slop. Pure functions, floats only.
 */
inline constexpr float kTabW = 14.0f;
inline constexpr float kTabH = 40.0f;
inline constexpr float kSlop = 3.0f;

struct Box {
    float x{0}, y{0}, w{0}, h{0};
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

/// The drawn tab, centred on a rail at (railX, railY) of size railW x railH (shorter rails shrink the tab to fit).
inline Box tab(float railX, float railY, float railW, float railH) {
    const float h = std::min(kTabH, std::max(0.0f, railH));
    return Box{railX + (railW - kTabW) * 0.5f, railY + (railH - h) * 0.5f, kTabW, h};
}

/// What reacts to the pointer: the tab with the slop around it.
inline Box hit(float railX, float railY, float railW, float railH) {
    const Box t = tab(railX, railY, railW, railH);
    return Box{t.x - kSlop, t.y - kSlop, t.w + kSlop * 2.0f, t.h + kSlop * 2.0f};
}

} // namespace InspectorHandle
} // namespace AestraUI
