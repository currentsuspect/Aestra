// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraJSON.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief The shared, disk-backed store for "how this user arranges the app".
 *
 * V8-C14 (FD-23). Step 1 defined this schema; step 2 defined how it resolves
 * against layout (see UISurfaceResolution.h). Nothing in the app constructs,
 * loads, saves or reads a UISurfaceStore yet — AestraContent, PluginUIController,
 * MixerUIPreferences, PluginBrowserPanel's favorites and Source/Core/UIState are
 * all untouched. That wiring is steps 3 and 5 of FD-23's binding order.
 *
 * The ownership boundary this generalizes (founder's own words, FD-23): "The
 * project owns what is semantically part of the session. The UI state store
 * owns how this user arranges the application." Panel and plugin-editor
 * geometry is a user preference because it is not a property of the music —
 * the same reasoning the founder gives for keeping it local also means it is
 * never written into a project file.
 *
 * Two record shapes only, matching FD-23's key table exactly — no generic
 * map<string, JSON> bag, and no per-key struct beyond what the table names:
 *
 *   - `surfaces["panel.mixer"]`, `surfaces["panel.browser"]`,
 *     `surfaces["panel.pattern-browser"]`, `surfaces["editor.<plugin-instance-id>"]`
 *     -> UISurfaceGeometry (anchored size + explicit maximize preference).
 *   - `dialogExport` -> UIDialogExportOptions, for the single "dialog.export" key.
 *
 * `boolPreferences`/`listPreferences` are reserved, unread homes for a later
 * step's migration of InspectorCollapseState's `expandedPreference` and
 * PluginBrowserPanel's favorites set into this same store — see the schema
 * design's open-questions section (FD-23 open question "d").
 */

/// A placement preference for one persistent surface (a panel or a plugin editor),
/// relative to its placement region: the Window-space rect the surface may occupy.
/// That region is deliberately not called a viewport — the NUI Layout Contract
/// reserves `Viewport` for the domain→pixel transform.
///
/// Size is in pixels and position re-anchors (ruled 2026-09-14): a surface keeps the
/// size the user gave it when the region changes, and only its position follows.
/// Every field here is the explicit user preference — never the value layout
/// computes when the preference does not fit. That split is the one
/// InspectorCollapseState already proves for expandedPreference vs. forcedCollapsed,
/// and nothing in this file ever clamps a stored value to fit a region.
struct UISurfaceGeometry {
    /// Position as a fraction [0,1] of the region's free space,
    /// `(x − region.x) / (region.width − width)`: 0 flush left/top, 1 flush
    /// right/bottom, 0.5 centred. Measured against free space so a docked surface
    /// stays docked and a centred one stays centred when the region changes.
    double anchorX{0.5};
    double anchorY{0.5};

    /// Pixels. The user's requested size, kept even when it is larger than the
    /// current region; resolution shrinks what is displayed, never what is stored.
    /// Placeholder defaults — per-surface defaults arrive with the step-3 migration.
    double width{480.0};
    double height{320.0};

    /// Explicit user preference. Set only by an actual maximize/restore action or
    /// cleared by dragging the maximized surface; never by a layout pass.
    bool maximized{false};

    /// Unix seconds, updated on every save of this entry. Drives the
    /// `editor.*` pruning pass; `panel.*` entries ignore it (see
    /// pruneStaleEditorEntries).
    int64_t lastUsedAt{0};
};

/// The `dialog.export` record. ExportDialog::show() resets the sample-rate
/// index and output path on every open (verified against
/// Source/Settings/ExportDialog.cpp:37-66) but leaves bit depth, scope and
/// the tail-padding text alone within a session — this struct is what lets
/// all five survive a restart. -1 / empty means "unset", so a stored value
/// can never stomp a legitimate engine-derived default (e.g. sample rate).
struct UIDialogExportOptions {
    int sampleRateIndex{-1};
    int bitDepthIndex{-1};
    int scopeIndex{-1};
    std::string tailInput;
    std::string lastOutputDirectory;
};

struct UISurfaceStore {
    /// Bumped whenever the on-disk shape changes. load() routes anything
    /// older through migrateToCurrent(); anything newer or unparseable is
    /// treated as foreign/corrupt and falls back to defaults, the same
    /// forgiving policy MixerUIPreferences::load already uses.
    ///
    /// Step 2 redefined UISurfaceGeometry within version 1 (anchors instead of
    /// fractional x/y, pixel sizes) without a bump, deliberately: nothing in the
    /// app had ever written this file, so no version-1 file with the old meaning
    /// can exist.
    static constexpr int kCurrentSchemaVersion = 1;
    int schemaVersion{kCurrentSchemaVersion};

    /// Keys: "panel.mixer", "panel.browser", "panel.pattern-browser",
    /// "editor.<plugin-instance-id>". Same record shape for both namespaces
    /// on purpose — FD-23 gives them the same owner and the same maximize
    /// semantics.
    std::map<std::string, UISurfaceGeometry> surfaces;

    /// Key: "dialog.export" today. Absent (std::nullopt) until a later step
    /// actually writes to it; nothing populates this yet.
    std::optional<UIDialogExportOptions> dialogExport;

    /// Reserved for a later step's inspector-collapse migration
    /// (e.g. "panel.mixer.inspectorExpanded"). Empty and unread today.
    std::map<std::string, bool> boolPreferences;

    /// Reserved for a later step's plugin-favorites migration
    /// (e.g. "pluginBrowser.favorites"). Empty and unread today.
    std::map<std::string, std::vector<std::string>> listPreferences;

    /// Full path to the store's file on disk, or empty when there is nowhere
    /// to put it (mirrors MixerUIPreferences::settingsPath's empty-on-failure
    /// convention).
    static std::string defaultPath();

    /// Read the store from @p path. Anything unreadable, empty, malformed, or
    /// carrying an unrecognized future schemaVersion yields defaults rather
    /// than an error — a corrupt or foreign store file must not stop any
    /// surface from opening. Takes an explicit path (rather than always
    /// resolving defaultPath() internally) so a test can round-trip through a
    /// temp file without touching the real per-user config directory.
    static UISurfaceStore load(const std::string& path);

    /// Write the store to @p path via the shared atomic-write helper.
    /// Returns false, leaving any existing file untouched, if nothing was
    /// written.
    bool save(const std::string& path) const;
};

} // namespace Aestra
