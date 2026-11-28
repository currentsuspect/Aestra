// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// EditorWindowIdentityTest — a plugin's standalone editor window tells the window manager what it is.
//
// Under native Wayland the plugin editor cannot be nested in Aestra's window, so it is a top-level XWayland window of the
// sandbox helper. It was made with no WM_CLASS and no window type, so a tiling compositor (Hyprland) tiled it, squeezed a
// fixed-size editor into half a screen and cropped it, and the user had nothing to write a rule against (the class was
// empty). Giving it a class and a dialog type lets compositors float it at its real size and lets users target it.

#include "Plugin/EditorWindowIdentity.h"

#include <iostream>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}
} // namespace

int main() {
    const std::string cls = kStandaloneEditorWindow.resClass;
    const std::string name = kStandaloneEditorWindow.resName;
    const std::string type = kStandaloneEditorWindow.windowType;

    check(!cls.empty() && !name.empty(), "the editor window has a WM class and instance name");
    check(cls != "Aestra", "its class is not the main window's, so a rule for plugins cannot catch the app");
    check(type.rfind("_NET_WM_WINDOW_TYPE_", 0) == 0, "its window type is an EWMH type atom");
    check(type == "_NET_WM_WINDOW_TYPE_DIALOG", "it is a dialog, which compositors float by default");

    check(editorSizeIsPinned(false), "a fixed-size editor is pinned to its size");
    check(!editorSizeIsPinned(true), "a resizable editor is left free to resize");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "editor window identity passed\n";
    return 0;
}
