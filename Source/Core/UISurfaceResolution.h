// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "UISurfaceStore.h"

#include "../../AestraUI/Layout/NUIAnchoredPlacement.h"

#include <cstdint>

namespace Aestra {

/**
 * @file UISurfaceResolution.h
 * @brief The store half of V8-C14's resolution contract (FD-23 step 2): the only
 * sanctioned ways a UISurfaceGeometry becomes on-screen geometry, and the only
 * sanctioned ways a new UISurfaceGeometry is produced.
 *
 * FD-23's boundary, in the founder's words: layout answers "where can this surface
 * physically go right now?"; the store answers "where did the user ask for it to
 * be?" — two questions never answered by the same code. So:
 *
 *   - resolveSurfacePlacement() reads a preference and never writes one.
 *   - capture*() produce a new preference from a user action and never read a
 *     resolve result.
 *
 * The rules the step-4 CI guard enforces are written out in the vault contract doc
 * (13 Agent Work/2026-09-14 - V8-C14 Step 2 Resolution Contract.md): every
 * setBounds of a persistent surface comes from a resolve result; a preference is
 * assigned only from capture*() or defaultSurfacePreference(); capture*() is called
 * only from gesture handlers.
 *
 * Nothing in the app calls these yet — floating panels (AestraContent) and plugin
 * editors migrate in step 5. Header-only; includes only header-only dependencies.
 */

/**
 * @brief The on-screen rect for a stored preference in the current placement region.
 *
 * A maximized preference fills the region; its saved anchor and size are ignored
 * while maximized, not overwritten, so restoring returns exactly the rect it had
 * before — however long it stayed maximized and however the region changed.
 */
inline AestraUI::Layout::NUIPlacementResult resolveSurfacePlacement(
    const UISurfaceGeometry& preference, const AestraUI::Layout::NUIWindowRect& region,
    const AestraUI::Layout::NUISizeLimits& limits) noexcept {
    using namespace AestraUI::Layout;
    const NUIAnchoredRect anchored{preference.anchorX, preference.anchorY, preference.width, preference.height};
    return resolveAnchoredPlacement(anchored,
                                    preference.maximized ? NUIPlacementMode::FillRegion : NUIPlacementMode::Anchored,
                                    region, limits);
}

/**
 * @brief A new preference from a drag or resize gesture.
 *
 * Also the path for dragging a maximized surface's title bar (ruled 2026-09-14:
 * it restores and follows the pointer, like a maximized OS window): the handler
 * builds @p gestureRect with restoredRectUnderPointer(), and capturing it clears
 * `maximized`. Resize never reaches here while maximized — WindowPanel blocks it.
 *
 * Returns @p prior unchanged for an unusable region, per the contract's rule that
 * a degenerate region is never captured against.
 */
[[nodiscard]] inline UISurfaceGeometry captureSurfaceGesture(const UISurfaceGeometry& prior,
                                                             const AestraUI::Layout::NUIWindowRect& gestureRect,
                                                             const AestraUI::Layout::NUIWindowRect& region,
                                                             int64_t nowSeconds) noexcept {
    using namespace AestraUI::Layout;
    if (!detail::regionUsable(region)) {
        return prior;
    }
    const NUIAnchoredRect captured =
        captureAnchoredPlacement(gestureRect, region, NUIAnchoredRect{prior.anchorX, prior.anchorY, prior.width,
                                                                      prior.height});
    UISurfaceGeometry next = prior;
    next.anchorX = captured.anchorX;
    next.anchorY = captured.anchorY;
    next.width = captured.width;
    next.height = captured.height;
    next.maximized = false;
    next.lastUsedAt = nowSeconds;
    return next;
}

/**
 * @brief A new preference from a maximize or restore action. Changes only
 * `maximized` and `lastUsedAt` — never the saved anchor or size, which is what
 * lets a restore return to the pre-maximize rect.
 */
[[nodiscard]] inline UISurfaceGeometry captureMaximizeToggle(const UISurfaceGeometry& prior, bool maximized,
                                                             int64_t nowSeconds) noexcept {
    UISurfaceGeometry next = prior;
    next.maximized = maximized;
    next.lastUsedAt = nowSeconds;
    return next;
}

/**
 * @brief The preference a surface uses when the store has no entry for it.
 * Resolved like any other preference, and never written to the store until the
 * user actually moves, resizes or maximizes the surface.
 */
[[nodiscard]] inline UISurfaceGeometry defaultSurfacePreference(double width, double height, double anchorX,
                                                                double anchorY) noexcept {
    UISurfaceGeometry preference;
    preference.width = width;
    preference.height = height;
    preference.anchorX = anchorX;
    preference.anchorY = anchorY;
    return preference;
}

} // namespace Aestra
