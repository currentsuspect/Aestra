// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Muse audio-clip round-trip integrity.
//
// The invariant: anything Muse creates or lists can be addressed unchanged and
// produces the same real project object the GUI would. Concretely —
//
//   blank project -> add_lane -> add_clip --file -> list_clips
//     -> reuse the returned id verbatim -> act on the clip -> undo/redo
//
// Before this, list_clips printed a 32-hex-char id that every clip verb
// rejected as a non-integer, and add_clip recorded the file path as a name
// without decoding it, leaving a silent clip with no pattern.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <variant>

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
    return JSON::parse(service.handleRequest(request));
}

std::string status(JSON& response) {
    return response.has("status") ? response["status"].asString() : "<missing>";
}

std::string message(JSON& response) {
    return response.has("message") ? response["message"].asString() : "";
}

/** A request whose args are built as JSON, so Windows paths survive escaping. */
std::string request(const std::string& verb, const JSON& args) {
    JSON req = JSON::object();
    req.set("id", JSON(1.0));
    req.set("verb", JSON(verb));
    req.set("args", args);
    return req.toString();
}

std::string verbWithId(const std::string& verb, const std::string& clipId) {
    JSON args = JSON::object();
    args.set("id", JSON(clipId));
    return request(verb, args);
}

/**
 * Front-loaded float32 stereo WAV: loud attack, near-silent tail. Reversing it
 * is measurable rather than merely plausible.
 */
std::string writeDecayWav() {
    const std::string path = (std::filesystem::temp_directory_path() / "muse_clip_roundtrip.wav").string();
    const uint32_t sampleRate = 48000;
    const uint16_t channels = 2;
    const uint32_t numFrames = 24000; // 0.5 s
    const uint32_t dataBytes = numFrames * channels * sizeof(float);

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return "";
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(3); // IEEE float
    u16(channels);
    u32(sampleRate);
    u32(sampleRate * channels * 4);
    u16(static_cast<uint16_t>(channels * 4));
    u16(32);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (uint32_t i = 0; i < numFrames; ++i) {
        const float env = 1.0f - static_cast<float>(i) / static_cast<float>(numFrames);
        const float s = env * env * 0.8f;
        std::fwrite(&s, sizeof(float), 1, f);
        std::fwrite(&s, sizeof(float), 1, f);
    }
    std::fclose(f);
    return path;
}

/** Energy in the first or last half of whatever audio the clip resolves to. */
double halfEnergy(const Aestra::Audio::AudioBufferData& buffer, bool front) {
    const uint64_t half = buffer.numFrames / 2;
    const uint64_t begin = front ? 0 : half;
    const uint64_t end = front ? half : buffer.numFrames;
    double energy = 0.0;
    for (uint64_t f = begin; f < end; ++f) {
        const double s = buffer.interleavedData[f * buffer.numChannels];
        energy += s * s;
    }
    return energy;
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

    const std::string wavPath = writeDecayWav();
    if (wavPath.empty()) {
        std::cout << "FAIL: could not write the test wav\n";
        return 1;
    }

    // --- add_lane: a blank project can be given somewhere to put clips ------
    {
        JSON args = JSON::object();
        args.set("name", JSON(std::string("QA")));
        JSON r = call(service, request("add_lane", args));
        check(status(r) == "ok", "add_lane creates a playlist lane");
    }

    // --- add_clip: really imports, or changes nothing -----------------------
    {
        JSON args = JSON::object();
        args.set("track", JSON(0.0));
        args.set("file", JSON(std::string("/nonexistent/definitely_not_here.wav")));
        args.set("bar", JSON(1.0));
        JSON r = call(service, request("add_clip", args));
        check(status(r) != "ok", "add_clip on a missing file refuses");
        check(message(r).find("no such audio file") != std::string::npos,
              "add_clip names the missing file in its error");

        JSON clips = call(service, request("list_clips", JSON::object()));
        const bool noClip = !clips.has("result") || !clips["result"].has("lanes") ||
                            clips["result"]["lanes"][0]["clips"].size() == 0;
        check(noClip, "a refused import leaves no clip behind");
    }

    std::string clipId;
    {
        JSON args = JSON::object();
        args.set("track", JSON(0.0));
        args.set("file", JSON(wavPath));
        args.set("bar", JSON(1.0));
        JSON r = call(service, request("add_clip", args));
        check(status(r) == "ok", "add_clip imports a real audio file");
    }

    // --- list_clips reports an id, and the clip is a real audio object ------
    {
        JSON r = call(service, request("list_clips", JSON::object()));
        JSON lanes = r["result"]["lanes"];
        check(lanes.size() == 1, "the imported clip is on its lane");
        if (lanes.size() == 1 && lanes[0]["clips"].size() == 1) {
            JSON clip = lanes[0]["clips"][0];
            clipId = clip["id"].asString();
            check(!clipId.empty(), "list_clips reports a clip id");
            // The bug this slice exists for: a clip that names a file but
            // carries no pattern is silent and structurally invalid.
            const double pattern = clip.has("pattern") ? clip["pattern"].asNumber() : 0.0;
            check(pattern != 0.0, "the imported clip carries a real audio pattern");
        } else {
            check(false, "exactly one clip was imported");
        }
    }

    // --- the reported id is accepted verbatim by the clip verbs -------------
    if (!clipId.empty()) {
        // Every clip verb shares one id parser; prove the listed id reaches it
        // rather than being rejected before the command is ever built.
        JSON r = call(service, verbWithId("delete_clip", clipId));
        check(status(r) == "ok", "a listed clip id is accepted verbatim by delete_clip");

        r = call(service, request("undo", JSON::object()));
        check(status(r) == "ok", "the delete undoes");

        // A malformed id is refused as an id, not silently truncated.
        r = call(service, verbWithId("delete_clip", std::string("12")));
        check(status(r) != "ok", "a non-canonical id is refused");
        r = call(service, verbWithId("delete_clip", std::string("00000000000000000000000000000000")));
        check(status(r) != "ok", "the null id is refused");
    }

    // --- undo/redo of the import restores and reuses -------------------------
    if (!clipId.empty()) {
        JSON r = call(service, request("undo", JSON::object()));
        check(status(r) == "ok", "the import undoes");

        JSON clips = call(service, request("list_clips", JSON::object()));
        const bool empty = clips["result"]["lanes"][0]["clips"].size() == 0;
        check(empty, "undoing the import removes the clip");

        r = call(service, request("redo", JSON::object()));
        check(status(r) == "ok", "the import redoes");

        clips = call(service, request("list_clips", JSON::object()));
        check(clips["result"]["lanes"][0]["clips"].size() == 1, "redo restores the clip");
        if (clips["result"]["lanes"][0]["clips"].size() == 1) {
            check(clips["result"]["lanes"][0]["clips"][0]["id"].asString() == clipId,
                  "redo restores the clip under the same id");
        }
    }

    // --- the imported audio is really the file's audio -----------------------
    if (!clipId.empty()) {
        Aestra::Audio::ClipInstanceID id;
        Aestra::Audio::AestraUUID parsed;
        check(Aestra::Audio::AestraUUID::tryParse(clipId, parsed), "the reported id parses as a canonical id");
        id = Aestra::Audio::ClipInstanceID(parsed);

        const auto* clip = trackManager->getPlaylistModel().getClip(id);
        check(clip != nullptr, "the reported id resolves to the very clip that was created");
        if (clip) {
            // clip -> pattern -> audio payload -> decoded source buffer
            const auto* pattern = trackManager->getPatternManager().getPattern(clip->patternId);
            check(pattern != nullptr && pattern->isAudio(), "the clip points at an audio pattern");
            if (pattern && pattern->isAudio()) {
                const auto& payload = std::get<Aestra::Audio::AudioSlicePayload>(pattern->payload);
                const auto* source = trackManager->getSourceManager().getSource(payload.audioSourceId);
                check(source != nullptr, "the audio pattern points at a registered source");
                auto buffer = source ? source->getSharedBuffer() : nullptr;
                check(buffer && buffer->isValid(), "the source carries decoded audio, not just a path");
                if (buffer && buffer->isValid()) {
                    check(buffer->numFrames > 0, "the decoded audio has frames");
                    check(halfEnergy(*buffer, true) > halfEnergy(*buffer, false) * 2.0,
                          "the decoded audio is the front-loaded file that was imported");
                }
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove(wavPath, ec);

    if (g_failures != 0) {
        std::cout << "\nFAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "\nAll Muse clip round-trip checks passed.\n";
    return 0;
}
