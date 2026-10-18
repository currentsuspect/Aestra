// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// One playhead for the timeline, the piano roll and the Arsenal ruler: a flat,
// filled head (a short bar over a point) and a solid 2 px line. The heads used
// to be three 1.2 px strokes, which read as a hollow, jagged outline; the line
// was a 1 px stroke at 72 %. Both are now whole-pixel fills, so they stay crisp
// at any x and match the flat look of the clips and the Arsenal (v0.8.1).
//
// Header-only and renderer-only, so every widget can share it.

#include "../Graphics/NUIRenderer.h"

#include <cmath>

namespace AestraUI::Playhead {

constexpr float kLineWidth = 2.0f;
constexpr float kHeadWidth = 11.0f;   // odd, so the point lands on the line's centre
constexpr float kHeadBarHeight = 4.0f;
constexpr float kHeadPointHeight = 5.0f;
constexpr float kHeadHeight = kHeadBarHeight + kHeadPointHeight;

/** @brief Left edge of the 2 px line for a playhead at @p x (whole pixels). */
inline float lineLeft(float x) { return std::floor(x) - 1.0f; }

/** @brief The solid line from @p top to @p bottom. */
inline void drawLine(NUIRenderer& renderer, float x, float top, float bottom, const NUIColor& color) {
    if (bottom <= top) return;
    renderer.fillRect(NUIRect(lineLeft(x), std::round(top), kLineWidth, std::round(bottom) - std::round(top)), color);
}

/**
 * @brief The filled head, its point touching @p tipY (the grid's top edge):
 * a kHeadWidth x kHeadBarHeight bar, then a triangle narrowing to the line.
 * @p scale grows it while scrubbing.
 */
inline void drawHead(NUIRenderer& renderer, float x, float tipY, const NUIColor& color, float scale = 1.0f) {
    const float cx = lineLeft(x) + kLineWidth * 0.5f;
    const float halfW = std::round(kHeadWidth * scale) * 0.5f;
    const float pointH = std::round(kHeadPointHeight * scale);
    const float barH = std::round(kHeadBarHeight * scale);
    const float tip = std::round(tipY);
    const float pointTop = tip - pointH;
    renderer.fillRect(NUIRect(cx - halfW, pointTop - barH, halfW * 2.0f, barH), color);
    // The point as a strip from the left edge, through the tip, to the right edge.
    const NUIPoint top[3] = {{cx - halfW, pointTop}, {cx, pointTop}, {cx + halfW, pointTop}};
    const NUIPoint bottom[3] = {{cx - halfW, pointTop}, {cx, tip}, {cx + halfW, pointTop}};
    renderer.fillWaveform(top, bottom, 3, color);
    // The line's own top half-pixel, so head and line meet without a seam.
    renderer.fillRect(NUIRect(lineLeft(x), tip - 2.0f, kLineWidth, 2.0f), color);
}

} // namespace AestraUI::Playhead
