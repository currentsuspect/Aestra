// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace AestraUI::BrowserLibrary {

/// A well-known folder the browser offers without the user having to add it.
struct SystemPlace {
    std::string label; ///< "Home", "Downloads", ...
    std::string path;  ///< Absolute, existing directory.
};

/// Case-insensitive, digit-aware ordering, so "Kick 2" sorts before "Kick 10" and
/// "apple" sits next to "Apple" rather than after every capitalised name.
/// Returns <0, 0 or >0. Names that differ only in case or leading zeros compare
/// equal; callers must tie-break on something stable (the full path).
int naturalCompare(std::string_view a, std::string_view b);

/// Parse the contents of an XDG `user-dirs.dirs` file into a list of
/// (key, path) pairs, e.g. {"DOWNLOAD", "/home/me/Downloads"}. `$HOME` is
/// expanded to @p homeDir. Malformed lines are skipped.
std::vector<std::pair<std::string, std::string>> parseXdgUserDirs(const std::string& content,
                                                                  const std::string& homeDir);

/// The standard places, in display order (Home, Desktop, Downloads, Documents,
/// Music), keeping only those that exist as directories. @p xdgUserDirs is the
/// user-dirs.dirs content (may be empty); it overrides the default folder names
/// on Linux, where "Downloads" can be localised or relocated. A place that
/// resolves to the home directory itself (XDG's way of disabling it) or repeats
/// an earlier place is dropped.
std::vector<SystemPlace> discoverSystemPlaces(const std::string& homeDir, const std::string& xdgUserDirs);

/// Same, with folder locations already resolved as (key, path) pairs using the
/// XDG keys DESKTOP / DOWNLOAD / DOCUMENTS / MUSIC — how Windows' known-folder
/// answers (redirected or localised folders) reach the same rules.
std::vector<SystemPlace> discoverSystemPlacesFrom(const std::string& homeDir,
                                                  const std::vector<std::pair<std::string, std::string>>& folders);

/// discoverSystemPlaces() for the current user, reading the environment and
/// the XDG config directory.
std::vector<SystemPlace> discoverSystemPlaces();

} // namespace AestraUI::BrowserLibrary
