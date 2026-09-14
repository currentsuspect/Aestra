// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUILayoutSpace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace AestraUI {
namespace Layout {

/**
 * @file NUIAnchoredPlacement.h
 * @brief The layout half of V8-C14's resolution contract (FD-23 step 2): turning
 * a user's preferred placement into the rect a surface actually gets right now,
 * and turning a user's gesture back into a preferred placement.
 *
 * Two pure functions that never feed each other. resolveAnchoredPlacement()
 * answers "where can it physically go right now?"; captureAnchoredPlacement()
 * records "where did the user ask for it to be?". Passing a resolve result back
 * into capture is exactly the write-back defect AestraContent::onResize has today
 * (an outbound clamp rewriting stored intent), which the NUI Layout Contract §3.2
 * calls lossy: it destroys the information that the preference was ever larger
 * than the space it was shown in.
 *
 * Domain-free, per the contract's §9: nothing here knows the rect is persisted,
 * which panel it belongs to, or what a plugin editor is.
 *
 * Header-only and dependency-free, like every other Layout/ header: AESTRA_CI=ON
 * disables AestraUI_Core, so a test with a link dependency on it is skipped, not
 * failed (FD-19's silent-skip pattern).
 */

/**
 * @brief A preferred placement within a placement region.
 *
 * The placement region is the Window-space rect a surface may occupy (for
 * floating panels: the content area below the transport bar, right of the
 * browser). It is deliberately not a NUISpace::Viewport — the contract reserves
 * Viewport for the domain→pixel transform — and it gets no type of its own,
 * because it is not a transformation boundary, just a Window rect.
 *
 * Size is in pixels and position is a fraction of the region's FREE space,
 * `(x − region.x) / (region.width − width)`: 0 is flush left/top, 1 flush
 * right/bottom, 0.5 centred. Measuring against the free space rather than the
 * region's full extent is what keeps a docked panel docked and a centred one
 * centred when the region changes, and it makes an out-of-region position for a
 * size that fits unrepresentable.
 */
struct NUIAnchoredRect {
    double anchorX = 0.5;
    double anchorY = 0.5;
    double width = 0.0;
    double height = 0.0;
};

/** @brief The smallest size a surface may be given. Belongs to the surface, not the region. */
struct NUISizeLimits {
    double minWidth = 0.0;
    double minHeight = 0.0;
};

enum class NUIPlacementMode : std::uint8_t {
    Anchored,   //!< Size and anchor from the preference.
    FillRegion, //!< The whole region (a maximized surface). The preference's own geometry is ignored, not overwritten.
};

/** @brief Why a resolved rect differs from what was asked for. One bit per reason. */
namespace NUIPlacementAdjust {
constexpr std::uint32_t None = 0;
constexpr std::uint32_t FilledRegion = 1u << 0;     //!< Mode was FillRegion.
constexpr std::uint32_t SanitizedInput = 1u << 1;   //!< A non-finite or out-of-range input was replaced.
constexpr std::uint32_t GrownToMin = 1u << 2;       //!< Requested size was below the surface's minimum.
constexpr std::uint32_t ShrunkToRegion = 1u << 3;   //!< Requested size exceeded the region.
constexpr std::uint32_t MinExceedsRegion = 1u << 4; //!< Even the minimum does not fit; the region won.
constexpr std::uint32_t DegenerateRegion = 1u << 5; //!< Region unusable; the result must not be applied.
} // namespace NUIPlacementAdjust

/**
 * @brief The contract's §10.2 answer for one placement: what was asked for, what
 * was given, and every reason they differ.
 */
struct NUIPlacementTrace {
    NUIWindowRect region;
    NUIAnchoredRect input; //!< The preference after sanitising — what resolution actually used.
    NUISizeLimits limits;
    NUIPlacementMode mode = NUIPlacementMode::Anchored;
    NUIWindowRect resolved;
    std::uint32_t adjustments = NUIPlacementAdjust::None;

    /** Resolved size minus requested size. Non-zero only with a size-changing adjustment. */
    double dw() const { return static_cast<double>(resolved.width) - input.width; }
    double dh() const { return static_cast<double>(resolved.height) - input.height; }
};

struct NUIPlacementResult {
    NUIWindowRect resolved;
    NUIPlacementTrace trace;
    /** False when the region was unusable. The caller keeps its current bounds and must not capture. */
    bool applicable = false;
};

namespace detail {

inline bool regionUsable(const NUIWindowRect& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
           r.width > 0.0f && r.height > 0.0f;
}

inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

inline double usableMin(double v) { return (std::isfinite(v) && v > 0.0) ? v : 0.0; }

} // namespace detail

/**
 * @brief Resolve a preference against the current region. Pure: the preference is
 * a const reference and there are no out-parameters, so resolution cannot rewrite
 * what the user asked for.
 *
 * The order is fixed (contract §7.4): reject an unusable region, sanitise the
 * input, fill the region if asked, then size (grow to the minimum, then shrink to
 * the region — the region wins a conflict, so nothing is ever placed off-screen),
 * then position within the remaining free space. Position needs no clamp: an
 * anchor in [0,1] times non-negative free space is inside the region by
 * construction.
 *
 * All arithmetic is in double with a single cast to float at the end. That is what
 * makes resolve(capture(r)) == r bit-exact for a rect that fits: the double result
 * lands far closer to the original float than half a float step, so the one cast
 * rounds straight back to it (contract §7.5 — no rounding inside layout).
 *
 * Never clips. Clipping is a rendering concern (§3.2).
 */
inline NUIPlacementResult resolveAnchoredPlacement(const NUIAnchoredRect& preference, NUIPlacementMode mode,
                                                   const NUIWindowRect& region, const NUISizeLimits& limits) noexcept {
    NUIPlacementResult result;
    NUIPlacementTrace& trace = result.trace;
    trace.region = region;
    trace.limits = limits;
    trace.mode = mode;
    trace.input = preference;

    if (!detail::regionUsable(region)) {
        trace.adjustments |= NUIPlacementAdjust::DegenerateRegion;
        return result;
    }

    const double rx = region.x;
    const double ry = region.y;
    const double rw = region.width;
    const double rh = region.height;
    const double minW = detail::usableMin(limits.minWidth);
    const double minH = detail::usableMin(limits.minHeight);

    NUIAnchoredRect in = preference;
    bool sanitized = false;
    const auto sanitizeAnchor = [&sanitized](double& a) {
        const double fixed = std::isfinite(a) ? detail::clamp01(a) : 0.5;
        if (fixed != a) {
            a = fixed;
            sanitized = true;
        }
    };
    const auto sanitizeSize = [&sanitized](double& s, double minS) {
        if (!std::isfinite(s) || s <= 0.0) {
            s = minS;
            sanitized = true;
        }
    };
    sanitizeAnchor(in.anchorX);
    sanitizeAnchor(in.anchorY);
    sanitizeSize(in.width, minW);
    sanitizeSize(in.height, minH);
    if (sanitized) {
        trace.adjustments |= NUIPlacementAdjust::SanitizedInput;
    }
    trace.input = in;

    if (mode == NUIPlacementMode::FillRegion) {
        trace.adjustments |= NUIPlacementAdjust::FilledRegion;
        if (minW > rw || minH > rh) {
            trace.adjustments |= NUIPlacementAdjust::MinExceedsRegion;
        }
        result.resolved = region;
        trace.resolved = region;
        result.applicable = true;
        return result;
    }

    const auto fitExtent = [&trace](double requested, double minimum, double available) {
        double size = requested;
        if (size < minimum) {
            size = minimum;
            trace.adjustments |= NUIPlacementAdjust::GrownToMin;
        }
        if (size > available) {
            size = available;
            trace.adjustments |= NUIPlacementAdjust::ShrunkToRegion;
            if (minimum > available) {
                trace.adjustments |= NUIPlacementAdjust::MinExceedsRegion;
            }
        }
        return size;
    };
    const double w = fitExtent(in.width, minW, rw);
    const double h = fitExtent(in.height, minH, rh);

    const double x = rx + (in.anchorX * (rw - w));
    const double y = ry + (in.anchorY * (rh - h));

    result.resolved = NUIWindowRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                    static_cast<float>(h));
    trace.resolved = result.resolved;
    result.applicable = true;
    return result;
}

/**
 * @brief Record a user gesture (drag or resize result) as a new preference.
 *
 * Call only with a rect that came from a gesture — never with a resolve result.
 * The gesture rect is expected to be inbound-clamped already, as the drag and
 * resize handlers do today; clamping inbound is correct (§3.2), clamping outbound
 * is the defect.
 *
 * Returns @p prior unchanged for an unusable region or gesture. When the gesture
 * leaves no free space on an axis, that axis keeps the prior anchor: there is no
 * position to measure, and inventing one would silently move the panel later.
 */
inline NUIAnchoredRect captureAnchoredPlacement(const NUIWindowRect& gestureRect, const NUIWindowRect& region,
                                                const NUIAnchoredRect& prior) noexcept {
    const bool gestureUsable = std::isfinite(gestureRect.x) && std::isfinite(gestureRect.y) &&
                               std::isfinite(gestureRect.width) && std::isfinite(gestureRect.height) &&
                               gestureRect.width > 0.0f && gestureRect.height > 0.0f;
    if (!detail::regionUsable(region) || !gestureUsable) {
        return prior;
    }

    NUIAnchoredRect captured = prior;
    captured.width = gestureRect.width;
    captured.height = gestureRect.height;

    const double slackX = static_cast<double>(region.width) - static_cast<double>(gestureRect.width);
    if (slackX > 0.0) {
        captured.anchorX =
            detail::clamp01((static_cast<double>(gestureRect.x) - static_cast<double>(region.x)) / slackX);
    }
    const double slackY = static_cast<double>(region.height) - static_cast<double>(gestureRect.height);
    if (slackY > 0.0) {
        captured.anchorY =
            detail::clamp01((static_cast<double>(gestureRect.y) - static_cast<double>(region.y)) / slackY);
    }
    return captured;
}

/**
 * @brief The rect a maximized surface restores to when the user drags its title
 * bar: the saved size, placed so the pointer keeps its proportional position
 * along the title bar and its offset from the top edge.
 *
 * The proportion is measured on the MAXIMIZED rect's width — that is where the
 * user grabbed. Measuring it on the restored width would put a different part of
 * the title bar under the pointer.
 *
 * The saved size is returned as-is even when it is wider than the region: this
 * builds a gesture rect for capture, and shrinking it here would let a drag
 * quietly rewrite the size preference. Only the position is fitted (inbound), so
 * the visible part starts inside the region; resolveAnchoredPlacement() alone
 * decides what is displayed.
 */
inline NUIWindowRect restoredRectUnderPointer(const NUIWindowRect& maximizedRect, double restoredWidth,
                                              double restoredHeight, const NUIWindowPoint& pointer,
                                              const NUIWindowRect& region) noexcept {
    const double mx = maximizedRect.x;
    const double my = maximizedRect.y;
    const double mw = maximizedRect.width;
    const double w = (std::isfinite(restoredWidth) && restoredWidth > 0.0) ? restoredWidth : mw;
    const double h = (std::isfinite(restoredHeight) && restoredHeight > 0.0) ? restoredHeight
                                                                             : static_cast<double>(maximizedRect.height);

    const double fraction = (mw > 0.0) ? detail::clamp01((static_cast<double>(pointer.x) - mx) / mw) : 0.5;
    const double grabOffsetY = std::max(0.0, std::min(static_cast<double>(pointer.y) - my, h));

    double x = static_cast<double>(pointer.x) - (fraction * w);
    double y = static_cast<double>(pointer.y) - grabOffsetY;

    if (detail::regionUsable(region)) {
        const double rx = region.x;
        const double ry = region.y;
        const double fitW = std::min(w, static_cast<double>(region.width));
        const double fitH = std::min(h, static_cast<double>(region.height));
        x = std::max(rx, std::min(x, rx + static_cast<double>(region.width) - fitW));
        y = std::max(ry, std::min(y, ry + static_cast<double>(region.height) - fitH));
    }

    return NUIWindowRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
}

} // namespace Layout
} // namespace AestraUI
