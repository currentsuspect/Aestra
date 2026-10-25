// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Every Muse verb that creates something says what it created (createdId +
// createdKind), alone and inside a batch, and duplicate_clip's bar is a bar.
//
// Driving the running app, an agent had to diff list_units after every
// add_unit to learn the new id, and duplicate_clip --bar 9 put the copy at
// beat 9 — on top of the clips it was meant to follow.
#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
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
        (std::filesystem::temp_directory_path() / "muse_created_ids_sample.wav").string();
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

JSON obj() { return JSON::object(); }

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

    // --- offline renders pause and resume the host stream, on every exit path --
    {
        int pauses = 0;
        int resumes = 0;
        service.setOfflineRenderStreamHooks([&] { ++pauses; return true; }, [&] { ++resumes; });
        JSON renderArgs = obj();
        renderArgs.set("file", JSON((std::filesystem::temp_directory_path() / "muse_created_ids_empty.wav").string()));
        JSON failed = call(service, req(90, "render_song", renderArgs).toString());
        check(status(failed) == "execution_error", "rendering an empty timeline fails");
        check(pauses == 1 && resumes == 1, "a failed render still resumes the stream it paused (" +
                                               std::to_string(pauses) + " pause, " + std::to_string(resumes) +
                                               " resume)");
        JSON bpm = obj();
        bpm.set("value", JSON(120.0));
        call(service, req(91, "set_bpm", bpm).toString());
        check(pauses == 1 && resumes == 1, "a non-render verb does not touch the stream");
        // A stream that was not running is not resumed.
        service.setOfflineRenderStreamHooks([&] { ++pauses; return false; }, [&] { ++resumes; });
        call(service, req(92, "render_song", renderArgs).toString());
        check(pauses == 2 && resumes == 1, "a stream that was not running is left stopped");
        service.setOfflineRenderStreamHooks(nullptr, nullptr);
    }

    // --- add_unit ---------------------------------------------------------------
    JSON a = obj();
    a.set("name", JSON("Kick"));
    JSON r = call(service, req(1, "add_unit", a).toString());
    check(status(r) == "ok" && r.has("result") && r["result"]["createdKind"].asString() == "unit",
          "add_unit reports a created unit");
    const double unitId = r["result"]["createdId"].asNumber();
    JSON units = call(service, "{\"id\": 2, \"verb\": \"list_units\"}");
    check(units["result"]["units"][0]["id"].asNumber() == unitId, "the created unit id is the one list_units shows");

    // --- add_track --------------------------------------------------------------
    a = obj();
    a.set("name", JSON("Bus"));
    r = call(service, req(3, "add_track", a).toString());
    check(status(r) == "ok" && r["result"]["createdKind"].asString() == "track", "add_track reports a created track");
    const double trackId = r["result"]["createdId"].asNumber();
    JSON tracks = call(service, "{\"id\": 4, \"verb\": \"list_tracks\"}");
    bool trackFound = false;
    for (size_t i = 0; i < tracks["result"]["tracks"].size(); ++i) {
        if (tracks["result"]["tracks"][i]["id"].asNumber() == trackId) trackFound = true;
    }
    check(trackFound, "the created track id is in list_tracks");

    // --- add_lane ---------------------------------------------------------------
    a = obj();
    a.set("name", JSON("Sample"));
    r = call(service, req(41, "add_lane", a).toString());
    check(status(r) == "ok" && r["result"]["createdKind"].asString() == "lane" && r["result"]["createdId"].isString(),
          "add_lane reports the lane id as a UUID string");
    const std::string laneId = r["result"]["createdId"].asString();
    JSON lanes = call(service, "{\"id\": 42, \"verb\": \"list_clips\"}");
    check(lanes["result"]["lanes"].size() >= 1 && lanes["result"]["lanes"][0]["id"].asString() == laneId,
          "the created lane id is the one list_clips shows");

    // --- add_clip, duplicate_clip ------------------------------------------------
    a = obj();
    a.set("track", JSON(0.0));
    a.set("file", JSON(sample));
    a.set("bar", JSON(1.0));
    r = call(service, req(5, "add_clip", a).toString());
    check(status(r) == "ok" && r["result"]["createdKind"].asString() == "clip" && r["result"]["createdId"].isString(),
          "add_clip reports the clip id as the UUID string list_clips uses");
    const std::string clipId = r["result"]["createdId"].asString();

    a = obj();
    a.set("id", JSON(clipId));
    a.set("bar", JSON(3.0));
    r = call(service, req(6, "duplicate_clip", a).toString());
    check(status(r) == "ok" && r["result"]["createdKind"].asString() == "clip", "duplicate_clip reports the copy");
    const std::string copyId = r["result"]["createdId"].asString();
    JSON clips = call(service, "{\"id\": 7, \"verb\": \"list_clips\"}");
    double copyStart = -1.0;
    bool originalFound = false;
    for (size_t li = 0; li < clips["result"]["lanes"].size(); ++li) {
        JSON lane = clips["result"]["lanes"][li];
        for (size_t ci = 0; ci < lane["clips"].size(); ++ci) {
            JSON c = lane["clips"][ci];
            if (c["id"].asString() == copyId) copyStart = c["startBeat"].asNumber();
            if (c["id"].asString() == clipId) originalFound = true;
        }
    }
    check(originalFound, "the add_clip id addresses the clip in list_clips");
    check(copyStart == 8.0, "duplicate_clip --bar 3 starts the copy at beat 8, not beat 3 (got " +
                                std::to_string(copyStart) + ")");
    a = obj();
    a.set("id", JSON(clipId));
    a.set("bar", JSON(0.0));
    r = call(service, req(8, "duplicate_clip", a).toString());
    check(status(r) != "ok", "bar 0 is refused (bars are 1-based)");

    // --- arrange_pattern ----------------------------------------------------------
    const double pattern = units["result"]["units"][0]["defaultPatternId"].asNumber();
    a = obj();
    a.set("pattern", JSON(pattern));
    a.set("track", JSON(1.0));
    a.set("start", JSON(0.0));
    r = call(service, req(9, "arrange_pattern", a).toString());
    check(status(r) == "ok" && r["result"]["createdKind"].asString() == "clip", "arrange_pattern reports the clip");

    // --- clone_pattern and add_effect keep their numeric ids ---------------------
    a = obj();
    a.set("pattern", JSON(pattern));
    r = call(service, req(10, "clone_pattern", a).toString());
    check(status(r) == "ok" && r["result"]["createdKind"].asString() == "pattern" && r["result"]["createdId"].isNumber(),
          "clone_pattern still reports a numeric pattern id");

    // --- batch: every member's creation, in order ---------------------------------
    JSON cmds = JSON::array();
    JSON m1 = obj();
    m1.set("verb", JSON("add_unit"));
    JSON m1a = obj();
    m1a.set("name", JSON("Snare"));
    m1.set("args", m1a);
    cmds.push(m1);
    JSON m2 = obj();
    m2.set("verb", JSON("set_bpm"));
    JSON m2a = obj();
    m2a.set("value", JSON(96.0));
    m2.set("args", m2a);
    cmds.push(m2);
    JSON m3 = obj();
    m3.set("verb", JSON("add_track"));
    JSON m3a = obj();
    m3a.set("name", JSON("Drums"));
    m3.set("args", m3a);
    cmds.push(m3);
    a = obj();
    a.set("commands", cmds);
    r = call(service, req(11, "batch", a).toString());
    check(status(r) == "ok", "batch ok");
    JSON created = r["result"]["created"];
    check(created.size() == 2, "a batch lists the two members that created something (not set_bpm)");
    check(created.size() == 2 && created[0]["index"].asNumber() == 0 && created[0]["createdKind"].asString() == "unit" &&
              created[1]["index"].asNumber() == 2 && created[1]["createdKind"].asString() == "track",
          "each entry names its member index and kind");

    // A verb that creates nothing still answers without a result.
    a = obj();
    a.set("value", JSON(100.0));
    r = call(service, req(12, "set_bpm", a).toString());
    check(status(r) == "ok" && !r.has("result"), "set_bpm reports no created object");

    std::filesystem::remove(sample);
    if (g_failures == 0) {
        std::cout << "MuseCreatedIdsTest: all checks passed\n";
        return 0;
    }
    std::cout << "MuseCreatedIdsTest: " << g_failures << " failure(s)\n";
    return 1;
}
