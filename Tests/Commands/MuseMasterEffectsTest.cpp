// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseMasterEffectsTest — the effect verbs reach the master channel (track -1),
// so an agent can master a song: insert, read, set, bypass and remove effects
// on the master, proven in audio — a limiter on the master holds a loud
// render under its ceiling, and bypassing it lets the peak through again.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>

using Aestra::JSON;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& label) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << label << "\n";
    if (!cond) ++g_failures;
}

JSON call(MuseService& s, const std::string& verb, JSON args) {
    JSON r = JSON::object();
    r.set("id", JSON(1.0));
    r.set("verb", JSON(verb));
    r.set("args", args);
    return JSON::parse(s.handleRequest(r.toString()));
}
std::string statusOf(JSON r) { return r.has("status") ? r["status"].asString() : "<missing>"; }

std::string writeLoudWav(const std::filesystem::path& dir) {
    const std::string path = (dir / "loud.wav").string();
    const uint32_t sr = 48000, frames = 24000;
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + frames * 2); f.write("WAVE", 4); f.write("fmt ", 4);
    u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    f.write("data", 4); u32(frames * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const auto s = static_cast<int16_t>(32000.0 * std::sin(2.0 * 3.14159265358979 * 110.0 * i / sr));
        f.write(reinterpret_cast<const char*>(&s), 2);
    }
    return path;
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) return 1;
    auto tm = std::make_shared<TrackManager>();
    tm->getUnitManager().setPatternManager(&tm->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(tm, engine);
    if (!engine.initialize()) return 1;
    CommandRegistry::initialize();
    MuseService service(tm.get(), &engine);

    // One directory per run: two ctest invocations at once must not share, overwrite
    // or delete each other's renders.
    const auto runDir = std::filesystem::temp_directory_path() /
                        ("muse_master_" + std::to_string(std::random_device{}()) + "_" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(runDir);
    const std::string wav = writeLoudWav(runDir);
    const std::string out = (runDir / "out.wav").string();
    JSON a = JSON::object();
    a.set("name", JSON("Loud"));
    call(service, "add_unit", a);
    a = JSON::object();
    a.set("unit", JSON(1.0));
    a.set("file", JSON(wav));
    check(statusOf(call(service, "load_sample", a)) == "ok", "load a loud tone");
    a = JSON::object();
    a.set("unit", JSON(1.0));
    a.set("value", JSON(2.0));
    call(service, "set_unit_gain", a);
    a = JSON::object();
    a.set("pattern", JSON(1.0));
    a.set("unit", JSON(1.0));
    a.set("pitch", JSON(60.0));
    a.set("steps", JSON("x---x---x---x---"));
    a.set("velocity", JSON(1.0));
    call(service, "set_steps", a);
    a = JSON::object();
    a.set("pattern", JSON(1.0));
    a.set("track", JSON(0.0));
    a.set("start", JSON(0.0));
    check(statusOf(call(service, "arrange_pattern", a)) == "ok", "arrange it");

    // Drive the master with an EQ's output gain, then limit.
    a = JSON::object();
    a.set("track", JSON(-1.0));
    a.set("effect", JSON("com.Aestrastudios.eq"));
    JSON r = call(service, "add_effect", a);
    check(statusOf(r) == "ok", "insert an EQ on the master (track -1)");
    a = JSON::object();
    a.set("track", JSON(-1.0));
    a.set("slot", JSON(0.0));
    a.set("param", JSON("Output Gain"));
    a.set("value", JSON(1.0));
    check(statusOf(call(service, "set_effect_param", a)) == "ok", "set a master effect parameter");
    a = JSON::object();
    a.set("track", JSON(-1.0));
    a.set("effect", JSON("com.Aestrastudios.limiter"));
    check(statusOf(call(service, "add_effect", a)) == "ok", "insert the limiter on the master");
    a = JSON::object();
    a.set("track", JSON(-1.0));
    a.set("slot", JSON(1.0));
    a.set("param", JSON("Ceiling"));
    a.set("value", JSON(0.6));
    check(statusOf(call(service, "set_effect_param", a)) == "ok", "set the limiter ceiling");

    a = JSON::object();
    a.set("track", JSON(-1.0));
    r = call(service, "get_effects", a);
    check(statusOf(r) == "ok" && r["result"]["effects"].size() == 2, "get_effects reads the master chain");
    std::string ceilingDisplay;
    for (size_t i = 0; r["result"]["effects"].size() == 2 && i < r["result"]["effects"][1]["params"].size(); ++i) {
        if (r["result"]["effects"][1]["params"][i]["name"].asString() == "Ceiling")
            ceilingDisplay = r["result"]["effects"][1]["params"][i]["display"].asString();
    }
    const double ceilingDb = ceilingDisplay.empty() ? 0.0 : std::stod(ceilingDisplay);
    check(!ceilingDisplay.empty() && ceilingDb < -1.0, "the ceiling reads back (" + ceilingDisplay + ")");

    // NaN on a failed render: no numeric sentinel can satisfy both the
    // ceiling-holding bound below and the exceeds-the-ceiling bound after bypass,
    // so a failure is counted instead of silently reading as a passing peak.
    const auto render = [&] {
        JSON ra = JSON::object();
        ra.set("file", JSON(out));
        ra.set("tail", JSON(0.0));
        JSON rr = call(service, "render_song", ra);
        return statusOf(rr) == "ok" ? rr["result"]["peakDb"].asNumber()
                                    : std::numeric_limits<double>::quiet_NaN();
    };
    const double limited = render();
    // Finite and near the ceiling: a silenced or broken limiter returns -inf, which
    // the upper bound alone would accept.
    check(std::isfinite(limited) && limited <= ceilingDb + 0.3 && limited > ceilingDb - 6.0,
          "with the limiter the master peak holds the ceiling (" + std::to_string(limited) + " dBFS vs " +
              std::to_string(ceilingDb) + ")");
    a = JSON::object();
    a.set("track", JSON(-1.0));
    a.set("slot", JSON(1.0));
    a.set("state", JSON(true));
    check(statusOf(call(service, "bypass_effect", a)) == "ok", "bypass the master limiter");
    const double open = render();
    check(std::isfinite(open) && open > limited + 3.0,
          "bypassed, the boosted master goes over the ceiling (" + std::to_string(open) + " dBFS)");
    a = JSON::object();
    a.set("track", JSON(-1.0));
    a.set("slot", JSON(1.0));
    check(statusOf(call(service, "remove_effect", a)) == "ok" &&
              tm->getMasterChannel()->getEffectChain().getPlugin(1) == nullptr,
          "remove an effect from the master");
    a = JSON::object();
    a.set("track", JSON(-2.0));
    a.set("effect", JSON("com.Aestrastudios.eq"));
    check(statusOf(call(service, "add_effect", a)) != "ok", "track -2 is refused");

    std::filesystem::remove_all(runDir);
    if (g_failures == 0) {
        std::cout << "MuseMasterEffectsTest: all checks passed\n";
        return 0;
    }
    std::cout << "MuseMasterEffectsTest: " << g_failures << " failure(s)\n";
    return 1;
}
