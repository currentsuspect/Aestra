// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ClapInitTwiceTest — preparing a CLAP plugin that is already active deactivates it first.
//
// Reported by the founder: after Bounce in Place a plugin is so crackly it cannot be used. The exporter changes the sample rate
// for the render and back afterwards, and every plugin is prepared again each time: the app sends INIT to a plugin that is
// already active. The helper called activate() a second time without deactivate(), which CLAP forbids; a plugin that is
// activated twice keeps stale internal state and sounds like noise from then on. CLAP's rule: activate only when inactive.

#include "HelperClapModule.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

struct Fake {
    static inline std::vector<std::string> calls;
    static inline bool isActive = false;
    static inline int activeTwice = 0;
    static bool activate(const AestraHelper::ClapAbi::ClapPlugin*, double rate, uint32_t, uint32_t maxFrames) {
        if (isActive) ++activeTwice;
        isActive = true;
        calls.push_back("activate(" + std::to_string(static_cast<int>(rate)) + "," + std::to_string(maxFrames) + ")");
        return true;
    }
    static void deactivate(const AestraHelper::ClapAbi::ClapPlugin*) {
        isActive = false;
        calls.push_back("deactivate");
    }
    static bool start(const AestraHelper::ClapAbi::ClapPlugin*) {
        calls.push_back("start");
        return true;
    }
    static void stop(const AestraHelper::ClapAbi::ClapPlugin*) { calls.push_back("stop"); }
};
} // namespace

int main() {
    using namespace AestraHelper::ClapAbi;
    ClapPlugin plugin{};
    plugin.activate = Fake::activate;
    plugin.deactivate = Fake::deactivate;
    plugin.startProcessing = Fake::start;
    plugin.stopProcessing = Fake::stop;

    ClapModule module;
    module.plugin = &plugin;
    std::string error;

    check(module.initialize(48000.0, 512, error), "the first initialize activates the plugin");
    check(Fake::activeTwice == 0, "and nothing was active before it");

    check(module.initialize(44100.0, 4096, error), "initialize again, as the exporter's sample-rate change does");
    check(Fake::activeTwice == 0, "the plugin was deactivated before it was activated again (CLAP: activate only when inactive)");
    check(module.initialize(48000.0, 512, error) && Fake::activeTwice == 0, "and again when the render ends");
    check(Fake::calls.back() == "start" && module.active, "the plugin ends active and processing, at the last rate and block size");
    check(module.activeSampleRate == 48000.0 && module.maxBlockSize == 512, "and the module remembers them");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "clap init twice passed\n";
    return 0;
}
