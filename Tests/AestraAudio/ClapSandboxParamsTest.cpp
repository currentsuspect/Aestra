// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-H7: a CLAP plugin's parameters through the sandbox, in a build with VST3 too. The helper
// answered PARAMS and GETPARAM from a VST3-only branch first, so every CLAP plugin reported no
// parameters once VST3 was built in (every real build). Also: a blocking scan after a cancel used
// to walk no folder at all. And V82-H7 follow-up: the block's audio goes through shared memory, not hex over the pipe.
#include "CLAPHost.h"
#include "HostTransport.h"
#include "OutOfProcessPluginInstance.h"
#include "PluginScanner.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
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
    check(params.size() == 3, "it lists its three parameters (got " + std::to_string(params.size()) + ")");
    if (params.size() == 3) {
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

        // The engine's tempo and play state reach the plugin through the helper (HostTransport.h). The fixture
        // reports what its last block was told: Host Tempo = bpm / 1000, Host Playing = 1 or 0.
        const auto playFor = [&](const HostTransport& t) {
            for (int b = 0; b < 12; ++b) {
                HostTransportClock::publish(t);
                plugin.process(ins, outs, 2, 2, 256);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };
        playFor(HostTransport::at(48000, 48000.0, 137.0, 4, true, false, 0.0, 4.0));
        check(std::fabs(plugin.getParameter(8) - 0.137f) < 1e-4f,
              "the plugin is told the tempo, 137 (read " + std::to_string(plugin.getParameter(8) * 1000.0f) + ")");
        check(plugin.getParameter(9) > 0.5f, "and that the song is playing");
        playFor(HostTransport::at(96000, 48000.0, 90.0, 3, false, true, 0.0, 6.0));
        check(std::fabs(plugin.getParameter(8) - 0.090f) < 1e-4f, "a new tempo, 90, follows");
        check(plugin.getParameter(9) < 0.5f, "and so does stopping");
        playFor(HostTransport{});
        check(plugin.getParameter(8) < 1e-6f, "with no engine publishing, the plugin gets no transport");
    }
    plugin.deactivate();
    plugin.shutdown();
}
} // namespace

/// The same tempo through the in-process CLAP host, which reads the clock directly.
void runInProcess() {
    std::cout << "In process (CLAPPluginInstance)\n";
    // Made inside the library and used only through IPluginInstance: CLAPHost.h has members that exist only where
    // AESTRA_HAS_CLAP is defined (privately, in AestraAudioCore), so an instance built here would have the wrong layout.
    PluginInfo info;
    info.format = PluginFormat::CLAP;
    info.path = FIXTURE_PATH;
    std::shared_ptr<IPluginInstance> instance = CLAPPluginFactory::createInstance(info);
    check(instance != nullptr, "the in-process host loads the fixture");
    if (!instance)
        return;
    IPluginInstance& plugin = *instance;
    check(plugin.initialize(48000.0, 256), "it initialises");
    plugin.activate();
    float in[256] = {}, out[256] = {}, in2[256] = {}, out2[256] = {};
    const float* ins[2] = {in, in2};
    float* outs[2] = {out, out2};
    HostTransportClock::publish(HostTransport::at(0, 48000.0, 128.5, 4, true, false, 0.0, 4.0));
    plugin.process(ins, outs, 2, 2, 256);
    check(std::fabs(plugin.getParameter(8) - 0.1285f) < 1e-4f, "it is told the tempo, 128.5");
    check(plugin.getParameter(9) > 0.5f, "and that the song is playing");
    HostTransportClock::reset();
    plugin.process(ins, outs, 2, 2, 256);
    check(plugin.getParameter(8) < 1e-6f, "with nothing published it gets no transport");
    plugin.deactivate();
    plugin.shutdown();
}

/// The wire format between app and helper: a round trip keeps every field, and anything malformed is refused.
void runWire() {
    std::cout << "Transport wire format\n";
    const HostTransport t = HostTransport::at(123457, 44100.0, 141.25, 7, true, true, 2.5, 14.0);
    const std::string wire = t.toWire();
    std::vector<std::string> tok;
    size_t at = 0;
    while (at < wire.size()) {
        const size_t start = wire.find_first_not_of(' ', at);
        if (start == std::string::npos)
            break;
        const size_t end = wire.find(' ', start);
        tok.push_back(wire.substr(start, end - start));
        at = end == std::string::npos ? wire.size() : end;
    }
    check(tok.size() == 11 && tok[0] == "T", "it is T and ten fields");
    if (tok.size() == 11) {
        const HostTransport back = HostTransport::fromWire(tok.data() + 1, 10);
        check(back.valid && back.playing && back.looping && back.tempo == t.tempo && back.sampleRate == t.sampleRate &&
                  back.samplePosition == t.samplePosition && back.ppqPosition == t.ppqPosition &&
                  back.barStartPpq == t.barStartPpq && back.timeSigNumerator == 7 && back.loopStartPpq == 2.5 &&
                  back.loopEndPpq == 14.0,
              "every field comes back exactly");
        std::vector<std::string> bad = tok;
        bad[5] = "1,5";
        check(!HostTransport::fromWire(bad.data() + 1, 10).valid, "a mangled number is refused");
        check(!HostTransport::fromWire(tok.data() + 1, 9).valid, "so is a short line");
    }
    check(HostTransport{}.toWire().empty(), "nothing is sent when nothing is published");
    check(std::fabs(t.barStartPpq - 7.0 * std::floor(t.ppqPosition / 7.0)) < 1e-9, "the bar start sits on a bar line");
}

int main() {
    runWire();
    runGain(true);
    runGain(false);
    runInProcess();

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
