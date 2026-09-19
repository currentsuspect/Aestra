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
 * against layout (see UISurfaceResolution.h); step 3 wired the first consumer,
 * the mixer inspector's expand/collapse preference, through the app-owned
 * UISurfaceStoreFile below. Floating panels, plugin editors, PluginBrowserPanel's
 * favorites and Source/Core/UIState migrate in step 5.
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
 * `boolPreferences` holds simple preferences keyed by UISurfaceKeys (the mixer
 * inspector since step 3); `listPreferences` is reserved for a later step's
 * migration of PluginBrowserPanel's favorites.
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
    /// Placeholder defaults — per-surface defaults arrive with the step-5 migration.
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
    /// treated as foreign/corrupt and falls back to defaults, so a corrupt or
    /// foreign file never stops a surface from opening.
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

    /// Simple on/off preferences, keyed by UISurfaceKeys. Since step 3:
    /// "panel.mixer.inspectorExpanded".
    std::map<std::string, bool> boolPreferences;

    /// Reserved for a later step's plugin-favorites migration
    /// (e.g. "pluginBrowser.favorites"). Empty and unread today.
    std::map<std::string, std::vector<std::string>> listPreferences;

    /// Full path to the store's file in app data, or empty when there is nowhere
    /// to put it.
    static std::string defaultPath();

    /// Read the store from @p path. Anything unreadable, empty, malformed, or
    /// carrying an unrecognized future schemaVersion yields defaults rather
    /// than an error — a corrupt or foreign store file must not stop any
    /// surface from opening. Takes an explicit path so a test can round-trip
    /// through a temp file without touching the real per-user directory.
    static UISurfaceStore load(const std::string& path);

    /// Write the store to @p path via the shared atomic-write helper.
    /// Returns false, leaving any existing file untouched, if nothing was
    /// written. App code does not call this directly — it goes through
    /// UISurfaceStoreFile, the single owner.
    bool save(const std::string& path) const;
};

/// Every key the app reads or writes. Namespace-scope constants on purpose: a
/// function-local constexpr used inside a lambda fails to compile on MSVC (C3493).
namespace UISurfaceKeys {
inline constexpr char kMixerInspectorExpanded[] = "panel.mixer.inspectorExpanded";
inline constexpr char kPanelMixer[] = "panel.mixer";
inline constexpr char kPanelBrowser[] = "panel.browser";
inline constexpr char kPanelPatternBrowser[] = "panel.pattern-browser";
inline constexpr char kPanelPianoRoll[] = "panel.piano-roll";
inline constexpr char kPanelSequencer[] = "panel.sequencer";
inline constexpr char kPanelHistory[] = "panel.history";
inline constexpr char kPanelTakes[] = "panel.takes";
} // namespace UISurfaceKeys

/**
 * @brief The one in-memory copy of the store, bound to its file.
 *
 * The app owns exactly one (AestraApp, provided through ServiceLocator before any
 * surface is constructed). A single owner is what prevents a lost update: if two
 * surfaces each loaded, modified and saved the whole file on their own, the second
 * save would overwrite the first with a stale copy. Not copyable for the same reason.
 *
 * Saves on every change, never at shutdown — one save path with one meaning.
 */
class UISurfaceStoreFile {
public:
    /// Loads @p path (defaults for a missing or unusable file). Never saves: constructing
    /// the store must not write anything. An empty path means in-memory only.
    explicit UISurfaceStoreFile(std::string path);

    UISurfaceStoreFile(const UISurfaceStoreFile&) = delete;
    UISurfaceStoreFile& operator=(const UISurfaceStoreFile&) = delete;

    const std::string& path() const { return m_path; }

    /// The stored value, or nullopt when the user has never set this preference.
    std::optional<bool> boolPreference(const std::string& key) const;

    /// Sets and saves. A no-op when the key already holds @p value, which is what keeps
    /// applying a loaded preference from echoing straight back into a write. Returns
    /// false only when a write was attempted and failed; the new value stays in memory
    /// either way, and the next real change retries the write.
    bool setBoolPreference(const std::string& key, bool value);

    /// The stored geometry, or nullopt when the user has never moved, resized or
    /// maximized this surface. The caller falls back to defaultSurfacePreference().
    std::optional<UISurfaceGeometry> surfaceGeometry(const std::string& key) const;

    /// Sets and saves. A no-op when the key already holds the same preference
    /// (anchor, size and maximized flag — lastUsedAt is bookkeeping, not a
    /// change), so re-applying a loaded preference never echoes into a write.
    /// Same save/ownership contract as setBoolPreference.
    bool setSurfaceGeometry(const std::string& key, const UISurfaceGeometry& value);

private:
    UISurfaceStore m_store;
    std::string m_path;
};

} // namespace Aestra
