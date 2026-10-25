// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-H7: a CLAP plugin's parameters through the sandbox, in a build with VST3 too. The helper
// answered PARAMS and GETPARAM from a VST3-only branch first, so every CLAP plugin reported no
// parameters once VST3 was built in (every real build). Also: a blocking scan after a cancel used
// to walk no folder at all. And V82-H7 follow-up: the block's audio goes through shared memory, not hex over the pipe.
#include "OutOfProcessPluginInstance.h"
#include "PluginScanner.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef HELPER_PATH
#error "HELPER_PATH is set by CMake"
#endif
#ifndef FIXTURE_PATH
#error "FIXTURE_PATH is set by CMake"
#endif

using namespace Aestra::Audio;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok)
        ++failures;
}

/// Load the fixture, move its gain and play through it. With shm false the helper is asked for nothing shared, so the
/// hex transport (the fallback) is what carries the audio.
void runGain(bool shm) {
    std::cout << (shm ? "Shared-memory transport\n" : "Hex transport (AESTRA_PLUGIN_SHM=0)\n");
#ifdef _WIN32
    _putenv_s("AESTRA_PLUGIN_SHM", shm ? "" : "0");
#else
    if (shm)
        unsetenv("AESTRA_PLUGIN_SHM");
    else
        setenv("AESTRA_PLUGIN_SHM", "0", 1);
#endif
    PluginInfo info;
    info.id = "test.aestra.gain";
    info.name = "Gain Fixture";
    info.format = PluginFormat::CLAP;
    info.type = PluginType::Effect;
    info.path = FIXTURE_PATH;

    OutOfProcessPluginInstance plugin(info, HELPER_PATH);
    check(plugin.load(), "the helper loads the fixture");
    check(plugin.initialize(48000.0, 256), "it initialises");
    check(plugin.usesSharedAudio() == shm, shm ? "its audio goes through the shared block" : "its audio goes as hex");
#ifdef __linux__
    // The region's name is removed once the helper has it mapped, so nothing else can open it and it cannot outlive
    // both processes.
    bool leftover = false;
    const std::string mine = "aestra-ph-" + std::to_string(getpid()) + "-";
    for (const auto& e : std::filesystem::directory_iterator("/dev/shm"))
        leftover = leftover || e.path().filename().string().rfind(mine, 0) == 0;
    check(!leftover, "no shared-memory name is left in /dev/shm");
#endif
    plugin.activate();

    const auto params = plugin.getParameters();
    check(params.size() == 1, "it lists its one parameter (got " + std::to_string(params.size()) + ")");
    if (params.size() == 1) {
        check(params[0].id == 7 && params[0].name == "Gain", "by its id and name");
        check(std::fabs(plugin.getParameter(7) - 0.5f) < 1e-4f, "reading its default, 0.5");

        // CLAP values ride in with process calls, so keep blocks going after the set.
        plugin.setParameter(7, 0.25f);
        float in[256], out[256], in2[256], out2[256];
        for (int i = 0; i < 256; ++i)
            in[i] = in2[i] = 1.0f;
        const float* ins[2] = {in, in2};
        float* outs[2] = {out, out2};
        // Read the last block the helper answered: a late answer plays the block dry (x1).
        float played = -1.0f;
        for (int b = 0; b < 30; ++b) {
            const uint64_t answered = plugin.helperBlockCount();
            plugin.process(ins, outs, 2, 2, 256);
            if (plugin.helperBlockCount() > answered)
                played = out[100];
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(std::fabs(plugin.getParameter(7) - 0.25f) < 1e-4f, "a set value reads back (0.25)");
        check(std::fabs(played - 0.25f) < 1e-4f, "and the plugin plays at it (x0.25)");
    }
    plugin.deactivate();
    plugin.shutdown();
}
} // namespace

int main() {
    runGain(true);
    runGain(false);

    // A blocking scan after a cancel still walks the folders.
    PluginScanner scanner;
    scanner.clearSearchPaths();
    scanner.addSearchPath(std::filesystem::path(FIXTURE_PATH).parent_path());
    scanner.setFirstLoadWarningCallback([](const std::filesystem::path&, const std::string&) { return true; });
    scanner.cancelAndJoin();
    bool found = false;
    const auto scanned = scanner.scanBlocking();
    for (const auto& p : scanned)
        found = found || p.id == "test.aestra.gain";
    if (!found) {
        for (const auto& p : scanned) {
            if (p.format != PluginFormat::Internal)
                std::cout << "    scanned: " << p.id << " (" << p.path.string() << ")\n";
        }
    }
    check(found, "a blocking scan after a cancel finds the fixture");

    std::cout << (failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
