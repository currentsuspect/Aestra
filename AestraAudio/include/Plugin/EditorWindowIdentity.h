// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// What a plugin's standalone editor window says about itself to the window manager (X11 / XWayland).
//
// A standalone editor happens when it cannot be nested in the app's window (native Wayland, Windows). With no class and
// no type, a tiling compositor tiles it and crops a fixed-size editor. The class is also what a user matches in a
// compositor rule, so it is its own name and never the main window's.
//
// No X11 here: the helper's window code applies it, and the unit test pins it without a display.

namespace Aestra {
namespace Audio {

struct EditorWindowIdentity {
    const char* resName;    ///< WM_CLASS instance
    const char* resClass;   ///< WM_CLASS class
    const char* windowType; ///< _NET_WM_WINDOW_TYPE_* atom name
};

/// Dialog: compositors float these by default, at the size the window asks for.
constexpr EditorWindowIdentity kStandaloneEditorWindow{"aestra-plugin", "AestraPlugin", "_NET_WM_WINDOW_TYPE_DIALOG"};

/// A plugin that cannot resize its view gets min size == max size; one that can is left free.
constexpr bool editorSizeIsPinned(bool pluginCanResize) { return !pluginCanResize; }

} // namespace Audio
} // namespace Aestra
