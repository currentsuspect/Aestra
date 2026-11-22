// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Layout/NUIAnchoredPlacement.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace AestraUI {
namespace EditorPlacement {

/**
 * @file EditorSurfacePlacement.h
 * @brief V8-C14 step 5b: the pure half of plugin-editor position persistence.
 *
 * FD-23's rule for editors: an editor stores its anchor only — its size is the
 * plugin's intrinsic size, never a user preference — under `editor.<id>`, as a
 * local user preference, never project state. These helpers assemble that
 * preference from measured inputs; the controller resolves it through
 * Layout::resolveAnchoredPlacement and persists it through the app-provided
 * anchor callbacks (the store itself lives app-side; AestraUI must not include
 * Source/). Header-only; links nothing.
 */

/** @brief The `editor.<surface-id>` store key for a plugin-editor surface. */
inline std::string editorSurfaceKey(const std::string& surfaceId) {
    return "editor." + surfaceId;
}

/** @brief The plugin's intrinsic size: live bounds win, then the reported editor size, then 400. */
inline Layout::NUIWindowRect editorIntrinsicBounds(float currentWidth, float currentHeight, int reportedWidth,
                                                   int reportedHeight) {
    const float width = (std::isfinite(currentWidth) && currentWidth > 0.0f)
                            ? currentWidth
                            : ((reportedWidth > 0) ? static_cast<float>(reportedWidth) : 400.0f);
    const float height = (std::isfinite(currentHeight) && currentHeight > 0.0f)
                             ? currentHeight
                             : ((reportedHeight > 0) ? static_cast<float>(reportedHeight) : 400.0f);
    return Layout::NUIWindowRect(0.0f, 0.0f, width, height);
}

/**
 * @brief The preference an editor opens with: the stored anchor when the user
 * placed this surface before, centred otherwise — and always the CURRENT
 * intrinsic size. A stale stored size is never trusted, which is what makes
 * the record anchor-only in practice.
 */
inline Layout::NUIAnchoredRect editorOpenPreference(const std::optional<Layout::NUIAnchoredRect>& stored,
                                                    float intrinsicWidth, float intrinsicHeight) {
    Layout::NUIAnchoredRect preference{0.5, 0.5, static_cast<double>(intrinsicWidth),
                                       static_cast<double>(intrinsicHeight)};
    if (stored.has_value()) {
        preference.anchorX = stored->anchorX;
        preference.anchorY = stored->anchorY;
    }
    return preference;
}

/**
 * @brief The region an editor may be placed in, in the popup layer's local space.
 *
 * The layer covers the whole window, but the app's own title bar (and its window-drag band) owns the strip above
 * @p contentTop: NUICustomWindow never forwards a press there to the content, so a panel placed over it has a
 * header whose close button and drag cannot be clicked. The region starts below that strip.
 */
inline Layout::NUIWindowRect editorPlacementRegion(float layerY, float layerWidth, float layerHeight,
                                                   float contentTop) {
    const float top = std::clamp(contentTop - layerY, 0.0f, layerHeight);
    return Layout::NUIWindowRect(0.0f, top, layerWidth, layerHeight - top);
}


/**
 * @brief Resolve an editor's placement, optionally refusing to shrink it.
 *
 * A native plugin's view is a fixed-size window of the plugin's own making: if the panel were shrunk to the region
 * (what resolveAnchoredPlacement does, the region wins a conflict) the view would hang out of the bottom of its panel
 * and presses on it would count as outside the panel. With @p holdIntrinsicSize the region is grown to hold the
 * preference first, so the size is kept and an axis that does not fit is placed at the region's start (the header and
 * close button stay reachable; the overflow falls off the far edge). Still resolved by resolveAnchoredPlacement.
 */
inline Layout::NUIPlacementResult resolveEditorPlacement(const Layout::NUIAnchoredRect& preference,
                                                         const Layout::NUIWindowRect& region, bool holdIntrinsicSize) {
    Layout::NUIWindowRect usable = region;
    if (holdIntrinsicSize && region.width > 0.0f && region.height > 0.0f) {
        if (std::isfinite(preference.width) && preference.width > region.width)
            usable.width = static_cast<float>(preference.width);
        if (std::isfinite(preference.height) && preference.height > region.height)
            usable.height = static_cast<float>(preference.height);
    }
    return Layout::resolveAnchoredPlacement(preference, Layout::NUIPlacementMode::Anchored, usable,
                                            Layout::NUISizeLimits{0.0, 0.0});
}

/** @brief Whether a press at (@p x, @p y) lies in the rect (x0, y0, w x h): the same half-open test as NUIRect. */
inline bool pressInRect(float x, float y, float x0, float y0, float w, float h) {
    return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
}

/**
 * @brief Whether a press counts as inside a panel for the close-on-outside-click rule: inside its bounds, or inside
 * the plugin view it embeds (the X child window is not clipped to the panel, and the window system delivers its
 * presses to the app with positions the bounds may not contain).
 */
inline bool pressInsidePanel(float x, float y, float bx, float by, float bw, float bh, bool hasView, float vx,
                             float vy, float vw, float vh) {
    return pressInRect(x, y, bx, by, bw, bh) || (hasView && pressInRect(x, y, vx, vy, vw, vh));
}

} // namespace EditorPlacement
} // namespace AestraUI
