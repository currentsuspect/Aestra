// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraJSON.h"

#include <optional>
#include <string>

namespace Aestra {

/**
 * @brief Shared, crash-safe JSON-file I/O for per-user config files.
 *
 * Before this existed, each per-user preference file hand-rolled its own path
 * resolution, parsing and writing, and the copies had drifted: one reimplemented a
 * Windows HOME fallback IPlatformUtils already provides, and PluginBrowserPanel's
 * favorites path has no Windows fallback at all (it falls back to /tmp). This exists
 * so UISurfaceStore — and every preference migrated onto it — shares one correct copy.
 *
 * Deliberately NOT a generic typed-serialization framework: per-field type-checked
 * extraction (has() + isBool()/isNumber()/... before reading) stays inline in each
 * caller, because the "what does a wrong type default to" policy genuinely differs
 * per field.
 */

/// Resolve the full path to @p fileName inside this app's per-user data
/// directory, creating the directory if needed. Returns empty if the platform
/// layer is unavailable or the directory cannot be created, so callers can treat
/// "nowhere to put it" as a plain empty path.
std::string resolveAppDataFilePath(const std::string& appName, const std::string& fileName);

/// Read and strictly parse the JSON object at @p path.
///
/// Returns std::nullopt for a missing file, an unreadable file, an empty
/// file, or content that fails JSON::parseStrict (including trailing
/// garbage) — deliberately indistinguishable to the caller: "no usable file"
/// and "corrupt file" both mean "fall back to defaults," never an error.
std::optional<JSON> readJSONStrict(const std::string& path);

/// Write @p root to @p path via the house crash-safe pattern: write to a
/// "<path>.tmp" sibling, flush + fsync that file (Aestra::syncOfstream), then
/// replace the destination in a single step — rename plus a parent-directory
/// fsync on POSIX, MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
/// on Windows, matching ProjectSerializer's writer. Returns false, leaving the
/// original file untouched, if any step fails.
bool writeJSONAtomic(const std::string& path, const JSON& root);

} // namespace Aestra
