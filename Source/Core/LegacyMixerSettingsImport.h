// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "UISurfaceStore.h"

#include "AestraJSONFile.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace Aestra {

/**
 * @file LegacyMixerSettingsImport.h
 * @brief One-time, one-way import of the mixer inspector preference from the file
 * the app used before V8-C14 (`~/.config/aestra/mixer_settings.json`).
 *
 * FD-23 step 3 moved that preference into the shared UISurfaceStore. Its open
 * question (d) allowed "migrate or leave compatible", but ruled that compatibility
 * "must not mean two long-lived paths to the same preference". So this reads the old
 * file only while the store has no value for the preference, copies it in once, and
 * from then on the store alone is consulted. Nothing writes the old file any more;
 * it is left in place as a harmless orphan rather than deleted.
 *
 * Import-only code. When it can be deleted (e.g. after a public release that shipped
 * the migration) is an open founder decision recorded in the vault.
 */

/// `<homeDir>/.config/aestra/mixer_settings.json`, or empty for an empty home.
/// Pure: it never touches the filesystem. The pre-V8-C14 path helper created that
/// directory as a side effect, which a read-only probe must not do.
inline std::string legacyMixerSettingsPath(const std::string& homeDir) {
    if (homeDir.empty()) {
        return {};
    }
    return (std::filesystem::path(homeDir) / ".config" / "aestra" / "mixer_settings.json").string();
}

/// The legacy path for this user: HOME, then USERPROFILE and HOMEDRIVE+HOMEPATH on Windows
/// (HOME alone is normally unset there).
inline std::string legacyMixerSettingsPath() {
    std::string home;
    if (const char* h = std::getenv("HOME"); h != nullptr && *h != '\0') {
        home = h;
    }
#if defined(_WIN32)
    if (home.empty()) {
        if (const char* up = std::getenv("USERPROFILE"); up != nullptr && *up != '\0') {
            home = up;
        }
    }
    if (home.empty()) {
        const char* drive = std::getenv("HOMEDRIVE");
        const char* path = std::getenv("HOMEPATH");
        if (drive != nullptr && *drive != '\0' && path != nullptr && *path != '\0') {
            home = std::string(drive) + path;
        }
    }
#endif
    return legacyMixerSettingsPath(home);
}

/// The legacy file's inspector preference, if it holds a usable one. Missing,
/// unreadable, malformed or wrong-typed all mean "nothing to import": the value must be
/// a real Boolean, because JSON::asBool() quietly returns false for anything else and
/// importing that would collapse the inspector for a user who never chose to.
inline std::optional<bool> readLegacyInspectorExpanded(const std::string& legacyPath) {
    std::optional<JSON> root = readJSONStrict(legacyPath);
    if (!root.has_value() || !root->isObject()) {
        return std::nullopt;
    }
    if (!root->has("inspectorExpanded") || !(*root)["inspectorExpanded"].isBool()) {
        return std::nullopt;
    }
    return (*root)["inspectorExpanded"].asBool();
}

enum class LegacyImportResult {
    AlreadyInStore,  //!< The store already holds the preference; the legacy file was not read.
    Imported,        //!< Copied from the legacy file into the store and saved.
    NothingToImport, //!< No usable legacy value; nothing was written anywhere.
    SaveFailed,      //!< Copied into the in-memory store, but writing the store failed.
};

/// Runs once at startup, before any surface reads the store. The store holding the key
/// is the "already imported" marker, so the legacy file is never consulted again once a
/// value exists — even if the file later says something different.
inline LegacyImportResult importLegacyMixerInspectorPreference(UISurfaceStoreFile& store,
                                                               const std::string& legacyPath) {
    if (store.boolPreference(UISurfaceKeys::kMixerInspectorExpanded).has_value()) {
        return LegacyImportResult::AlreadyInStore;
    }
    const std::optional<bool> legacy = readLegacyInspectorExpanded(legacyPath);
    if (!legacy.has_value()) {
        return LegacyImportResult::NothingToImport;
    }
    return store.setBoolPreference(UISurfaceKeys::kMixerInspectorExpanded, *legacy) ? LegacyImportResult::Imported
                                                                                     : LegacyImportResult::SaveFailed;
}

} // namespace Aestra
