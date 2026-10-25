// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-M1: render_song and verify_render render a beat range, or one mixer
// channel's own output (a stem), through the project's real mix.
//
// The point of the channel capture is isolation, so the test proves it: two
// units on two channels, only one of them playing. The playing channel's stem
// has audio, the silent one's stem is silent, and the master has the mix.
#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
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
        (std::filesystem::temp_directory_path() / "muse_render_scope_sample.wav").string();
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


JSON renderReq(double id, const std::string& verb, const std::string& file, JSON extra) {
    JSON req = JSON::object();
    req.set("id", JSON(id));
    req.set("verb", JSON(verb));
    extra.set("file", JSON(file));
    if (!extra.has("tail")) extra.set("tail", JSON(0.0));
    req.set("args", extra);
    return req;
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
    check(!sample.empty(), "test sample written");
    const auto tmp = std::filesystem::temp_directory_path();
    const std::string out = (tmp / "muse_render_scope_out.wav").string();

    // Two units, each on its own mixer channel. Only "Playing" gets notes.
    JSON r = call(service, "{\"id\": 1, \"verb\": \"add_unit\", \"args\": {\"name\": \"Playing\"}}");
    check(status(r) == "ok", "add first unit");
    r = call(service, "{\"id\": 2, \"verb\": \"add_unit\", \"args\": {\"name\": \"Silent\"}}");
    check(status(r) == "ok", "add second unit");
    for (int unit = 1; unit <= 2; ++unit) {
        r = call(service, "{\"id\": 3, \"verb\": \"load_sample\", \"args\": {\"unit\": " +
                              std::to_string(unit) + ", \"file\": \"" + sample + "\"}}");
        check(status(r) == "ok", "load sample into unit " + std::to_string(unit));
    }
    auto* playingChannel = trackManager->addChannel("Playing");
    auto* silentChannel = trackManager->addChannel("Silent");
    check(playingChannel && silentChannel, "two mixer channels");
    const uint32_t playingId = playingChannel->getChannelId();
    const uint32_t silentId = silentChannel->getChannelId();
    trackManager->getUnitManager().setUnitMixerChannel(Aestra::Audio::UnitID{1}, playingId);
    trackManager->getUnitManager().setUnitMixerChannel(Aestra::Audio::UnitID{2}, silentId);

    r = call(service, "{\"id\": 4, \"verb\": \"set_steps\", \"args\": {\"pattern\": 1, \"unit\": 1, "
                      "\"pitch\": 60, \"steps\": \"x---x---x---x---x---x---x---x---\"}}");
    check(status(r) == "ok", "kick row on the playing unit");
    r = call(service, "{\"id\": 5, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": 1, \"track\": 0, \"start\": 0}}");
    check(status(r) == "ok", "arrange at beat 0");
    r = call(service, "{\"id\": 6, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": 1, \"track\": 0, \"start\": 8}}");
    check(status(r) == "ok", "arrange at beat 8");

    // --- range ---------------------------------------------------------------
    r = call(service, renderReq(10, "render_song", out, JSON::object()).toString());
    check(status(r) == "ok", "full render ok");
    const double fullSeconds = r["result"]["durationSeconds"].asNumber();
    check(!r["result"].has("startBeat"), "a full render reports no range");

    JSON range = JSON::object();
    range.set("start", JSON(8.0));
    range.set("end", JSON(16.0));
    r = call(service, renderReq(11, "render_song", out, range).toString());
    check(status(r) == "ok", "range render ok");
    check(std::abs(r["result"]["durationSeconds"].asNumber() - fullSeconds / 2.0) < 0.01,
          "beats 8-16 render half of the 16-beat song (" +
              std::to_string(r["result"]["durationSeconds"].asNumber()) + "s of " + std::to_string(fullSeconds) + "s)");
    check(r["result"]["startBeat"].asNumber() == 8.0 && r["result"]["endBeat"].asNumber() == 16.0,
          "the range is echoed");
    check(r["result"]["peakDb"].asNumber() > -90.0, "the range render has the late clip in it");

    JSON fromOnly = JSON::object();
    fromOnly.set("start", JSON(8.0));
    r = call(service, renderReq(12, "render_song", out, fromOnly).toString());
    check(status(r) == "ok" && r["result"]["endBeat"].asNumber() == 16.0,
          "start alone runs to the end of the timeline");

    // --- channel capture -----------------------------------------------------
    JSON playing = JSON::object();
    playing.set("channel", JSON(static_cast<double>(playingId)));
    r = call(service, renderReq(20, "render_song", out, playing).toString());
    check(status(r) == "ok", "stem of the playing channel ok");
    check(r["result"]["peakDb"].asNumber() > -90.0,
          "the playing channel's stem has audio (peakDb " + std::to_string(r["result"]["peakDb"].asNumber()) + ")");
    check(r["result"]["channel"].asNumber() == static_cast<double>(playingId) &&
              r["result"]["postFader"].asBool(),
          "the channel and post-fader default are echoed");

    JSON silent = JSON::object();
    silent.set("channel", JSON(static_cast<double>(silentId)));
    r = call(service, renderReq(21, "render_song", out, silent).toString());
    check(status(r) == "ok", "stem of the silent channel ok");
    check(r["result"]["peakDb"].asNumber() < -90.0,
          "the silent channel's stem is silent: the capture is that channel only (peakDb " +
              std::to_string(r["result"]["peakDb"].asNumber()) + ")");

    JSON preFader = JSON::object();
    preFader.set("channel", JSON(static_cast<double>(playingId)));
    preFader.set("post_fader", JSON(false));
    r = call(service, renderReq(22, "render_song", out, preFader).toString());
    check(status(r) == "ok" && !r["result"]["postFader"].asBool(), "pre-fader capture ok");

    // --- verify_render takes the same scope ----------------------------------
    JSON verifyRange = JSON::object();
    verifyRange.set("start", JSON(8.0));
    verifyRange.set("end", JSON(16.0));
    verifyRange.set("channel", JSON(static_cast<double>(playingId)));
    r = call(service, renderReq(30, "verify_render", out, verifyRange).toString());
    check(status(r) == "ok" && r["result"]["verdict"].asString() == "pass",
          "verify_render judges a ranged stem");
    check(r["result"]["startBeat"].asNumber() == 8.0 && r["result"]["channel"].asNumber() == playingId,
          "verify_render echoes its scope");

    // --- refusals ------------------------------------------------------------
    const auto refused = [&](double id, JSON args, const std::string& label) {
        JSON resp = call(service, renderReq(id, "render_song", out, args).toString());
        check(status(resp) == "validation_error", label + " -> validation_error (" +
                                                      (resp.has("message") ? resp["message"].asString() : "") + ")");
    };
    JSON a = JSON::object();
    a.set("start", JSON(8.0));
    a.set("end", JSON(8.0));
    refused(40, a, "end equal to start");
    a = JSON::object();
    a.set("start", JSON(-1.0));
    refused(41, a, "negative start");
    a = JSON::object();
    a.set("start", JSON(64.0));
    refused(42, a, "start past the end of the timeline");
    a = JSON::object();
    a.set("channel", JSON(0.0));
    refused(43, a, "channel 0 (that is the master: render without channel)");
    a = JSON::object();
    a.set("channel", JSON(9999.0));
    refused(44, a, "unknown channel");
    a = JSON::object();
    a.set("post_fader", JSON(false));
    refused(45, a, "post_fader without channel");
    a = JSON::object();
    a.set("channel", JSON("one"));
    refused(46, a, "non-numeric channel");

    std::filesystem::remove(out);
    std::filesystem::remove(sample);
    if (g_failures == 0) {
        std::cout << "MuseRenderScopeTest: all checks passed\n";
        return 0;
    }
    std::cout << "MuseRenderScopeTest: " << g_failures << " failure(s)\n";
    return 1;
}
