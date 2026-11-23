// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The transport's pills in one place (21 Nov 2026). The Panels group, the Count-in / Wait / Loop record chips and the
// metronome chip are all the same part as the title bar's view switcher: a sunk bed (Depth::bed, shallow) with a thumb
// inset by the same 3 px when engaged. Their heights are equal, a glyph sits at the exact centre of its cell, and the
// thumb is inset equally on every side. Nothing here draws, so TransportPillGeometryTest checks the numbers headless.

#include "NUITypes.h"

#include <algorithm>
#include <cmath>

namespace Aestra::TransportPill {

constexpr float kHeight = 24.0f; ///< every transport pill
constexpr float kInset = 3.0f;   ///< bed to thumb, the segmented control's pad

/// The raised or lit part inside a pill, inset equally on all four sides.
inline AestraUI::NUIRect thumb(const AestraUI::NUIRect& pill) {
    return {pill.x + kInset, pill.y + kInset, std::max(0.0f, pill.width - 2.0f * kInset), std::max(0.0f, pill.height - 2.0f * kInset)};
}

/// A square glyph of @p size centred in @p cell, on whole pixels (so it never straddles a pixel row).
inline AestraUI::NUIRect centredIcon(const AestraUI::NUIRect& cell, float size) {
    return {std::round(cell.x + (cell.width - size) * 0.5f), std::round(cell.y + (cell.height - size) * 0.5f), size, size};
}

/// A pill's corner radius: a half-height capsule, as the segmented control's.
inline float radius(const AestraUI::NUIRect& pill) { return pill.height * 0.5f; }

/// The icons in the pills draw their glyphs 1 px below the middle of their 15 px box (measured on the real renderer, 21 Nov:
/// the glyph's pixel bounding box centre sat 1 px under the pill's centre for the mixer, step-grid, piano-roll and
/// metronome icons alike). This is the correction applied to those icons, so the glyph pixels are centred, not the box.
constexpr float kGlyphOpticalDy = -1.0f;

/// Where a chip's content (lamp, gap, word or glyph) starts so the same space is left and right of it. The chip's width
/// is estimated before any text is measured (the layout runs without a renderer), so the content is centred in whatever
/// width the chip got rather than pinned to its left edge.
inline float groupStartX(const AestraUI::NUIRect& pill, float groupWidth) {
    return std::round(pill.x + (pill.width - groupWidth) * 0.5f);
}

/// The bed that holds a row of cells (the Panels group): from the first cell's left edge to the last one's right.
inline AestraUI::NUIRect groupBed(const AestraUI::NUIRect& first, const AestraUI::NUIRect& last) {
    return {first.x, first.y, last.right() - first.x, first.height};
}

} // namespace Aestra::TransportPill
