// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Plugin editors embed into an Aestra panel only when Aestra is an X11 window, and SDL picks native Wayland on a
// Wayland session. These tests pin the pure decision in AestraPlat/include/LinuxVideoDriver.h: a user's own
// SDL_VIDEODRIVER always wins, AESTRA_VIDEO_DRIVER overrides, Wayland + DISPLAY picks x11, anything else leaves
// SDL's default alone.

#include "../../AestraPlat/include/LinuxVideoDriver.h"

#include <iostream>
#include <string>

using namespace Aestra::LinuxVideo;

namespace {
int g_failures = 0;
void expect(bool ok, const std::string& msg) {
    if (!ok) { std::cerr << "[FAIL] " << msg << "\n"; ++g_failures; }
}
using O = std::optional<std::string>;
struct Case { const char* name; LinuxSessionEnv env; O want; };
} // namespace

int main() {
    const Case cases[] = {
        {"x11 session: leave SDL alone", {{}, {}, "x11", {}, ":0"}, std::nullopt},
        {"empty env: leave SDL alone", {}, std::nullopt},
        {"wayland by XDG_SESSION_TYPE + DISPLAY -> x11", {{}, {}, "wayland", {}, ":0"}, O("x11")},
        {"wayland by WAYLAND_DISPLAY + DISPLAY -> x11", {{}, {}, {}, "wayland-1", ":1"}, O("x11")},
        {"wayland without DISPLAY -> none", {{}, {}, "wayland", "wayland-1", {}}, std::nullopt},
        {"wayland with empty DISPLAY -> none", {{}, {}, "wayland", {}, ""}, std::nullopt},
        {"user SDL_VIDEODRIVER wins over wayland+DISPLAY", {"wayland", {}, "wayland", "wayland-1", ":0"}, std::nullopt},
        {"user SDL_VIDEODRIVER wins over aestra override", {"x11", "wayland", "wayland", "w", ":0"}, std::nullopt},
        {"empty SDL_VIDEODRIVER counts as unset", {"", {}, "wayland", {}, ":0"}, O("x11")},
        {"AESTRA_VIDEO_DRIVER=wayland forces native wayland", {{}, "wayland", "wayland", "wayland-1", ":0"}, O("wayland")},
        {"AESTRA_VIDEO_DRIVER applies on an x11 session too", {{}, "x11", "x11", {}, ":0"}, O("x11")},
        {"AESTRA_VIDEO_DRIVER applies without DISPLAY", {{}, "wayland", "wayland", "w", {}}, O("wayland")},
        {"empty AESTRA_VIDEO_DRIVER is ignored", {{}, "", "wayland", {}, ":0"}, O("x11")},
    };
    for (const auto& c : cases) {
        const auto got = chooseLinuxVideoDriver(c.env);
        expect(got.driver == c.want, std::string(c.name) + ": driver mismatch (got " + got.driver.value_or("<none>") + ")");
        expect(!got.reason.empty(), std::string(c.name) + ": reason must say why");
    }
    if (g_failures) { std::cerr << g_failures << " failure(s)\n"; return 1; }
    std::cout << "LinuxVideoDriverTest: all passed\n";
    return 0;
}
