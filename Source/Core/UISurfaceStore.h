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
 * Step 1 of V8-C14 (FD-23): this file defines the schema only. Nothing in the
 * app constructs, loads, saves or reads a UISurfaceStore yet — AestraContent,
 * PluginUIController, MixerUIPreferences, PluginBrowserPanel's favorites and
 * Source/Core/UIState are all untouched. That wiring is later steps (3 and 5
 * of FD-23's binding order).
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
 *     -> UISurfaceGeometry (rect + explicit maximize preference).
 *   - `dialogExport` -> UIDialogExportOptions, for the single "dialog.export" key.
 *
 * `boolPreferences`/`listPreferences` are reserved, unread homes for a later
 * step's migration of InspectorCollapseState's `expandedPreference` and
 * PluginBrowserPanel's favorites set into this same store — see the schema
 * design's open-questions section (FD-23 open question "d").
 */

/// A rectangle-and-maximize preference for one persistent surface (a panel or
/// a plugin editor). `maximized` is the explicit user preference — never the
/// derived/clamped runtime value the V8-X2b layout resolution path (a later
/// step) computes when the stored geometry does not fit the current
/// viewport. Same split InspectorCollapseState already proves for
/// expandedPreference vs. forcedCollapsed; nothing in this file ever clamps.
struct UISurfaceGeometry {
    /// Fractions [0,1] of the owning viewport's content area at last save,
    /// deliberately not absolute pixels: the founder's reasoning for keeping
    /// editor positions local (different monitors, laptop vs. 4K) applies a
    /// second time within one machine, since the reference viewport can
    /// change between sessions without the project ever moving computers.
    /// Absolute pixels would reproduce exactly the "restores off-screen" bug
    /// the founder is guarding against. Clamped defensively on load, since
    /// this is untrusted disk data.
    double x{0.0};
    double y{0.0};
    double width{0.5};
    double height{0.5};

    /// Explicit user preference. Set only by an actual maximize/restore
    /// action; never by a layout pass that merely could not fit the geometry.
    bool maximized{false};

    /// Unix seconds, updated on every save of this entry. Drives the
    /// `editor.*` pruning pass below; `panel.*` entries ignore it (see
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
    static constexpr int kCurrentSchemaVersion = 1;
    int schemaVersion{kCurrentSchemaVersion};

    /// Keys: "panel.mixer", "panel.browser", "panel.pattern-browser",
    /// "editor.<plugin-instance-id>". Same record shape for both namespaces
    /// on purpose — FD-23 gives them the same owner and the same maximize
    /// semantics.
    std::map<std::string, UISurfaceGeometry> surfaces;

    /// Key: "dialog.export" today. Absent (std::nullopt) until a later step
    /// actually writes to it; step 1 never populates this.
    std::optional<UIDialogExportOptions> dialogExport;

    /// Reserved for a later step's inspector-collapse migration
    /// (e.g. "panel.mixer.inspectorExpanded"). Empty and unread in step 1.
    std::map<std::string, bool> boolPreferences;

    /// Reserved for a later step's plugin-favorites migration
    /// (e.g. "pluginBrowser.favorites"). Empty and unread in step 1.
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
