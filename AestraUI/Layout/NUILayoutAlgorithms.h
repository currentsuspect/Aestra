// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUILayoutSpace.h"
#include <algorithm>
#include <vector>

namespace AestraUI {
namespace Layout {

/**
 * @file NUILayoutAlgorithms.h
 * @brief The first real "algorithms" (V8-X2b ten-part list) — extracted from an
 * actual consumer, not invented ahead of one.
 *
 * These are free functions on NUILocalRect, not NUILayoutNode subclasses. This
 * slice migrates one panel's hand-rolled arithmetic to the layout system without
 * deciding how (or whether) NUIComponent and NUILayoutNode wire together — that
 * is a separate, larger question the spec reconstruction doc leaves for the
 * inspector phase, where a full measure/arrange/dirty-propagation tree is
 * actually needed. A stateless algorithm a caller invokes and applies via its
 * existing setBounds() is still automatic layout replacing hand-positioning —
 * X2b·1 asks for that replacement, not a specific mechanism for it.
 *
 * Both functions here are the exact generalization of WindowPanel::layoutContent()
 * — the plan's own §3 evidence list already names this file among the V8-X2a
 * compatibility bridge's seven consumers. See NUILayoutSpaceTest / the migration
 * commit for the equivalence proof: these produce byte-identical output to the
 * arithmetic they replace, checked against the original's actual numbers.
 *
 * Header-only, dependency-free — same rationale as every other Layout/ header
 * this milestone: AESTRA_CI=ON disables AestraUI_Core, so a test with a link
 * dependency on it is skipped, not failed (FD-19's silent-skip pattern).
 */

/**
 * @brief Arranges fixed-height, individually-sized items in a horizontal row,
 * anchored to the trailing (right) edge of `container`.
 *
 * `spacing` is used consistently as both the gap between adjacent items AND the
 * margin between the first item and the container's trailing edge — the
 * convention a title bar's button row already uses, where the same value reads
 * as "how far the close button sits from the corner" and "how far apart the
 * buttons are." Items are listed nearest-to-the-edge first: `itemWidths[0]` is
 * the item closest to the trailing edge.
 *
 * Each returned rect is vertically centered within `container`'s height. If an
 * item's width plus its predecessors' widths and spacing would push it past the
 * container's leading edge, its x coordinate goes negative — this function does
 * not clip or reflow; a caller that cannot fit its items has a real overflow
 * condition, and X2b's own principle here is that clipping must never be folded
 * into the coordinate calculation, only applied afterward as a rendering concern.
 */
inline std::vector<NUILocalRect> arrangeTrailingRow(
    const NUILocalRect& container,
    const std::vector<float>& itemWidths,
    float itemHeight,
    float spacing) {
    std::vector<NUILocalRect> result;
    result.reserve(itemWidths.size());

    // container.x/container.y anchor the row at the container's actual origin
    // rather than (0,0) — a container with a non-zero origin is a real case
    // (a nested row inside a padded parent, say), and dropping the offset here
    // would silently place items outside the container that was passed in.
    // splitVertical() already does this correctly; this was the one place it
    // was missed.
    const float y = container.y + ((container.height - itemHeight) * 0.5f);
    float cursor = container.x + container.width;
    for (float width : itemWidths) {
        cursor -= spacing;
        cursor -= width;
        result.emplace_back(cursor, y, width, itemHeight);
    }
    return result;
}

/**
 * @brief Splits `container` into a fixed-height leading band and a
 * fill-remaining trailing band, stacked vertically.
 *
 * `leadingHeight` is clamped into `[0, container.height]` so the trailing
 * band's height is never negative — a real invariant of the split, not a
 * caller-specific business rule. A caller that wants a *minimum* trailing
 * height (WindowPanel wants its content area never below 20px, say) enforces
 * that by choosing `container.height` accordingly before calling this, not by
 * asking this function to know about that policy.
 */
struct NUIVerticalSplit {
    NUILocalRect leading;
    NUILocalRect trailing;
};

inline NUIVerticalSplit splitVertical(const NUILocalRect& container, float leadingHeight) {
    const float clampedLeading = std::max(0.0f, std::min(leadingHeight, container.height));
    NUIVerticalSplit split;
    split.leading = NUILocalRect(container.x, container.y, container.width, clampedLeading);
    split.trailing = NUILocalRect(container.x, container.y + clampedLeading, container.width, container.height - clampedLeading);
    return split;
}

} // namespace Layout
} // namespace AestraUI
