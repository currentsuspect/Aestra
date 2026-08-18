// © 2026 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Pure numeric and geometry values shared by the TrackManagerUI* translation
// units. Split out of TrackManagerUIInternal.h, which also carries context-menu
// helpers and therefore includes NUIContextMenu.h — nothing here needs a widget,
// and a shared numeric primitive should be reachable by a test without dragging
// the UI framework in behind it (AESTRA_CI=ON disables those targets entirely).
//
// Depends on <algorithm> and <cmath> and nothing else. Keep it that way.

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

// Shared by layout, rendering, hit testing, and drag math so visible and
// interactive geometry cannot drift independently.
constexpr float kTimelineHeaderHeight = 38.0f;
constexpr float kTimelineRulerHeight = 28.0f;
constexpr float kTimelineHorizontalScrollbarHeight = 24.0f;
constexpr float kTimelineScrollbarWidth = 15.0f;

// FD-14 §10: nested lane rows (owned lanes of an expanded track) indent this
// far inside the track's primary row, leaving a gutter that reads as nesting.
constexpr float kNestedLaneIndent = 24.0f;

/**
 * @brief Gap between the track-controls column and the first grid pixel.
 *
 * Appeared as a bare `+ 5` in 30-odd `gridStartX` expressions across every
 * TrackManagerUI translation unit (#550). Rendering, hit testing and drag math
 * all have to agree on it, and a literal repeated that many times agrees only
 * by luck.
 */
constexpr float kTimelineGridInsetX = 5.0f;

/**
 * @brief Resolve the first grid pixel in the caller's stated coordinate basis.
 *
 * Timeline code uses component-relative, window-absolute, and ruler-relative
 * coordinates. Requiring the basis origin as an argument keeps that choice
 * visible while centralising the layout formula shared by rendering and input.
 */
inline float timelineGridStartX(float basisOriginX, float controlAreaWidth) {
    return basisOriginX + controlAreaWidth + kTimelineGridInsetX;
}

/** @brief Resolve the last interactive grid pixel in the same stated basis. */
inline float timelineGridEndX(float basisOriginX, float boundsWidth) {
    return basisOriginX + boundsWidth - kTimelineScrollbarWidth;
}

/** @brief Resolve the first track-row pixel in the caller's stated y basis. */
inline float timelineTrackAreaTopY(float basisOriginY) {
    return basisOriginY + kTimelineHeaderHeight + kTimelineHorizontalScrollbarHeight + kTimelineRulerHeight;
}

/** @brief Convert a distance from the grid origin to an unclamped beat. */
inline double timelineGridOffsetToBeat(float offsetFromGridStart, float scrollOffset, float pixelsPerBeat) {
    if (!(pixelsPerBeat > 0.0f) || !std::isfinite(pixelsPerBeat)) {
        return 0.0;
    }
    return (static_cast<double>(offsetFromGridStart) + static_cast<double>(scrollOffset)) /
           static_cast<double>(pixelsPerBeat);
}

/** @brief Convert a beat to a distance from the grid origin. */
inline float timelineBeatToGridOffset(double beat, float scrollOffset, float pixelsPerBeat) {
    if (!(pixelsPerBeat > 0.0f) || !std::isfinite(pixelsPerBeat)) {
        return 0.0f;
    }
    return static_cast<float>(beat * static_cast<double>(pixelsPerBeat)) - scrollOffset;
}

/**
 * @brief Clamp @p value into the interval described by @p a and @p b.
 *
 * The contract, in the order the function decides it:
 *
 * 1. **Bounds that describe no interval** (both non-finite) return `0.0f`. There
 *    is no in-range answer to give, so this stays the defensive sentinel it has
 *    always been. No caller passes such bounds today.
 * 2. **One non-finite bound** collapses to the other, giving a degenerate
 *    interval rather than discarding the call.
 * 3. **Inverted finite bounds are normalised**, not rejected. This is relied
 *    upon: `gridStartX = bounds.x + 236 + 5` and `gridEndX = bounds.x + width - 15`
 *    invert below ~256px of component width, and narrow windows are a supported
 *    layout (#551 audit).
 * 4. **Degenerate bounds** (`lo == hi`) return that bound.
 * 5. **Non-finite values, against a real interval**, resolve where the maths
 *    says they belong: `-inf → lo`, `+inf → hi`, `NaN → lo`. NaN has no ordering,
 *    so its answer is a deterministic choice rather than a derivation; the low
 *    end is picked because every caller's lower bound is a legal value for it.
 * 6. **Finite values** clamp normally.
 *
 * Rule 5 is the one that changed (#551). This previously returned `0.0f` for any
 * non-finite value, which is only in range for 3 of the 12 call sites — the
 * scroll-offset family, whose lower bound happens to be `0.0f`. For the other
 * nine it produced a value outside the caller's own interval, and for the four
 * zoom sites that value is a *divisor* in every x-to-beat conversion.
 *
 * Note this is hardening, not a repair: no path was found that actually feeds a
 * non-finite value in. It removes an invalid-state escape rather than a
 * reproducible defect.
 */
inline float safeClampFloat(float value, float a, float b) {
    // Bounds first — "in range" is meaningless without a range.
    if (!std::isfinite(a) && !std::isfinite(b))
        return 0.0f;
    if (!std::isfinite(a))
        a = b;
    if (!std::isfinite(b))
        b = a;

    const float lo = std::min(a, b);
    const float hi = std::max(a, b);

    // NaN compares false against everything, so it would otherwise fall through
    // both bounds checks and be returned unchanged.
    if (std::isnan(value))
        return lo;

    // These two also absorb -inf and +inf, which order correctly against finite
    // bounds and so need no special case of their own.
    if (value <= lo)
        return lo;
    if (value >= hi)
        return hi;
    return value;
}

} // namespace Audio
} // namespace Aestra
