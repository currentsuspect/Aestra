// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Layout/NUIAnchoredPlacement.h"

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

} // namespace EditorPlacement
} // namespace AestraUI
