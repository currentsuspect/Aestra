// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "LegacyMixerSettingsImport.h"
#include "UISurfaceStore.h"

#include <cstdint>
#include <optional>

namespace Aestra {

/**
 * @file DockedRailWidths.h
 * @brief V8-C14 step 5c: the two docked rail widths (file browser, pattern
 * browser) live in the surface store under "panel.browser" and
 * "panel.pattern-browser" (FD-23 keys). Only the width is user state — the
 * rail's position comes from layout every pass, so the record's anchor and
 * height are never read back.
 *
 * Two operations, matching the step-2 contract's split:
 *   - applyStoredRailWidth() reads a preference and never writes one. Called
 *     from layout while the live pref is still unset; an absent entry leaves
 *     the pref for the computed default, which is never written back.
 *   - importLegacyBrowserRailWidth() runs once at startup. The store holding
 *     the key is the "already imported" marker: the legacy ui_state.json value
 *     is never consulted again afterwards. A legacy value equal to the legacy
 *     default proves the user never resized (only a real drag could persist
 *     anything else — shutdown always saved the clamped live width), so there
 *     is nothing to import and the responsive computed default flows instead.
 */

/// Applies a stored rail width to the live pref. Returns true when it did;
/// an absent entry leaves @p pref untouched for the computed default.
/// The store's own load validation owns the domain, so no clamp here —
/// layout clamps against the live window every pass (it owns "where can
/// this surface physically go right now").
inline bool applyStoredRailWidth(float& pref, const std::optional<UISurfaceGeometry>& stored) noexcept {
    if (!stored.has_value()) {
        return false;
    }
    pref = static_cast<float>(stored->width);
    return true;
}

/// One-time, one-way import of the pre-V8-C14 ui_state.json rail width.
/// See LegacyMixerSettingsImport.h for the shared result contract.
inline LegacyImportResult importLegacyBrowserRailWidth(UISurfaceStoreFile& store, float legacyWidth,
                                                       float legacyDefault, int64_t nowSeconds) {
    if (store.surfaceGeometry(UISurfaceKeys::kPanelBrowser).has_value()) {
        return LegacyImportResult::AlreadyInStore;
    }
    if (legacyWidth == legacyDefault) {
        return LegacyImportResult::NothingToImport;
    }
    UISurfaceGeometry seeded;
    seeded.width = legacyWidth;
    seeded.lastUsedAt = nowSeconds;
    return store.setSurfaceGeometry(UISurfaceKeys::kPanelBrowser, seeded) ? LegacyImportResult::Imported
                                                                           : LegacyImportResult::SaveFailed;
}

} // namespace Aestra
