// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ClapReactivateTest — after a CLAP plugin loads state, the helper takes it through stop, deactivate, activate, start.
//
// Reported live: pressing A/B in a plugin's header made the plugin vanish. A/B loads a saved state, and the helper then
// processed the next block in the already-active plugin. The Riverflux plugins (RF-16VU, RF-201) crash inside their own
// engine on that block (SIGSEGV in rf201::Engine::process; the state sets something the engine sized its buffers for,
// such as oversampling). The helper died, the app marked the plugin crashed, and the editor window went with it.
// Reactivating after the load makes the same sequence survive (checked against both real plugins).

#include "Plugin/ClapReactivate.h"

#include <iostream>
#include <string>
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

struct Fake {
    static inline std::vector<std::string> calls;
    static inline bool activateWorks = true;
    static inline bool startWorks = true;
    static bool activate(const ClapPlugin*, double rate, uint32_t minFrames, uint32_t maxFrames) {
        calls.push_back("activate(" + std::to_string(static_cast<int>(rate)) + "," + std::to_string(minFrames) + "," +
                        std::to_string(maxFrames) + ")");
        return activateWorks;
    }
    static void deactivate(const ClapPlugin*) { calls.push_back("deactivate"); }
    static bool start(const ClapPlugin*) {
        calls.push_back("start");
        return startWorks;
    }
    static void stop(const ClapPlugin*) { calls.push_back("stop"); }
};

ClapPlugin makePlugin() {
    ClapPlugin plugin{};
    plugin.activate = Fake::activate;
    plugin.deactivate = Fake::deactivate;
    plugin.startProcessing = Fake::start;
    plugin.stopProcessing = Fake::stop;
    return plugin;
}

void reset() {
    Fake::calls.clear();
    Fake::activateWorks = true;
    Fake::startWorks = true;
}
} // namespace

int main() {
    {
        reset();
        const ClapPlugin plugin = makePlugin();
        // The scope marks go into the same log as the plugin calls, so the order shows what ran inside it.
        const bool ok = reactivateClapPlugin(&plugin, 48000.0, 512,
                                             [&](bool in) { Fake::calls.push_back(in ? "[" : "]"); });
        check(ok, "a plugin that reactivates cleanly reports success");
        const std::vector<std::string> expected{"[", "stop", "]", "deactivate", "activate(48000,1,512)", "[", "start", "]"};
        check(Fake::calls == expected,
              "stopped and started inside the processing scope; deactivated and activated outside it, at the same rate and block");
    }

    {
        reset();
        Fake::activateWorks = false;
        const ClapPlugin plugin = makePlugin();
        const bool ok = reactivateClapPlugin(&plugin, 44100.0, 256, {});
        check(!ok, "a failed activate is reported");
        check(Fake::calls.back() != "start", "and processing is not started on a plugin that did not activate");
    }

    {
        reset();
        Fake::startWorks = false;
        const ClapPlugin plugin = makePlugin();
        const bool ok = reactivateClapPlugin(&plugin, 44100.0, 256, {});
        check(!ok, "a failed start is reported");
        check(Fake::calls.size() >= 3 && Fake::calls[Fake::calls.size() - 2] == "start" &&
                  Fake::calls.back() == "deactivate",
              "a plugin that activated but would not start is deactivated again, never left active (CLAP teardown rule)");
    }

    {
        // A plugin that has no stop_processing still reactivates.
        reset();
        ClapPlugin plugin = makePlugin();
        plugin.stopProcessing = nullptr;
        const bool ok = reactivateClapPlugin(&plugin, 44100.0, 128, {});
        check(ok && Fake::calls.front() == "deactivate", "a plugin without stop_processing still reactivates");
    }

    {
        const bool ok = reactivateClapPlugin(nullptr, 44100.0, 128, {});
        check(!ok, "no plugin is a failure, not a crash");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "clap reactivate passed\n";
    return 0;
}
