// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ClapGuiResizeTest — resizing a CLAP editor follows the CLAP negotiation: adjust_size, then set_size with the adjusted
// size, then the window takes that size.
//
// Reported live: RF-16VU's editor did not resize. The plugin keeps a fixed aspect ratio, so for a requested 1000x300 its
// adjust_size says 1282x300. The helper skipped adjust_size, handed the plugin 1000x300 (refused: set_size false), the
// plugin snapped to 1282x300 on its own, and the window stayed 1000x300 — a plugin drawn wider than its window.

#include "Plugin/ClapGuiResize.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace AestraHelper::ClapAbi;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

// A fake editor that keeps a 4:1 aspect, like RF-16VU keeps its own: only an adjusted size is accepted.
struct Fake {
    static inline std::vector<std::pair<uint32_t, uint32_t>> setSizeCalls;
    static bool adjust(const ClapPlugin*, uint32_t* w, uint32_t* h) {
        *w = *h * 4; // keep the height, snap the width to the ratio
        return true;
    }
    static bool adjustRefuses(const ClapPlugin*, uint32_t*, uint32_t*) { return false; }
    static bool set(const ClapPlugin*, uint32_t w, uint32_t h) {
        setSizeCalls.emplace_back(w, h);
        return w == h * 4;
    }
};

ClapPluginGui guiWith(bool (*adjust)(const ClapPlugin*, uint32_t*, uint32_t*)) {
    ClapPluginGui gui{};
    gui.adjustSize = adjust;
    gui.setSize = Fake::set;
    return gui;
}
} // namespace

int main() {
    {
        // The reported case: the request is not on the plugin's aspect.
        Fake::setSizeCalls.clear();
        auto gui = guiWith(Fake::adjust);
        std::pair<int, int> window{0, 0};
        const auto applied = applyClapGuiResize(gui, nullptr, 1000, 300, [&](int w, int h) { window = {w, h}; });
        check(Fake::setSizeCalls.size() == 1 && Fake::setSizeCalls[0] == std::make_pair(1200u, 300u),
              "set_size is given the size the plugin adjusted to, not the raw request");
        check(applied.first == 1200 && applied.second == 300, "the size that was applied is reported");
        check(window == std::make_pair(1200, 300), "the window takes the adjusted size, so plugin and window agree");
    }

    {
        // A request already on the aspect needs no window correction.
        Fake::setSizeCalls.clear();
        auto gui = guiWith(Fake::adjust);
        int windowCalls = 0;
        applyClapGuiResize(gui, nullptr, 1200, 300, [&](int, int) { ++windowCalls; });
        check(Fake::setSizeCalls.size() == 1 && Fake::setSizeCalls[0] == std::make_pair(1200u, 300u),
              "a size already on the aspect goes through unchanged");
        check(windowCalls == 0, "and the window is not touched again");
    }

    {
        // A plugin with no adjust_size is asked for what the user asked for.
        Fake::setSizeCalls.clear();
        auto gui = guiWith(nullptr);
        std::pair<int, int> window{-1, -1};
        applyClapGuiResize(gui, nullptr, 640, 480, [&](int w, int h) { window = {w, h}; });
        check(Fake::setSizeCalls.size() == 1 && Fake::setSizeCalls[0] == std::make_pair(640u, 480u),
              "without adjust_size the request is passed as given");
        check(window == std::make_pair(-1, -1), "and the window is left as it is");
    }

    {
        // adjust_size that says no: fall back to the request rather than a size it did not give.
        Fake::setSizeCalls.clear();
        auto gui = guiWith(Fake::adjustRefuses);
        applyClapGuiResize(gui, nullptr, 640, 480, nullptr);
        check(Fake::setSizeCalls.size() == 1 && Fake::setSizeCalls[0] == std::make_pair(640u, 480u),
              "when adjust_size refuses, the request is used");
    }

    {
        // No set_size at all: nothing to do, and nothing crashes.
        ClapPluginGui gui{};
        const auto applied = applyClapGuiResize(gui, nullptr, 640, 480, nullptr);
        check(applied.first == 0 && applied.second == 0, "a plugin without set_size reports no applied size");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "clap gui resize passed\n";
    return 0;
}
