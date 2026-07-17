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
    // Must cover AudioExporter's 4096-frame render blocks: processBlock does
    // not split blocks larger than the configured maximum, it overruns.
    engine.setBufferConfig(4096, 2);
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
                      fileRequest(55, "load_sample", "unit", kickUnit, "/nonexistent/kick.wav"));
        check(status(r) == "execution_error", "load_sample on missing file -> execution_error");

        const std::string wavPath = writeTestWav();
        check(!wavPath.empty(), "test wav written");
        r = call(service, fileRequest(56, "load_sample", "unit", kickUnit, wavPath));
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

    // --- Musical verbs: whole grooves in one gesture ---
    {
        const std::string p = std::to_string(static_cast<long long>(kickPattern));
        const std::string u = std::to_string(static_cast<long long>(kickUnit));

        JSON r = call(service,
                      "{\"id\": 140, \"verb\": \"set_steps\", \"args\": {\"pattern\": " + p +
                          ", \"unit\": " + u +
                          ", \"pitch\": 40, \"steps\": \"x---X---x---X-x-\"}}");
        check(status(r) == "ok", "set_steps writes a 16-step row");

        r = call(service,
                 "{\"id\": 141, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        size_t rowNotes = 0;
        double accentVelocity = 0.0;
        double plainVelocity = 0.0;
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            JSON note = r["result"]["notes"][i];
            if (note["pitch"].asNumber() == 40.0) {
                ++rowNotes;
                if (note["start"].asNumber() == 1.0) accentVelocity = note["velocity"].asNumber();
                if (note["start"].asNumber() == 0.0) plainVelocity = note["velocity"].asNumber();
            }
        }
        check(rowNotes == 5, "step string with 5 hits lands 5 notes");
        check(accentVelocity > plainVelocity, "accented step is louder than plain step");

        // Rewriting the row replaces it — the revision gesture.
        r = call(service, "{\"id\": 142, \"verb\": \"set_steps\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u + ", \"pitch\": 40, \"steps\": \"x-------\"}}");
        check(status(r) == "ok", "rewriting the row ok");
        r = call(service,
                 "{\"id\": 143, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        rowNotes = 0;
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            if (r["result"]["notes"][i]["pitch"].asNumber() == 40.0) ++rowNotes;
        }
        check(rowNotes == 1, "rewritten row replaced the old one");

        // Undo restores the previous groove, not an empty row.
        trackManager->getCommandHistory().undo();
        r = call(service,
                 "{\"id\": 144, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        rowNotes = 0;
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            if (r["result"]["notes"][i]["pitch"].asNumber() == 40.0) ++rowNotes;
        }
        check(rowNotes == 5, "undoing the rewrite restores the previous row");

        // Garbage in the step string is an error, not a silent rest.
        r = call(service, "{\"id\": 145, \"verb\": \"set_steps\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u + ", \"pitch\": 40, \"steps\": \"x--q\"}}");
        check(status(r) == "execution_error", "invalid step character rejected");

        // Quantize: drag an off-grid note onto the grid.
        r = call(service, "{\"id\": 146, \"verb\": \"add_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u +
                              ", \"pitch\": 41, \"start\": 1.13, \"duration\": 0.5}}");
        check(status(r) == "ok", "off-grid note added");
        r = call(service, "{\"id\": 147, \"verb\": \"quantize_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"grid\": 0.25}}");
        check(status(r) == "ok", "quantize_pattern ok");
        r = call(service,
                 "{\"id\": 148, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        double quantizedStart = -1.0;
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            if (r["result"]["notes"][i]["pitch"].asNumber() == 41.0) {
                quantizedStart = r["result"]["notes"][i]["start"].asNumber();
            }
        }
        check(std::abs(quantizedStart - 1.25) < 1e-9, "off-grid note snapped to 1.25");
        trackManager->getCommandHistory().undo();
        r = call(service,
                 "{\"id\": 149, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            if (r["result"]["notes"][i]["pitch"].asNumber() == 41.0) {
                quantizedStart = r["result"]["notes"][i]["start"].asNumber();
            }
        }
        // 1.13 crossed the JSON->flag->float boundary, so compare at float
        // precision.
        check(std::abs(quantizedStart - 1.13) < 1e-6, "undo restores the exact off-grid start");

        // Transpose: shift the whole pattern up an octave and back.
        r = call(service,
                 "{\"id\": 150, \"verb\": \"transpose_pattern\", \"args\": {\"pattern\": " + p +
                     ", \"semitones\": 12}}");
        check(status(r) == "ok", "transpose up an octave ok");
        r = call(service,
                 "{\"id\": 151, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        bool sawShifted = false;
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            if (r["result"]["notes"][i]["pitch"].asNumber() == 52.0) sawShifted = true;
        }
        check(sawShifted, "row pitch 40 reads back at 52 after transpose");
        trackManager->getCommandHistory().undo();

        // Undo must land every note back on its original pitch.
        r = call(service,
                 "{\"id\": 156, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        size_t at40 = 0, at52 = 0;
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            const double pitch = r["result"]["notes"][i]["pitch"].asNumber();
            if (pitch == 40.0) ++at40;
            if (pitch == 52.0) ++at52;
        }
        check(at40 == 5 && at52 == 0, "undo returns all row notes to pitch 40");

        // Out-of-range transpose is rejected, state untouched.
        r = call(service,
                 "{\"id\": 152, \"verb\": \"transpose_pattern\", \"args\": {\"pattern\": " + p +
                     ", \"semitones\": -48}}");
        check(status(r) == "execution_error", "transpose past MIDI range rejected");
        r = call(service,
                 "{\"id\": 157, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        bool pitchesIntact = true;
        for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
            const double pitch = r["result"]["notes"][i]["pitch"].asNumber();
            if (pitch != 36.0 && pitch != 40.0 && pitch != 41.0) pitchesIntact = false;
        }
        check(pitchesIntact, "rejected transpose left no partially shifted pitches");

        // Cleanup: drop the scratch notes so later sections see the original
        // two-note pattern.
        r = call(service, "{\"id\": 153, \"verb\": \"delete_note\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u + ", \"pitch\": 41, \"start\": 1.13}}");
        check(status(r) == "ok", "scratch quantize note removed");
        r = call(service, "{\"id\": 154, \"verb\": \"set_steps\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u +
                              ", \"pitch\": 40, \"steps\": \"----------------\"}}");
        check(status(r) == "ok", "row cleared with an all-rest step string");
        r = call(service,
                 "{\"id\": 155, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        check(r["result"]["notes"].size() == 2, "pattern back to its two original notes");

        // Batch composition: a whole groove in one gesture, one undo step.
        r = call(service,
                 "{\"id\": 158, \"verb\": \"batch\", \"args\": {\"commands\": ["
                 "{\"verb\": \"set_steps\", \"args\": {\"pattern\": " + p +
                     ", \"unit\": " + u + ", \"pitch\": 45, \"steps\": \"x---x---\"}},"
                 "{\"verb\": \"set_steps\", \"args\": {\"pattern\": " + p +
                     ", \"unit\": " + u + ", \"pitch\": 47, \"steps\": \"--x---x-\"}},"
                 "{\"verb\": \"quantize_pattern\", \"args\": {\"pattern\": " + p +
                     ", \"grid\": 0.25}}]}}");
        check(status(r) == "ok", "batch of musical verbs ok");
        r = call(service,
                 "{\"id\": 159, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        check(r["result"]["notes"].size() == 6, "batch groove landed both rows");
        trackManager->getCommandHistory().undo();
        r = call(service,
                 "{\"id\": 160, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " + p + "}}");
        check(r["result"]["notes"].size() == 2,
              "single undo reverts the whole musical batch");
    }

    // --- Agent UX: refusals carry reasons, schema carries semantics ---
    {
        const std::string p = std::to_string(static_cast<long long>(kickPattern));
        const std::string u = std::to_string(static_cast<long long>(kickUnit));

        // Factory refusals name the object and the rule, not just the verb.
        JSON r = call(service,
                      "{\"id\": 170, \"verb\": \"set_steps\", \"args\": {\"pattern\": 999999, "
                      "\"unit\": " + u + ", \"pitch\": 40, \"steps\": \"x---\"}}");
        check(status(r) == "execution_error" &&
                  r["message"].asString().find("no such MIDI pattern: 999999") != std::string::npos,
              "unknown pattern refusal names the pattern");

        r = call(service, "{\"id\": 171, \"verb\": \"set_steps\", \"args\": {\"pattern\": " + p +
                              ", \"unit\": " + u + ", \"pitch\": 40, \"steps\": \"x--q\"}}");
        check(r["message"].asString().find("invalid step character 'q'") != std::string::npos,
              "bad step char refusal names the character");

        r = call(service,
                 "{\"id\": 172, \"verb\": \"set_volume\", \"args\": {\"track\": 97, \"value\": 0.5}}");
        check(r["message"].asString().find("no such track: 97") != std::string::npos,
              "unknown track refusal names the index");

        r = call(service,
                 "{\"id\": 173, \"verb\": \"add_unit\", \"args\": {\"name\": \"X\", \"type\": "
                 "\"theremin\"}}");
        check(r["message"].asString().find("unknown unit type: theremin") != std::string::npos,
              "unknown unit type refusal names the type");

        // The schema manifest documents queries, actions, and semantics.
        const std::string schema = Aestra::Audio::MuseGrammar::schemaToJsonString();
        JSON manifest = JSON::parse(schema);
        check(manifest.has("commands") && manifest.has("queries") && manifest.has("actions") &&
                  manifest.has("notes"),
              "schema manifest has commands, queries, actions, notes");
        check(manifest["notes"]["samplerPitch"].asString().find("60") != std::string::npos,
              "schema notes document the sampler root");
        check(manifest["commands"][0].has("description"),
              "mutation entries carry descriptions");
    }

    // --- Pattern lifecycle: clone and length ---
    {
        const std::string p = std::to_string(static_cast<long long>(kickPattern));

        JSON r = call(service,
                      "{\"id\": 180, \"verb\": \"clone_pattern\", \"args\": {\"pattern\": " + p +
                          "}}");
        check(status(r) == "ok", "clone_pattern ok");
        check(r["message"].asString().find("cloned pattern -> ") != std::string::npos,
              "clone message is human-readable");
        check(r.has("result") && r["result"]["createdId"].isNumber(),
              "clone response carries the new id as structured data");
        const uint64_t clonedId = static_cast<uint64_t>(r["result"]["createdId"].asNumber());

        r = call(service, "{\"id\": 181, \"verb\": \"list_patterns\"}");
        check(status(r) == "ok", "list_patterns ok");
        bool sawClone = false;
        size_t patternCount = r["result"]["patterns"].size();
        double sourceNotes = -1.0, cloneNotes = -2.0;
        for (size_t i = 0; i < patternCount; ++i) {
            JSON entry = r["result"]["patterns"][i];
            if (entry["id"].asNumber() == static_cast<double>(clonedId)) {
                sawClone = true;
                cloneNotes = entry["noteCount"].asNumber();
            }
            if (entry["id"].asNumber() == kickPattern) sourceNotes = entry["noteCount"].asNumber();
        }
        check(sawClone, "clone appears in list_patterns");
        check(cloneNotes == sourceNotes, "clone carries the source notes");

        r = call(service, "{\"id\": 182, \"verb\": \"set_pattern_length\", \"args\": {\"pattern\": " +
                              std::to_string(clonedId) + ", \"beats\": 16}}");
        check(status(r) == "ok", "set_pattern_length ok");
        r = call(service, "{\"id\": 183, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " +
                              std::to_string(clonedId) + "}}");
        check(r["result"]["lengthBeats"].asNumber() == 16.0, "length readback is 16 beats");

        // Undo unwinds length then the clone itself.
        trackManager->getCommandHistory().undo();
        trackManager->getCommandHistory().undo();
        r = call(service, "{\"id\": 184, \"verb\": \"get_pattern\", \"args\": {\"pattern\": " +
                              std::to_string(clonedId) + "}}");
        check(status(r) == "execution_error", "undo removes the cloned pattern");
    }

    // --- Effects: the mixing loop (discover, insert, tweak, read back) ---
    {
        JSON r = call(service, "{\"id\": 190, \"verb\": \"list_plugins\"}");
        check(status(r) == "ok", "list_plugins ok");
        check(r["result"]["effects"].size() > 0, "effects are discoverable");
        bool sawEq = false;
        std::string eqName;
        for (size_t i = 0; i < r["result"]["effects"].size(); ++i) {
            if (r["result"]["effects"][i]["id"].asString() == "com.Aestrastudios.eq") {
                sawEq = true;
                eqName = r["result"]["effects"][i]["name"].asString();
            }
        }
        check(sawEq, "built-in EQ listed");

        // Insert by id; the response carries the slot as structured data.
        r = call(service,
                 "{\"id\": 191, \"verb\": \"add_effect\", "
                 "\"args\": {\"track\": 0, \"effect\": \"com.Aestrastudios.eq\"}}");
        check(status(r) == "ok", "add_effect by id ok");
        check(r.has("result") && r["result"]["createdId"].isNumber(),
              "add_effect returns the slot as structured data");
        const int slot = static_cast<int>(r["result"]["createdId"].asNumber());

        // Read the chain, pick a writable parameter, set it by name.
        r = call(service, "{\"id\": 192, \"verb\": \"get_effects\", \"args\": {\"track\": 0}}");
        check(status(r) == "ok", "get_effects ok");
        check(r["result"]["effects"].size() == 1, "chain shows one effect");
        JSON effect = r["result"]["effects"][0];
        check(effect["name"].asString() == eqName, "chain entry matches list_plugins name");
        check(effect["params"].size() > 0, "effect exposes parameters");
        const std::string paramName = effect["params"][0]["name"].asString();
        const double originalValue = effect["params"][0]["value"].asNumber();

        r = call(service, "{\"id\": 193, \"verb\": \"set_effect_param\", \"args\": {\"track\": 0, "
                          "\"slot\": " + std::to_string(slot) + ", \"param\": \"" + paramName +
                          "\", \"value\": 0.8}}");
        check(status(r) == "ok", "set_effect_param by name ok");
        r = call(service, "{\"id\": 194, \"verb\": \"get_effects\", \"args\": {\"track\": 0}}");
        const double paramValue = r["result"]["effects"][0]["params"][0]["value"].asNumber();
        check(paramValue > 0.79 && paramValue < 0.81, "parameter readback reflects the set");
        trackManager->getCommandHistory().undo();
        r = call(service, "{\"id\": 195, \"verb\": \"get_effects\", \"args\": {\"track\": 0}}");
        check(std::abs(r["result"]["effects"][0]["params"][0]["value"].asNumber() -
                       originalValue) < 1e-6,
              "param undo restores the original value");
        trackManager->getCommandHistory().redo();

        // Bypass round-trip.
        r = call(service, "{\"id\": 196, \"verb\": \"bypass_effect\", \"args\": {\"track\": 0, "
                          "\"slot\": " + std::to_string(slot) + ", \"state\": true}}");
        check(status(r) == "ok", "bypass_effect ok");
        r = call(service, "{\"id\": 197, \"verb\": \"get_effects\", \"args\": {\"track\": 0}}");
        check(r["result"]["effects"][0]["bypassed"].asBool(), "bypass readback true");

        // Refusals carry reasons.
        r = call(service,
                 "{\"id\": 198, \"verb\": \"add_effect\", "
                 "\"args\": {\"track\": 0, \"effect\": \"MegaVerb 9000\"}}");
        check(status(r) == "execution_error" &&
                  r["message"].asString().find("unknown effect: MegaVerb 9000") !=
                      std::string::npos,
              "unknown effect refusal names it");
        r = call(service, "{\"id\": 199, \"verb\": \"set_effect_param\", \"args\": {\"track\": 0, "
                          "\"slot\": " + std::to_string(slot) +
                          ", \"param\": \"wobble\", \"value\": 0.5}}");
        check(r["message"].asString().find("no parameter 'wobble'") != std::string::npos,
              "unknown param refusal names it");
        r = call(service,
                 "{\"id\": 200, \"verb\": \"remove_effect\", \"args\": {\"track\": 0, \"slot\": 7}}");
        check(r["message"].asString().find("no effect in slot 7") != std::string::npos,
              "empty slot refusal names it");

        // Remove and verify the chain is empty again.
        r = call(service, "{\"id\": 201, \"verb\": \"remove_effect\", \"args\": {\"track\": 0, "
                          "\"slot\": " + std::to_string(slot) + "}}");
        check(status(r) == "ok", "remove_effect ok");
        r = call(service, "{\"id\": 202, \"verb\": \"get_effects\", \"args\": {\"track\": 0}}");
        check(r["result"]["effects"].size() == 0, "chain empty after remove");
        trackManager->getCommandHistory().undo();
        r = call(service, "{\"id\": 203, \"verb\": \"get_effects\", \"args\": {\"track\": 0}}");
        check(r["result"]["effects"].size() == 1, "undo restores the removed effect");
        trackManager->getCommandHistory().undo(); // bypass
        trackManager->getCommandHistory().undo(); // param
        trackManager->getCommandHistory().undo(); // add_effect
        r = call(service, "{\"id\": 204, \"verb\": \"get_effects\", \"args\": {\"track\": 0}}");
        check(r["result"]["effects"].size() == 0, "full undo chain leaves a clean track");
    }

    // --- Render: the beat comes back as a file with signal in it ---
    {
        const std::string outPath =
            (std::filesystem::temp_directory_path() / "muse_service_test_render.wav").string();
        std::filesystem::remove(outPath);

        JSON r = call(service, "{\"id\": 80, \"verb\": \"render_pattern\"}");
        check(status(r) == "validation_error", "render_pattern without args -> validation_error");

        r = call(service, fileRequest(81, "render_pattern", "pattern", 999999.0, outPath));
        check(status(r) == "execution_error", "render_pattern unknown pattern -> execution_error");

        r = call(service, "{\"id\": 84, \"verb\": \"render_pattern\", "
                          "\"args\": {\"pattern\": -3, \"file\": \"x.wav\"}}");
        check(status(r) == "validation_error", "negative pattern id -> validation_error");

        r = call(service, fileRequest(82, "render_pattern", "pattern", kickPattern, outPath, 0.25));
        check(status(r) == "ok", "render_pattern ok");
        const double frames = r["result"]["frames"].asNumber();
        check(frames > 0.0, "render reports frames");
        // The pattern triggers the loud test click through the sampler; a
        // silent render means the pattern->unit->engine path is broken.
        check(r["result"]["peakDb"].asNumber() > -90.0,
              "rendered audio is not silent (peakDb " +
                  std::to_string(r["result"]["peakDb"].asNumber()) + ")");
        check(!engine.isTransportPlaying(), "engine transport stopped after render");

        // The file must honor the advertised contract: IEEE-float stereo at
        // the engine rate, with chunk sizes agreeing with reported frames.
        {
            std::ifstream wav(outPath, std::ios::binary);
            check(wav.good(), "rendered wav exists");
            char riff[4] = {}, wave[4] = {};
            uint32_t riffSize = 0, fmtSize = 0, byteRate = 0, dataSize = 0, sampleRate = 0;
            uint16_t format = 0, channels = 0, blockAlign = 0, bits = 0;
            char tag[4] = {};
            wav.read(riff, 4);
            wav.read(reinterpret_cast<char*>(&riffSize), 4);
            wav.read(wave, 4);
            wav.read(tag, 4); // "fmt "
            wav.read(reinterpret_cast<char*>(&fmtSize), 4);
            wav.read(reinterpret_cast<char*>(&format), 2);
            wav.read(reinterpret_cast<char*>(&channels), 2);
            wav.read(reinterpret_cast<char*>(&sampleRate), 4);
            wav.read(reinterpret_cast<char*>(&byteRate), 4);
            wav.read(reinterpret_cast<char*>(&blockAlign), 2);
            wav.read(reinterpret_cast<char*>(&bits), 2);
            wav.read(tag, 4); // "data"
            wav.read(reinterpret_cast<char*>(&dataSize), 4);
            check(std::memcmp(riff, "RIFF", 4) == 0 && std::memcmp(wave, "WAVE", 4) == 0,
                  "wav container tags");
            check(format == 3 && channels == 2 && bits == 32,
                  "wav is IEEE-float stereo 32-bit");
            check(sampleRate == 48000, "wav sample rate matches engine");
            check(dataSize == static_cast<uint32_t>(frames) * 8u,
                  "wav data size agrees with reported frames");
            check(std::filesystem::file_size(outPath) == 44u + dataSize,
                  "file size agrees with header");
            const double reported = r["result"]["durationSeconds"].asNumber();
            check(std::abs(frames / 48000.0 - reported) < 0.001,
                  "durationSeconds agrees with frames");
        }

        JSON bad = JSON::object();
        bad.set("id", JSON(83.0));
        bad.set("verb", JSON("render_pattern"));
        JSON badArgs = JSON::object();
        badArgs.set("pattern", JSON(kickPattern));
        badArgs.set("file", JSON(outPath));
        badArgs.set("wobble", JSON(1.0));
        bad.set("args", badArgs);
        r = call(service, bad.toString());
        check(status(r) == "validation_error", "render_pattern unknown arg -> validation_error");
        std::filesystem::remove(outPath);
    }

    // --- Batch: all-or-nothing groups that undo as one step ---
    {
        // Baseline state for this section.
        JSON r = call(service, "{\"id\": 90, \"verb\": \"list_tracks\"}");
        const size_t tracksBefore = r["result"]["tracks"].size();
        const bool muteBefore = r["result"]["tracks"][0]["muted"].asBool();

        r = call(service,
                 "{\"id\": 91, \"verb\": \"batch\", \"args\": {\"commands\": ["
                 "{\"verb\": \"add_track\", \"args\": {\"name\": \"Perc\"}},"
                 "{\"verb\": \"set_volume\", \"args\": {\"track\": 0, \"value\": 0.25}},"
                 "{\"verb\": \"mute_track\", \"args\": {\"track\": 0, \"state\": " +
                     std::string(muteBefore ? "false" : "true") + "}}]}}");
        check(status(r) == "ok", "batch of three mutations ok");
        check(r["result"]["count"].asNumber() == 3.0, "batch reports member count");
        check(r["undoable"].asBool(), "batch is undoable");

        r = call(service, "{\"id\": 92, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == tracksBefore + 1, "batch added the track");
        const double vol = r["result"]["tracks"][0]["volume"].asNumber();
        check(vol > 0.24 && vol < 0.26 &&
                  r["result"]["tracks"][0]["muted"].asBool() == !muteBefore,
              "batch applied volume and mute flip");

        // One undo reverts the entire batch.
        trackManager->getCommandHistory().undo();
        r = call(service, "{\"id\": 93, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == tracksBefore, "single undo removes batch track");
        check(r["result"]["tracks"][0]["muted"].asBool() == muteBefore,
              "single undo reverts mute from the same batch");

        // Dependent batch: members run against the state their predecessors
        // produced, so a batch can configure the track it just added.
        r = call(service,
                 "{\"id\": 101, \"verb\": \"batch\", \"args\": {\"commands\": ["
                 "{\"verb\": \"add_track\", \"args\": {\"name\": \"Lead\"}},"
                 "{\"verb\": \"set_pan\", \"args\": {\"track\": " +
                     std::to_string(tracksBefore) + ", \"value\": -0.5}}]}}");
        check(status(r) == "ok", "dependent batch (pan the track it added) ok");
        r = call(service, "{\"id\": 102, \"verb\": \"list_tracks\"}");
        const double newPan = r["result"]["tracks"][tracksBefore]["pan"].asNumber();
        check(newPan < -0.49 && newPan > -0.51, "dependent batch applied pan to new track");
        trackManager->getCommandHistory().undo();
        r = call(service, "{\"id\": 103, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == tracksBefore, "dependent batch undoes as one step");

        // A failing member anywhere means nothing executes — including
        // rolling back members that already ran.
        r = call(service,
                 "{\"id\": 94, \"verb\": \"batch\", \"args\": {\"commands\": ["
                 "{\"verb\": \"add_track\", \"args\": {\"name\": \"Ghost\"}},"
                 "{\"verb\": \"set_volume\", \"args\": {\"track\": 0, \"value\": 9.0}}"
                 "]}}");
        check(status(r) == "validation_error" &&
                  r["message"].asString().find("commands[1]") != std::string::npos,
              "invalid member -> validation_error naming the index");
        r = call(service, "{\"id\": 95, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == tracksBefore,
              "failed batch executed nothing (no Ghost track)");

        // Contract: shape errors and non-mutation members are rejected.
        r = call(service, "{\"id\": 96, \"verb\": \"batch\", \"args\": {\"commands\": []}}");
        check(status(r) == "validation_error", "empty batch -> validation_error");

        r = call(service,
                 "{\"id\": 97, \"verb\": \"batch\", \"args\": {\"commands\": ["
                 "{\"verb\": \"list_tracks\"}]}}");
        check(status(r) == "validation_error", "query verb inside batch -> validation_error");

        r = call(service,
                 "{\"id\": 98, \"verb\": \"batch\", \"args\": {\"commands\": ["
                 "{\"verb\": \"batch\", \"args\": {\"commands\": []}}]}}");
        check(status(r) == "validation_error", "nested batch -> validation_error");

        r = call(service, "{\"id\": 99, \"verb\": \"batch\"}");
        check(status(r) == "validation_error", "batch without args -> validation_error");

        r = call(service,
                 "{\"id\": 100, \"verb\": \"batch\", \"args\": {\"commands\": ["
                 "{\"verb\": \"warp_reality\"}]}}");
        check(status(r) == "parse_error" &&
                  r["message"].asString().find("commands[0]") != std::string::npos,
              "unknown verb inside batch -> parse_error naming the index");
    }

    // --- Arrange: pattern onto the timeline as a clip ---
    {
        const std::string p = std::to_string(static_cast<long long>(kickPattern));

        // Earlier sections leave track 0 muted (the typed-args mute survived
        // the delete/undo reorder); the arranged pattern routes here, so an
        // accidentally muted target would make the song render silent for
        // reasons unrelated to the arrange path.
        JSON unmute = call(service,
                           "{\"id\": 109, \"verb\": \"mute_track\", "
                           "\"args\": {\"track\": 0, \"state\": false}}");
        check(status(unmute) == "ok", "unmute render target track");

        JSON r = call(service, "{\"id\": 110, \"verb\": \"list_clips\"}");
        const size_t lanesBefore = r["result"]["lanes"].size();

        r = call(service, "{\"id\": 111, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"track\": 0, \"start\": 0}}");
        check(status(r) == "ok", "arrange_pattern ok");

        r = call(service, "{\"id\": 112, \"verb\": \"list_clips\"}");
        check(r["result"]["lanes"].size() > lanesBefore, "arrange created the missing lane");
        check(r["result"]["lanes"][0]["clips"].size() == 1, "lane carries one clip");
        check(r["result"]["lanes"][0]["clips"][0]["pattern"].asNumber() == kickPattern,
              "clip is bound to the pattern");
        check(r["result"]["lanes"][0]["clips"][0]["durationBeats"].asNumber() > 0.0,
              "clip sized from pattern length");

        r = call(service, "{\"id\": 113, \"verb\": \"list_units\"}");
        check(r["result"]["units"][0]["timelineLane"].asNumber() == 0.0,
              "pattern's unit routed to timeline lane 0");

        r = call(service, "{\"id\": 114, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"track\": 0, \"start\": 8}}");
        check(status(r) == "ok", "second arrange at beat 8 ok");
        r = call(service, "{\"id\": 115, \"verb\": \"list_clips\"}");
        check(r["result"]["lanes"][0]["clips"].size() == 2, "two clips on the lane");
        check(r["result"]["lanes"][0]["clips"][1]["startBeat"].asNumber() == 8.0,
              "second clip starts at the requested beat");

        // Conflict: the pattern's unit is routed to lane 0; arranging it on
        // another track would silently reroute the clips already placed.
        r = call(service, "{\"id\": 130, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"track\": 1, \"start\": 0}}");
        check(status(r) == "execution_error",
              "arrange onto a conflicting track -> execution_error");

        // Undo unwinds the whole gesture: clip, unit routing, created lane.
        trackManager->getCommandHistory().undo();
        trackManager->getCommandHistory().undo();
        r = call(service, "{\"id\": 116, \"verb\": \"list_clips\"}");
        check(r["result"]["lanes"].size() == lanesBefore, "undo removes the created lane");
        r = call(service, "{\"id\": 117, \"verb\": \"list_units\"}");
        check(r["result"]["units"][0]["timelineLane"].asNumber() == -1.0,
              "undo restores the unit's preview routing");

        // Contract: bad targets never build.
        r = call(service,
                 "{\"id\": 118, \"verb\": \"arrange_pattern\", "
                 "\"args\": {\"pattern\": 999999, \"track\": 0, \"start\": 0}}");
        check(status(r) == "execution_error", "arrange unknown pattern -> execution_error");
        r = call(service, "{\"id\": 119, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"track\": 99, \"start\": 0}}");
        check(status(r) == "execution_error", "arrange onto missing channel -> execution_error");
    }

    // --- Render the arrangement: the song comes back as audio ---
    {
        const std::string p = std::to_string(static_cast<long long>(kickPattern));
        const std::string outPath =
            (std::filesystem::temp_directory_path() / "muse_service_test_song.wav").string();
        std::filesystem::remove(outPath);

        JSON r = call(service, "{\"id\": 120, \"verb\": \"render_song\"}");
        check(status(r) == "validation_error", "render_song without args -> validation_error");

        r = call(service, fileRequest(121, "render_song", "pattern", 1.0, outPath));
        check(status(r) == "validation_error", "render_song unknown arg -> validation_error");

        // The arrange section undid its clips: the timeline is empty here.
        JSON emptyReq = JSON::object();
        emptyReq.set("id", JSON(124.0));
        emptyReq.set("verb", JSON("render_song"));
        JSON emptyArgs = JSON::object();
        emptyArgs.set("file", JSON(outPath));
        emptyReq.set("args", emptyArgs);
        r = call(service, emptyReq.toString());
        check(status(r) == "execution_error", "empty timeline -> execution_error");
        check(!std::filesystem::exists(outPath), "empty timeline writes no file");

        r = call(service, "{\"id\": 122, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"track\": 0, \"start\": 0}}");
        check(status(r) == "ok", "re-arrange for render ok");
        r = call(service, "{\"id\": 125, \"verb\": \"arrange_pattern\", \"args\": {\"pattern\": " +
                              p + ", \"track\": 0, \"start\": 8}}");
        check(status(r) == "ok", "late clip at beat 8 for render ok");

        // Unwritable destination must fail loudly, not report a phantom file.
        JSON badDest = JSON::object();
        badDest.set("id", JSON(126.0));
        badDest.set("verb", JSON("render_song"));
        JSON badDestArgs = JSON::object();
        badDestArgs.set("file", JSON("/nonexistent_muse_dir/song.wav"));
        badDest.set("args", badDestArgs);
        r = call(service, badDest.toString());
        check(status(r) == "execution_error", "unwritable destination -> execution_error");

        JSON req = JSON::object();
        req.set("id", JSON(123.0));
        req.set("verb", JSON("render_song"));
        JSON reqArgs = JSON::object();
        reqArgs.set("file", JSON(outPath));
        reqArgs.set("tail", JSON(0.25));
        req.set("args", reqArgs);
        r = call(service, req.toString());
        check(status(r) == "ok", "render_song ok");
        check(r["result"]["frames"].asNumber() > 0.0, "song render reports frames");
        // Two 8-beat clips at 0 and 8 = 16 beats; at 120 BPM that's 8 s. A
        // render that ignored the late clip would come back shorter.
        check(r["result"]["durationSeconds"].asNumber() >= 8.0,
              "render duration reaches the late clip");
        check(std::filesystem::exists(outPath) && std::filesystem::file_size(outPath) > 44,
              "song wav exists with data");
        // The timeline clip triggers the loud test click through the routed
        // unit; silence means the clip->schedule->unit->track path is broken.
        check(r["result"]["peakDb"].asNumber() > -90.0,
              "song render is not silent (peakDb " +
                  std::to_string(r["result"]["peakDb"].asNumber()) + ")");
        check(!engine.isTransportPlaying(), "engine transport stopped after song render");
        std::filesystem::remove(outPath);
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
