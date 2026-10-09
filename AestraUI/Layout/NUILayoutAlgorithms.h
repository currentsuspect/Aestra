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
 * `spacing` separates adjacent items. `edgeMargin` separates the first item
 * from the container's trailing edge — a second real consumer (the mixer's
 * master strip, flush against the panel edge with zero margin, ahead of an
 * inspector separated from it and from every other item by the same spacing
 * value) needed a different edge convention than the title bar's, where the
 * same value happened to serve as both. Pass `spacing` for `edgeMargin` to
 * get that original behavior back exactly — this is not a new default,
 * because a default here would silently change every existing caller's
 * meaning the day a second one needed something else.
 *
 * Items are listed nearest-to-the-edge first: `itemWidths[0]` is the item
 * closest to the trailing edge.
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
    float spacing,
    float edgeMargin) {
    std::vector<NUILocalRect> result;
    result.reserve(itemWidths.size());

    // container.x/container.y anchor the row at the container's actual origin
    // rather than (0,0) — a container with a non-zero origin is a real case
    // (a nested row inside a padded parent, say), and dropping the offset here
    // would silently place items outside the container that was passed in.
    // splitVertical() already does this correctly; this was the one place it
    // was missed.
    const float y = container.y + ((container.height - itemHeight) * 0.5f);
    float cursor = container.x + container.width - edgeMargin;
    bool first = true;
    for (float width : itemWidths) {
        if (!first) {
            cursor -= spacing;
        }
        first = false;
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

/**
 * @brief The horizontal counterpart of splitVertical(): a fixed-width leading
 * column and a fill-remaining trailing column, side by side.
 *
 * Same clamp, same reason: `leadingWidth` lands in `[0, container.width]` so
 * the trailing column is never negative. Added for the second consumer
 * (TrackManagerUI, V8-X2b phase 4), whose track rows and scrollbar gutter are
 * this split of one band — not a TrackManager-only shape.
 */
struct NUIHorizontalSplit {
    NUILocalRect leading;
    NUILocalRect trailing;
};

inline NUIHorizontalSplit splitHorizontal(const NUILocalRect& container, float leadingWidth) {
    const float clampedLeading = std::max(0.0f, std::min(leadingWidth, container.width));
    NUIHorizontalSplit split;
    split.leading = NUILocalRect(container.x, container.y, clampedLeading, container.height);
    split.trailing = NUILocalRect(container.x + clampedLeading, container.y, container.width - clampedLeading, container.height);
    return split;
}

/**
 * @brief One item's placement within a scrolling row — its rect, in the row's
 * own local space, and whether it currently falls inside `viewport`.
 */
struct NUIScrollItem {
    NUILocalRect rect;
    bool visible = false;
};

/** @brief The axis a stack runs along. */
enum class NUIAxis {
    Horizontal, //!< Items left to right; each takes the viewport's full height.
    Vertical,   //!< Items top to bottom; each takes the viewport's full width.
};

/**
 * @brief arrangeScrollingRow() (below) along either axis.
 *
 * The mixer's strip row (horizontal) and the timeline's track rows (vertical,
 * V8-X2b phase 4) are the same layout: fixed-extent items one step apart,
 * shifted by a scroll offset, each reported visible or not against the
 * viewport. One function with an axis, rather than a second near-copy for
 * the second consumer, is what X2b·5 asks for: the engine absorbs a new
 * consumer without growing a special case for it.
 *
 * Same rules as arrangeScrollingRow(): nothing is clipped or hidden here, and
 * `scrollOffset` is the caller's to bound.
 */
inline std::vector<NUIScrollItem> arrangeScrollingStack(
    const NUILocalRect& viewport,
    NUIAxis axis,
    float itemExtent,
    float spacing,
    std::size_t itemCount,
    float scrollOffset) {
    std::vector<NUIScrollItem> result;
    result.reserve(itemCount);

    const bool horizontal = axis == NUIAxis::Horizontal;
    const float start = horizontal ? viewport.x : viewport.y;
    const float end = horizontal ? viewport.right() : viewport.bottom();
    const float step = itemExtent + spacing;

    for (std::size_t i = 0; i < itemCount; ++i) {
        const float along = start - scrollOffset + (static_cast<float>(i) * step);
        NUIScrollItem item;
        item.rect = horizontal ? NUILocalRect(along, viewport.y, itemExtent, viewport.height)
                               : NUILocalRect(viewport.x, along, viewport.width, itemExtent);
        item.visible = (along + itemExtent) >= start && along <= end;
        result.push_back(item);
    }
    return result;
}

/**
 * @brief Arranges `itemCount` fixed-width, equally-spaced items left to right,
 * offset by `scrollOffset`, and reports which ones currently fall inside
 * `viewport` — the mixer's horizontally-scrolling channel-strip row. The
 * horizontal case of arrangeScrollingStack().
 *
 * This is the layout system's first encounter with overflow: `viewport` is
 * not the same thing as the row's own extent, which is typically far wider
 * than what's currently visible. `visible` is reported rather than acted on —
 * this function does not clip, hide or reorder anything; it answers "is this
 * item inside the window right now," and the caller decides what to do with
 * that (here: skip setVisible(true) and the resulting draw call for anything
 * the caller wouldn't see anyway). Folding the decision itself into this
 * function would blur layout, overflow and rendering into one step, which is
 * exactly the conflation X2b's four-concepts rule refuses.
 *
 * `scrollOffset` is not clamped here — an out-of-range value is a real bound
 * for the CALLER to enforce (typically against `itemCount * (itemWidth +
 * spacing) - spacing`, the row's total content width), because only the
 * caller knows whether the scroll position came from a live drag mid-gesture,
 * where transiently exceeding the bound is expected and will self-correct,
 * or from a stored value that should never have gotten there.
 */
inline std::vector<NUIScrollItem> arrangeScrollingRow(
    const NUILocalRect& viewport,
    float itemWidth,
    float spacing,
    std::size_t itemCount,
    float scrollOffset) {
    return arrangeScrollingStack(viewport, NUIAxis::Horizontal, itemWidth, spacing, itemCount, scrollOffset);
}

/**
 * @brief Total extent of a stack's content along its axis: the bound a
 * caller clamps `scrollOffset` against (content extent minus viewport extent).
 * No trailing spacing after the last item; zero items take no space.
 */
inline float scrollingStackContentExtent(float itemExtent, float spacing, std::size_t itemCount) {
    if (itemCount == 0) {
        return 0.0f;
    }
    return (static_cast<float>(itemCount) * (itemExtent + spacing)) - spacing;
}

/**
 * @brief The inverse of arrangeScrollingStack() for hit testing: which slot
 * the coordinate `along` (in the viewport's own space, on the stack's axis)
 * falls in, or -1 when it lies before the first item.
 *
 * A slot is an item plus the spacing that follows it, so a point in a gap
 * belongs to the item before it — the rule the timeline's hit tests already
 * applied by hand, made one rule. The result is deliberately unbounded above:
 * an index at or past `itemCount` is a real answer ("below the last row"),
 * and whether that means "nothing" or "a new row" is the caller's call, not
 * this function's.
 */
inline int scrollingStackSlotAt(
    const NUILocalRect& viewport,
    NUIAxis axis,
    float itemExtent,
    float spacing,
    float scrollOffset,
    float along) {
    const float step = itemExtent + spacing;
    if (!(step > 0.0f)) {
        return -1;
    }
    const float start = axis == NUIAxis::Horizontal ? viewport.x : viewport.y;
    const float offset = along - start + scrollOffset;
    if (offset < 0.0f) {
        return -1;
    }
    return static_cast<int>(offset / step);
}

} // namespace Layout
} // namespace AestraUI
