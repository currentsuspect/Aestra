// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-M5: Muse routes Arsenal units to mixer tracks, routes track outputs into
// buses, adds/edits/removes sends (including sidechain-only sends) and reorders
// insert effects — and refuses every illegal route with a reason instead of
// reporting a no-op as ok.
//
// The unit routing is proven in audio, not only in the model: a kick routed to
// track 0 goes silent when track 0 is muted, and comes back when it is routed
// straight to the master.
#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Core/MixerChannel.h"
#include "Plugin/PluginManager.h"
#include "RealtimeThreadGuard.h"

#include "AestraJSON.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using Aestra::Audio::AudioEngine;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MixerChannel;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using Aestra::JSON;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

JSON call(MuseService& service, const std::string& request) {
    const std::string responseText = service.handleRequest(request);
    return JSON::parse(responseText);
}

std::string status(JSON& response) {
    return response.has("status") ? response["status"].asString() : "<missing>";
}

// Requests carrying filesystem paths must be serialized, not interpolated —
// Windows paths contain backslashes that are invalid JSON escapes raw.
std::string fileRequest(double id, const std::string& verb, const std::string& idKey,
                        double idValue, const std::string& file, double tail = -1.0) {
    JSON req = JSON::object();
    req.set("id", JSON(id));
    req.set("verb", JSON(verb));
    JSON args = JSON::object();
    args.set(idKey, JSON(idValue));
    args.set("file", JSON(file));
    if (tail >= 0.0) args.set("tail", JSON(tail));
    req.set("args", args);
    return req.toString();
}

// Minimal valid PCM16 mono WAV for load_sample: a loud 480-frame click so a
// rendered pattern that triggers it is measurably non-silent.
std::string writeTestWav() {
    const std::string path =
        (std::filesystem::temp_directory_path() / "muse_mixer_routing_sample.wav").string();
    const uint32_t sampleRate = 48000;
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t numFrames = 480;
    const uint32_t dataBytes = numFrames * sizeof(int16_t);
    const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    const uint16_t blockAlign = channels * bitsPerSample / 8;

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return "";
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(1); // PCM
    u16(channels);
    u32(sampleRate);
    u32(byteRate);
    u16(blockAlign);
    u16(bitsPerSample);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (uint32_t i = 0; i < numFrames; ++i) {
        const int16_t sample = (i % 2 == 0) ? 29000 : -29000;
        std::fwrite(&sample, sizeof(int16_t), 1, f);
    }
    std::fclose(f);
    return path;
}


JSON req(double id, const std::string& verb, JSON args) {
    JSON r = JSON::object();
    r.set("id", JSON(id));
    r.set("verb", JSON(verb));
    r.set("args", args);
    return r;
}

JSON args() { return JSON::object(); }

std::string statusOf(JSON response) { return status(response); }

double renderPeak(MuseService& service, const std::string& file) {
    JSON a = args();
    a.set("file", JSON(file));
    a.set("tail", JSON(0.0));
    JSON r = call(service, req(900, "render_song", a).toString());
    return status(r) == "ok" ? r["result"]["peakDb"].asNumber() : 999.0;
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) {
        std::cout << "FAIL: plugin manager initialize\n";
        return 1;
    }
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(trackManager, engine);
    if (!engine.initialize()) {
        std::cout << "FAIL: engine initialize\n";
        return 1;
    }
    CommandRegistry::initialize();
    MuseService service(trackManager.get(), &engine);
    const std::string sample = writeTestWav();
    const std::string out = (std::filesystem::temp_directory_path() / "muse_mixer_routing_out.wav").string();

    for (const char* name : {"Drums", "Bus", "Verb"}) {
        JSON a = args();
        a.set("name", JSON(name));
        check(statusOf(call(service, req(1, "add_track", a).toString())) == "ok", std::string("add track ") + name);
    }
    MixerChannel* drums = trackManager->getChannel(0);
    MixerChannel* bus = trackManager->getChannel(1);
    MixerChannel* verb = trackManager->getChannel(2);
    check(drums && bus && verb, "three tracks at indexes 0..2");
    if (!drums || !bus || !verb) return 1;

    // A kick unit with one hit, arranged on the timeline.
    JSON a = args();
    a.set("name", JSON("Kick"));
    check(statusOf(call(service, req(2, "add_unit", a).toString())) == "ok", "add kick unit");
    JSON load = args();
    load.set("unit", JSON(1.0));
    load.set("file", JSON(sample));
    check(statusOf(call(service, req(3, "load_sample", load).toString())) == "ok", "load kick sample");
    JSON steps = args();
    steps.set("pattern", JSON(1.0));
    steps.set("unit", JSON(1.0));
    steps.set("pitch", JSON(60.0));
    steps.set("steps", JSON("x---x---x---x---"));
    check(statusOf(call(service, req(4, "set_steps", steps).toString())) == "ok", "kick row");
    JSON arrange = args();
    arrange.set("pattern", JSON(1.0));
    arrange.set("track", JSON(0.0));
    arrange.set("start", JSON(0.0));
    check(statusOf(call(service, req(5, "arrange_pattern", arrange).toString())) == "ok", "arrange kick");

    // --- route_unit, proven in audio --------------------------------------------
    a = args();
    a.set("unit", JSON(1.0));
    a.set("track", JSON(0.0));
    check(statusOf(call(service, req(10, "route_unit", a).toString())) == "ok", "route the kick to track 0");
    check(trackManager->getUnitManager().getUnit(1)->targetMixerChannelId == drums->getChannelId(),
          "the unit's destination is track 0's channel");
    check(renderPeak(service, out) > -90.0, "routed kick is audible");
    JSON mute = args();
    mute.set("track", JSON(0.0));
    mute.set("state", JSON(true));
    check(statusOf(call(service, req(11, "mute_track", mute).toString())) == "ok", "mute track 0");
    check(renderPeak(service, out) < -90.0, "muting track 0 silences the kick: its audio really goes through track 0");
    a = args();
    a.set("unit", JSON(1.0));
    a.set("track", JSON(-1.0));
    check(statusOf(call(service, req(12, "route_unit", a).toString())) == "ok", "route the kick straight to the master");
    check(trackManager->getUnitManager().getUnit(1)->targetMixerChannelId == 0, "destination 0 means the master");
    check(renderPeak(service, out) > -90.0, "routed to the master, the kick is audible again with track 0 muted");
    a = args();
    a.set("unit", JSON(99.0));
    a.set("track", JSON(0.0));
    check(statusOf(call(service, req(13, "route_unit", a).toString())) != "ok", "an unknown unit is refused");

    // --- set_output -------------------------------------------------------------
    a = args();
    a.set("track", JSON(0.0));
    a.set("to", JSON(1.0));
    check(statusOf(call(service, req(20, "set_output", a).toString())) == "ok", "track 0 outputs into the bus");
    check(drums->getMainOutputId() == bus->getChannelId(), "track 0's main output is the bus");
    a = args();
    a.set("track", JSON(1.0));
    a.set("to", JSON(0.0));
    JSON loop = call(service, req(21, "set_output", a).toString());
    check(status(loop) != "ok", "a routing loop is refused, not reported ok");
    check(bus->getMainOutputId() != drums->getChannelId(), "and nothing changed");
    a = args();
    a.set("track", JSON(0.0));
    a.set("to", JSON(-1.0));
    check(statusOf(call(service, req(22, "set_output", a).toString())) == "ok" && drums->getMainOutputId() == 0xFFFFFFFFu,
          "track 0 back to the master");

    // --- sends ------------------------------------------------------------------
    a = args();
    a.set("track", JSON(0.0));
    a.set("to", JSON(2.0));
    a.set("level", JSON(0.5));
    check(statusOf(call(service, req(30, "add_send", a).toString())) == "ok", "send track 0 to the verb track");
    check(drums->getSends().size() == 1 && drums->getSends()[0].targetChannelId == verb->getChannelId() &&
              std::abs(drums->getSends()[0].gain - 0.5f) < 1e-6f && drums->getSends()[0].postFader,
          "the send targets the verb at half level, post-fader");
    const uint64_t sendId = drums->getSends().empty() ? 0 : drums->getSends()[0].sendId;

    JSON edit = args();
    edit.set("track", JSON(0.0));
    edit.set("send", JSON(static_cast<double>(sendId)));
    edit.set("mute", JSON(true));
    edit.set("pre_fader", JSON(true));
    check(statusOf(call(service, req(31, "set_send", edit).toString())) == "ok", "edit the send");
    check(!drums->getSends().empty() && drums->getSends()[0].mute && !drums->getSends()[0].postFader &&
              std::abs(drums->getSends()[0].gain - 0.5f) < 1e-6f,
          "mute and pre-fader changed; the level was kept");

    JSON rm = args();
    rm.set("track", JSON(0.0));
    rm.set("send", JSON(static_cast<double>(sendId)));
    check(statusOf(call(service, req(32, "remove_send", rm).toString())) == "ok" && drums->getSends().empty(),
          "remove the send");
    check(statusOf(call(service, "{\"id\": 33, \"verb\": \"undo\"}")) == "ok" && drums->getSends().size() == 1 &&
              drums->getSends()[0].sendId == sendId,
          "undo restores the send with the same sendId");

    a = args();
    a.set("track", JSON(1.0));
    a.set("to", JSON(0.0));
    a.set("sidechain", JSON(true));
    check(statusOf(call(service, req(34, "add_send", a).toString())) == "ok" && bus->getSends().size() == 1 &&
              bus->getSends()[0].sidechainOnly,
          "a sidechain-only send");
    a = args();
    a.set("track", JSON(0.0));
    a.set("to", JSON(-1.0));
    check(statusOf(call(service, req(35, "add_send", a).toString())) != "ok", "a send to the master is refused");
    a = args();
    a.set("track", JSON(0.0));
    a.set("to", JSON(0.0));
    check(statusOf(call(service, req(36, "add_send", a).toString())) != "ok", "a send to itself is refused");
    edit = args();
    edit.set("track", JSON(0.0));
    edit.set("send", JSON(4242.0));
    edit.set("mute", JSON(false));
    check(statusOf(call(service, req(37, "set_send", edit).toString())) != "ok", "an unknown sendId is refused");

    // --- move_effect --------------------------------------------------------------
    const auto effects = Aestra::Audio::PluginManager::getInstance().getEffectPlugins();
    check(effects.size() >= 2, "at least two built-in effects to reorder");
    if (effects.size() >= 2) {
        for (size_t i = 0; i < 2; ++i) {
            JSON fx = args();
            fx.set("track", JSON(0.0));
            fx.set("effect", JSON(effects[i].id));
            check(statusOf(call(service, req(40 + static_cast<double>(i), "add_effect", fx).toString())) == "ok",
                  "insert " + effects[i].name);
        }
        auto& chain = drums->getEffectChain();
        const auto first = chain.getPlugin(0);
        const auto second = chain.getPlugin(1);
        JSON mv = args();
        mv.set("track", JSON(0.0));
        mv.set("from", JSON(0.0));
        mv.set("to", JSON(1.0));
        check(statusOf(call(service, req(42, "move_effect", mv).toString())) == "ok", "move slot 0 to slot 1");
        check(chain.getPlugin(0) == second && chain.getPlugin(1) == first, "the two effects swapped places");
        mv = args();
        mv.set("track", JSON(0.0));
        mv.set("from", JSON(7.0));
        mv.set("to", JSON(0.0));
        check(statusOf(call(service, req(43, "move_effect", mv).toString())) != "ok", "moving an empty slot is refused");
    }

    std::filesystem::remove(out);
    std::filesystem::remove(sample);
    if (g_failures == 0) {
        std::cout << "MuseMixerRoutingTest: all checks passed\n";
        return 0;
    }
    std::cout << "MuseMixerRoutingTest: " << g_failures << " failure(s)\n";
    return 1;
}
