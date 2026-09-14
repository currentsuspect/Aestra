// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraJSON.h"

#include <optional>
#include <string>

namespace Aestra {

/**
 * @brief Shared, crash-safe JSON-file I/O for per-user config files.
 *
 * Before this existed, MixerUIPreferences, PluginBrowserPanel's favorites and
 * Source/Core/UIState each hand-rolled their own version of these same three
 * steps, and had already drifted: MixerUIPreferences reimplements a Windows
 * HOME fallback IPlatformUtils already provides correctly, and
 * PluginBrowserPanel's favorites path has no Windows fallback at all (falls
 * back to /tmp). This does not replace any of them in place — that migration
 * is a separate step — it exists so the next caller (UISurfaceStore) does not
 * add a fourth divergent copy, and so existing callers have somewhere correct
 * to migrate to later.
 *
 * Deliberately NOT a generic typed-serialization framework: per-field
 * type-checked extraction (has() + isBool()/isNumber()/... before reading)
 * stays inline in each caller, where MixerUIPreferences::load already does it
 * well and where the "what does a wrong type default to" policy genuinely
 * differs per field.
 */

/// Resolve the full path to @p fileName inside this app's per-user config
/// directory, creating the directory if needed. Returns empty if the
/// platform layer is unavailable or the directory cannot be created — the
/// same "nowhere to put it" signal MixerUIPreferences::configDirectory()
/// already uses, so callers can keep their existing empty-path handling.
std::string resolveAppDataFilePath(const std::string& appName, const std::string& fileName);

/// Read and strictly parse the JSON object at @p path.
///
/// Returns std::nullopt for a missing file, an unreadable file, an empty
/// file, or content that fails JSON::parseStrict (including trailing
/// garbage) — deliberately indistinguishable to the caller, matching
/// MixerUIPreferences::load's existing policy that "no usable file" and
/// "corrupt file" both mean "fall back to defaults," never an error.
std::optional<JSON> readJSONStrict(const std::string& path);

/// Write @p root to @p path via the house crash-safe pattern: write to a
/// "<path>.tmp" sibling, flush + fsync that file (Aestra::syncOfstream), then
/// replace the destination in a single step — rename plus a parent-directory
/// fsync on POSIX, MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
/// on Windows, matching ProjectSerializer's writer. Returns false, leaving the
/// original file untouched, if any step fails.
bool writeJSONAtomic(const std::string& path, const JSON& root);

} // namespace Aestra
