// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Pins the pure decisions in AestraPlat/include/WindowMaximizePolicy.h: a window already maximised is never asked
// to maximise again (each request costs ~1 s on X11), a saved-maximised window that is not yet maximised still is.

#include "../../AestraPlat/include/WindowMaximizePolicy.h"

#include <iostream>
#include <string>

using namespace Aestra::WindowMaximize;

namespace {
int g_failures = 0;
void expect(bool ok, const std::string& msg) {
    if (!ok) { std::cerr << "[FAIL] " << msg << "\n"; ++g_failures; }
}
struct MaxCase { const char* name; bool stateMax; bool winMax; bool want; };
} // namespace

int main() {
    const MaxCase maxCases[] = {
        {"saved maximised, window not yet maximised -> call", true, false, true},
        {"saved maximised, born maximised -> skip the second request", true, true, false},
        {"saved normal, window not maximised -> nothing to do", false, false, false},
        {"saved normal, window maximised -> restore path, not maximize", false, true, false},
    };
    for (const auto& c : maxCases)
        expect(shouldCallMaximize(c.stateMax, c.winMax) == c.want, std::string("shouldCallMaximize: ") + c.name);

    struct CreateCase { const char* name; bool start; bool x11; bool wl; bool want; };
    const CreateCase createCases[] = {
        {"saved maximised, plain X11 session -> request", true, true, false, true},
        {"saved maximised, XWayland -> skip (compositor places it)", true, true, true, false},
        {"saved maximised, native Wayland -> request (unchanged)", true, false, true, true},
        {"saved maximised, other driver, no Wayland -> request", true, false, false, true},
        {"saved normal, plain X11 -> no request", false, true, false, false},
        {"saved normal, XWayland -> no request", false, true, true, false},
    };
    for (const auto& c : createCases)
        expect(shouldRequestMaximizeAtCreate(c.start, c.x11, c.wl) == c.want,
               std::string("shouldRequestMaximizeAtCreate: ") + c.name);

    if (g_failures == 0) std::cout << "WindowMaximizePolicyTest: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
