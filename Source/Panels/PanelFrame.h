// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// A panel's frame: one closed rectangle of four whole-pixel lines, drawn AFTER the body and the title bar so nothing is painted over it.
// (21 Nov 2026: WindowPanel stroked the border and then filled the title bar over it, which covered the frame's top part and left an
// L-shaped stub of line at each top corner; the vertical sides only started below the title bar.) Pure geometry, so PanelFrameTest can
// rasterise it and check the lines are complete and no short orphan segment is drawn.

#include "NUIRenderer.h"
#include "NUITypes.h"

#include <algorithm>
#include <cmath>

namespace Aestra::PanelFrame {

struct Lines {
    AestraUI::NUIRect top, bottom, left, right;
};

/// The four sides of the frame around @p r, snapped to whole pixels. The left and right sides run between the top and bottom lines, so the
/// corners belong to the top and bottom lines alone and nothing overlaps.
inline Lines lines(const AestraUI::NUIRect& r) {
    const float x0 = std::round(r.x), y0 = std::round(r.y), x1 = std::round(r.right()), y1 = std::round(r.bottom());
    const float w = std::max(2.0f, x1 - x0), h = std::max(2.0f, y1 - y0);
    return {{x0, y0, w, 1.0f}, {x0, y0 + h - 1.0f, w, 1.0f}, {x0, y0 + 1.0f, 1.0f, h - 2.0f}, {x0 + w - 1.0f, y0 + 1.0f, 1.0f, h - 2.0f}};
}

/// The divider under a title bar: the full width inside the frame, on the pixel row @p titleHeight below the top, so it meets the left and
/// right sides in a T and never stops short of them.
inline AestraUI::NUIRect divider(const AestraUI::NUIRect& r, float titleHeight) {
    const float x0 = std::round(r.x), x1 = std::round(r.right());
    return {x0 + 1.0f, std::round(r.y + titleHeight), std::max(0.0f, x1 - x0 - 2.0f), 1.0f};
}

inline void draw(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& r, const AestraUI::NUIColor& color) {
    const Lines l = lines(r);
    renderer.fillRect(l.top, color);
    renderer.fillRect(l.bottom, color);
    renderer.fillRect(l.left, color);
    renderer.fillRect(l.right, color);
}

} // namespace Aestra::PanelFrame
