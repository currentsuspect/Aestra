// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseService Tests — the structured JSON surface agents drive Aestra through.
// Covers: request/response protocol, query verbs (eyes), mutation verbs
// routed through CommandHistory (hands + undo), stable IDs, and malformed
// input never crashing or corrupting the session.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <cstdint>
#include <cstdio>
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

// Minimal valid PCM16 mono WAV for load_sample: a loud 480-frame click so a
// rendered pattern that triggers it is measurably non-silent.
std::string writeTestWav() {
    const std::string path =
        (std::filesystem::temp_directory_path() / "muse_service_test_sample.wav").string();
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

} // namespace

int main() {
    // Built-ins (the sampler) register inside PluginManager::initialize();
    // without it load_sample sets the path but never instantiates a plugin,
    // and renders come back silent.
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) {
        std::cout << "FAIL: plugin manager initialize\n";
        return 1;
    }

    auto trackManager = std::make_shared<TrackManager>();
    // Mirror the app wiring (AestraContent): units need the pattern manager
    // to auto-create their default MIDI pattern.
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());

    // Engine wired like MuseRepl so render_pattern can pump the live path.
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(512, 2);
    MuseService::wireHeadlessEngine(trackManager, engine);
    if (!engine.initialize()) {
        std::cout << "FAIL: engine initialize\n";
        return 1;
    }

    CommandRegistry::initialize(trackManager.get());
    CommandRegistry::setAudioEngine(&engine);
    MuseService service(trackManager.get(), &engine);

    // --- Protocol: malformed input never crashes, always structured error ---
    {
        JSON r = call(service, "not json at all");
        check(status(r) == "parse_error", "garbage input -> parse_error");

        r = call(service, "[1,2,3]");
        check(status(r) == "parse_error", "non-object request -> parse_error");

        r = call(service, "{\"id\": 7}");
        check(status(r) == "parse_error" && r["id"].asNumber() == 7.0,
              "missing verb -> parse_error, id echoed");

        r = call(service, "{\"id\": 8, \"verb\": \"warp_reality\"}");
        check(status(r) == "parse_error" && r["message"].asString().find("unknown") != std::string::npos,
              "unknown verb -> parse_error with message");

        r = call(service, "{\"id\": \"abc\", \"verb\": \"list_tracks\"}");
        check(status(r) == "parse_error", "non-numeric id -> parse_error, not silent 0");
    }

    // --- Eyes: queries on an empty session ---
    {
        JSON r = call(service, "{\"id\": 1, \"verb\": \"list_tracks\"}");
        check(status(r) == "ok", "list_tracks ok on empty session");
        check(r["result"]["tracks"].size() == 0, "empty session has zero tracks");

        r = call(service, "{\"id\": 2, \"verb\": \"get_session_state\"}");
        check(status(r) == "ok", "get_session_state ok");
        check(r["result"]["transport"]["playing"].isBool(), "session state reports transport");
    }

    // --- Hands: build a session through mutations ---
    {
        JSON r = call(service, "{\"id\": 10, \"verb\": \"add_track\", \"args\": {\"name\": \"Drums\"}}");
        check(status(r) == "ok", "add_track Drums ok");
        r = call(service, "{\"id\": 11, \"verb\": \"add_track\", \"args\": {\"name\": \"Bass\"}}");
        check(status(r) == "ok", "add_track Bass ok");

        r = call(service, "{\"id\": 12, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == 2, "two tracks listed");
        check(r["result"]["tracks"][0]["name"].asString() == "Drums", "track 0 named Drums");
        check(r["result"]["tracks"][1]["name"].asString() == "Bass", "track 1 named Bass");
        check(r["result"]["tracks"][0]["id"].isNumber() &&
                  r["result"]["tracks"][0]["id"].asNumber() !=
                      r["result"]["tracks"][1]["id"].asNumber(),
              "tracks carry distinct stable ids");
    }

    // --- Typed args: numbers and bools cross the JSON->flag boundary ---
    {
        JSON r = call(service,
                      "{\"id\": 20, \"verb\": \"set_volume\", \"args\": {\"track\": 0, \"value\": 0.5}}");
        check(status(r) == "ok", "set_volume with numeric args ok");

        r = call(service, "{\"id\": 21, \"verb\": \"list_tracks\"}");
        const double vol = r["result"]["tracks"][0]["volume"].asNumber();
        check(vol > 0.49 && vol < 0.51, "volume readback reflects mutation");

        r = call(service,
                 "{\"id\": 22, \"verb\": \"mute_track\", \"args\": {\"track\": 1, \"state\": true}}");
        check(status(r) == "ok", "mute_track with bool arg ok");
        r = call(service, "{\"id\": 23, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"][1]["muted"].asBool(), "mute readback reflects mutation");
    }

    // --- Validation: schema ranges still guard the JSON path ---
    {
        JSON r = call(service,
                      "{\"id\": 30, \"verb\": \"set_volume\", \"args\": {\"track\": 0, \"value\": 4.0}}");
        check(status(r) == "validation_error", "out-of-range volume -> validation_error");

        r = call(service, "{\"id\": 31, \"verb\": \"set_volume\", \"args\": {\"track\": 0}}");
        check(status(r) == "validation_error", "missing required flag -> validation_error");

        r = call(service, "{\"id\": 32, \"verb\": \"list_tracks\"}");
        const double vol = r["result"]["tracks"][0]["volume"].asNumber();
        check(vol > 0.49 && vol < 0.51, "failed mutations leave state untouched");
    }

    // --- Contract honesty: nothing the protocol ignores returns ok ---
    {
        JSON r = call(service,
                      "{\"id\": 35, \"verb\": \"set_volume\", "
                      "\"args\": {\"track\": 0, \"value\": 0.5, \"wobble\": 9}}");
        check(status(r) == "validation_error", "unknown mutation flag -> validation_error");

        r = call(service,
                 "{\"id\": 36, \"verb\": \"add_track\", "
                 "\"args\": {\"name\": \"X\", \"type\": \"sampler\"}}");
        check(status(r) == "validation_error",
              "add_track type (unconsumed by factory) -> validation_error");

        r = call(service, "{\"id\": 37, \"verb\": \"list_tracks\", \"args\": {\"track\": 0}}");
        check(status(r) == "validation_error", "arguments to a query -> validation_error");
    }

    // --- Stable IDs survive edits that shift indexes ---
    {
        JSON r = call(service, "{\"id\": 38, \"verb\": \"list_tracks\"}");
        const double bassId = r["result"]["tracks"][1]["id"].asNumber();

        r = call(service, "{\"id\": 39, \"verb\": \"delete_track\", \"args\": {\"track\": 0}}");
        check(status(r) == "ok", "delete_track ok");

        r = call(service, "{\"id\": 40, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == 1, "one track remains after delete");
        check(r["result"]["tracks"][0]["id"].asNumber() == bassId,
              "surviving track keeps its id after index shift");
        check(r["result"]["tracks"][0]["name"].asString() == "Bass",
              "surviving track is Bass");
    }

    // --- Revision loop: undo through the same history the UI uses ---
    {
        auto& history = trackManager->getCommandHistory();
        check(history.canUndo(), "mutations were recorded as undoable history");
        history.undo(); // restore the deleted Drums track
        JSON r = call(service, "{\"id\": 41, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == 2, "undo restores the deleted track");
    }

    // --- Beat path: units ---
    double kickUnit = 0.0;
    double kickPattern = 0.0;
    {
        JSON r = call(service,
                      "{\"id\": 50, \"verb\": \"add_unit\", \"args\": {\"name\": \"Kick\"}}");
        check(status(r) == "ok", "add_unit Kick ok");

        r = call(service,
                 "{\"id\": 51, \"verb\": \"add_unit\", "
                 "\"args\": {\"name\": \"Sub\", \"type\": \"808\"}}");
        check(status(r) == "ok", "add_unit 808 ok");

        r = call(service,
                 "{\"id\": 52, \"verb\": \"add_unit\", "
                 "\"args\": {\"name\": \"X\", \"type\": \"theremin\"}}");
        check(status(r) == "execution_error", "unknown unit type rejected");

        r = call(service, "{\"id\": 53, \"verb\": \"list_units\"}");
        check(status(r) == "ok", "list_units ok");
        check(r["result"]["units"].size() == 2, "two units listed");
        check(r["result"]["units"][0]["name"].asString() == "Kick", "unit 0 named Kick");
        check(r["result"]["units"][1]["type"].asString() == "808", "unit 1 typed 808");
        kickUnit = r["result"]["units"][0]["id"].asNumber();
        kickPattern = r["result"]["units"][0]["defaultPatternId"].asNumber();
        check(kickPattern > 0.0, "unit creation minted a default pattern");
    }

    // --- Beat path: sample loading ---
    {
        JSON r = call(service,
                      "{\"id\": 55, \"verb\": \"load_sample\", \"args\": {\"unit\": " +
                          std::to_string(static_cast<long long>(kickUnit)) +
                          ", \"file\": \"/nonexistent/kick.wav\"}}");
        check(status(r) == "execution_error", "load_sample on missing file -> execution_error");

        const std::string wavPath = writeTestWav();
        check(!wavPath.empty(), "test wav written");
        r = call(service, "{\"id\": 56, \"verb\": \"load_sample\", \"args\": {\"unit\": " +
                              std::to_string(static_cast<long long>(kickUnit)) +
                              ", \"file\": \"" + wavPath + "\"}}");
        check(status(r) == "ok", "load_sample ok");
        r = call(service, "{\"id\": 57, \"verb\": \"list_units\"}");
        check(r["result"]["units"][0]["samplePath"].asString() == wavPath,
              "unit readback carries sample path");
    }

    // --- Beat path: notes in the unit's default pattern ---
    {
        const std::string p = std::to_string(static_cast<long long>(kickPattern));
        const std::string u = std::to_string(static_cast<long long>(kickUnit));

        JSON r = call(service, "{\"id\": 60, \"verb\": \"add_note\", \"args\": {\"pattern\": " + p +
                                   ", \"unit\": " + u +
                                   ", \"pitch\": 36, \"start\": 0, \"duration\": 1}}");
        check(status(r) == "ok", "add_note ok");

        r = call(service, "{\"id\": 61, \"verb\": \"add_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u +
                              ", \"pitch\": 36, \"start\": 2.5, \"duration\": 0.5, "
                              "\"velocity\": 0.6, \"pan\": -0.25}}");
        check(status(r) == "ok", "add_note with expression ok");

        r = call(service, "{\"id\": 62, \"verb\": \"add_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u +
                              ", \"pitch\": 200, \"start\": 0, \"duration\": 1}}");
        check(status(r) == "validation_error", "out-of-range pitch -> validation_error");

        r = call(service,
                 "{\"id\": 63, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        check(status(r) == "ok", "get_pattern ok");
        check(r["result"]["notes"].size() == 2, "pattern readback lists both notes");
        check(r["result"]["notes"][1]["velocity"].asNumber() > 0.59 &&
                  r["result"]["notes"][1]["velocity"].asNumber() < 0.61,
              "note expression round-trips");

        // Revision loop: move the second hit, then soften it, then delete it.
        r = call(service, "{\"id\": 64, \"verb\": \"move_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u +
                              ", \"pitch\": 36, \"start\": 2.5, \"to_start\": 3}}");
        check(status(r) == "ok", "move_note ok");

        r = call(service, "{\"id\": 65, \"verb\": \"set_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u +
                              ", \"pitch\": 36, \"start\": 3, \"velocity\": 0.3}}");
        check(status(r) == "ok", "set_note ok");

        r = call(service,
                 "{\"id\": 66, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        check(r["result"]["notes"][1]["start"].asNumber() == 3.0, "moved note reads back at 3");
        check(r["result"]["notes"][1]["velocity"].asNumber() < 0.31,
              "softened velocity reads back");
        check(r["result"]["notes"][1]["pan"].asNumber() < -0.24,
              "set_note leaves untouched expression alone");

        r = call(service, "{\"id\": 67, \"verb\": \"delete_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u + ", \"pitch\": 36, \"start\": 3}}");
        check(status(r) == "ok", "delete_note ok");

        r = call(service, "{\"id\": 68, \"verb\": \"delete_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u + ", \"pitch\": 36, \"start\": 3}}");
        check(status(r) == "execution_error", "deleting a missing note -> execution_error");

        r = call(service,
                 "{\"id\": 69, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        check(r["result"]["notes"].size() == 1, "one note remains after delete");

        trackManager->getCommandHistory().undo(); // restore the deleted note
        r = call(service,
                 "{\"id\": 70, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        check(r["result"]["notes"].size() == 2, "undo restores the deleted note");
    }

    // --- get_pattern arg contract ---
    {
        JSON r = call(service, "{\"id\": 75, \"verb\": \"get_pattern\"}");
        check(status(r) == "validation_error", "get_pattern without args -> validation_error");

        r = call(service,
                 "{\"id\": 76, \"verb\": \"get_pattern\", \"args\": {\"pattern\": 999999}}");
        check(status(r) == "execution_error", "get_pattern unknown id -> execution_error");

        r = call(service,
                 "{\"id\": 77, \"verb\": \"get_pattern\", "
                 "\"args\": {\"pattern\": 1, \"wobble\": 2}}");
        check(status(r) == "validation_error", "get_pattern unknown arg -> validation_error");
    }

    // --- Render: the beat comes back as a file with signal in it ---
    {
        const std::string p = std::to_string(static_cast<long long>(kickPattern));
        const std::string outPath =
            (std::filesystem::temp_directory_path() / "muse_service_test_render.wav").string();
        std::filesystem::remove(outPath);

        JSON r = call(service, "{\"id\": 80, \"verb\": \"render_pattern\"}");
        check(status(r) == "validation_error", "render_pattern without args -> validation_error");

        r = call(service,
                 "{\"id\": 81, \"verb\": \"render_pattern\", "
                 "\"args\": {\"pattern\": 999999, \"file\": \"" + outPath + "\"}}");
        check(status(r) == "execution_error", "render_pattern unknown pattern -> execution_error");

        r = call(service, "{\"id\": 82, \"verb\": \"render_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"file\": \"" + outPath + "\", \"tail\": 0.25}}");
        check(status(r) == "ok", "render_pattern ok");
        check(r["result"]["frames"].asNumber() > 0.0, "render reports frames");
        check(std::filesystem::exists(outPath) && std::filesystem::file_size(outPath) > 44,
              "rendered wav exists with data");
        // The pattern triggers the loud test click through the sampler; a
        // silent render means the pattern->unit->engine path is broken.
        check(r["result"]["peakDb"].asNumber() > -90.0,
              "rendered audio is not silent (peakDb " +
                  std::to_string(r["result"]["peakDb"].asNumber()) + ")");
        check(!engine.isTransportPlaying(), "engine transport stopped after render");

        r = call(service, "{\"id\": 83, \"verb\": \"render_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"file\": \"" + outPath + "\", \"wobble\": 1}}");
        check(status(r) == "validation_error", "render_pattern unknown arg -> validation_error");
        std::filesystem::remove(outPath);
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
