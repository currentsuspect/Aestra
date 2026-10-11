// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Aestra::XdgPortal {

/// The desktop's own file chooser, reached through the XDG Desktop Portal
/// (org.freedesktop.portal.FileChooser over the session D-Bus). GNOME, KDE,
/// Hyprland, Sway and Flatpak sandboxes all answer it with their native dialog,
/// so Aestra needs no knowledge of which picker the desktop uses.
///
/// libdbus-1 is loaded at runtime (dlopen), never linked: a system without it,
/// or without a running portal, simply reports "unavailable".
///
/// Every call BLOCKS until the user answers. Call it from a worker thread.
///
/// Return value:
///   std::nullopt  -> no portal (no libdbus, no session bus, no FileChooser, or
///                    too old for the request). The caller may try another picker.
///   ""            -> the user cancelled. Final: do not open another picker.
///   "/abs/path"   -> the chosen file or folder.

struct Filter {
    std::string name;                  ///< "Audio Files"
    std::vector<std::string> patterns; ///< {"*.wav", "*.flac"}
};

std::optional<std::string> openFile(const std::string& title, const std::vector<Filter>& filters);
std::optional<std::string> saveFile(const std::string& title, const std::vector<Filter>& filters,
                                    const std::string& suggestedPath);
std::optional<std::string> selectFolder(const std::string& title);

/// Parse a Win32-style filter string ("Desc\0*.wav;*.flac\0All\0*.*\0") into
/// portal filters. Exposed for tests.
std::vector<Filter> parseWin32Filter(const std::string& filter);

/// Decode a file:// URI into a filesystem path; empty if it is not a local file URI.
std::string pathFromFileUri(const std::string& uri);

} // namespace Aestra::XdgPortal
