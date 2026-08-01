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
#include "Commands/ImportAudioClipCommand.h"
#include "IO/MiniAudioDecoder.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

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

/**
 * Read back a float32 WAV that render_song produced, so the assertions are
 * about audio that actually left the engine rather than about a buffer sitting
 * in SourceManager.
 */
bool readFloatWav(const std::string& path, std::vector<float>& out, uint32_t& channels) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char riff[4], wave[4];
    if (std::fread(riff, 1, 4, f) != 4) { std::fclose(f); return false; }
    uint32_t chunkSize = 0;
    if (std::fread(&chunkSize, 4, 1, f) != 1) { std::fclose(f); return false; }
    if (std::fread(wave, 1, 4, f) != 4) { std::fclose(f); return false; }
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) { std::fclose(f); return false; }

    channels = 0;
    uint16_t bits = 0;
    while (true) {
        char id[4];
        uint32_t size = 0;
        if (std::fread(id, 1, 4, f) != 4) break;
        if (std::fread(&size, 4, 1, f) != 1) break;
        if (std::memcmp(id, "fmt ", 4) == 0) {
            uint16_t fmt = 0, ch = 0;
            uint32_t rate = 0, byteRate = 0;
            uint16_t block = 0;
            std::fread(&fmt, 2, 1, f);
            std::fread(&ch, 2, 1, f);
            std::fread(&rate, 4, 1, f);
            std::fread(&byteRate, 4, 1, f);
            std::fread(&block, 2, 1, f);
            std::fread(&bits, 2, 1, f);
            channels = ch;
            if (size > 16) std::fseek(f, static_cast<long>(size - 16), SEEK_CUR);
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (bits != 32 || channels == 0) { std::fclose(f); return false; }
            out.resize(size / sizeof(float));
            const size_t read = std::fread(out.data(), sizeof(float), out.size(), f);
            out.resize(read);
            std::fclose(f);
            return !out.empty();
        } else {
            std::fseek(f, static_cast<long>(size + (size & 1u)), SEEK_CUR);
        }
    }
    std::fclose(f);
    return false;
}

/** Energy in the first or last half of an interleaved buffer. */
double halfEnergyOf(const std::vector<float>& data, uint32_t channels, bool front) {
    if (channels == 0 || data.empty()) return 0.0;
    const size_t frames = data.size() / channels;
    const size_t half = frames / 2;
    const size_t begin = front ? 0 : half;
    const size_t end = front ? half : frames;
    double energy = 0.0;
    for (size_t f = begin; f < end; ++f) {
        const double s = data[f * channels];
        energy += s * s;
    }
    return energy;
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

/**
 * The failure the Muse verbs cannot reach: the decode succeeds, the source is
 * registered, and then placement fails. Driven at the command level because
 * add_clip validates the lane before building, so the verb can never get here.
 */
void testPlacementFailureLeavesNoOrphanSource(TrackManager& tm, const std::string& wavPath) {
    const size_t sourcesBefore = tm.getSourceManager().getAllSourceIDs().size();
    const size_t patternsBefore = tm.getPatternManager().getAllPatterns().size();

    std::vector<float> decoded;
    uint32_t sampleRate = 0;
    uint32_t numChannels = 0;
    if (!Aestra::Audio::decodeAudioFile(wavPath, decoded, sampleRate, numChannels)) {
        check(false, "test fixture decodes");
        return;
    }
    auto buffer = std::make_shared<Aestra::Audio::AudioBufferData>();
    buffer->interleavedData = std::move(decoded);
    buffer->sampleRate = sampleRate;
    buffer->numChannels = numChannels;
    buffer->numFrames = buffer->interleavedData.size() / numChannels;

    // A lane id that belongs to no lane: addClip cannot place the clip.
    Aestra::Audio::PlaylistLaneID nowhere = Aestra::Audio::PlaylistLaneID::generate();

    Aestra::Audio::ImportAudioClipCommand command(tm, nowhere, wavPath, "orphan-probe", buffer, 0.0, 0.5, 1.0);
    command.execute();

    check(!command.isUndoable(), "an import that cannot place its clip reports failure");
    check(tm.getSourceManager().getAllSourceIDs().size() == sourcesBefore,
          "a failed placement withdraws the source it registered");
    check(tm.getPatternManager().getAllPatterns().size() == patternsBefore,
          "a failed placement leaves no orphan pattern");
}

/**
 * A source the project already had must survive a failed import: this command
 * did not introduce it, so it is not this command's to withdraw.
 */
void testPreexistingSourceSurvivesFailure(TrackManager& tm, const std::string& wavPath) {
    // Put the file in the project first, the way a previous import would have.
    auto seed = std::make_shared<Aestra::Audio::AudioBufferData>();
    seed->sampleRate = 48000;
    seed->numChannels = 2;
    seed->numFrames = 128;
    seed->interleavedData.assign(128 * 2, 0.25f);
    const auto seeded = tm.getSourceManager().createRecordedSource(wavPath, "already-here", seed);
    check(seeded.isValid(), "the fixture source registers");

    const size_t sourcesBefore = tm.getSourceManager().getAllSourceIDs().size();

    auto buffer = std::make_shared<Aestra::Audio::AudioBufferData>(*seed);
    Aestra::Audio::PlaylistLaneID nowhere = Aestra::Audio::PlaylistLaneID::generate();
    Aestra::Audio::ImportAudioClipCommand command(tm, nowhere, wavPath, "orphan-probe", buffer, 0.0, 0.5, 1.0);
    command.execute();

    check(!command.isUndoable(), "the import still fails");
    check(tm.getSourceManager().getAllSourceIDs().size() == sourcesBefore,
          "a source the project already had is not withdrawn");
    check(tm.getSourceManager().getSource(seeded) != nullptr, "the pre-existing source is still addressable");
}

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
        // Nothing may reach the project before the command runs: the factory
        // decodes and validates, it does not register.
        check(trackManager->getSourceManager().getAllSourceIDs().empty(),
              "a refused import registers no source either");
        check(trackManager->getPatternManager().getAllPatterns().empty(),
              "a refused import creates no pattern");
    }

    // --- an undecodable file is refused just as cleanly ---------------------
    {
        const std::string junkPath = (std::filesystem::temp_directory_path() / "muse_clip_junk.wav").string();
        std::FILE* junk = std::fopen(junkPath.c_str(), "wb");
        if (junk) {
            const char garbage[] = "this is not audio";
            std::fwrite(garbage, 1, sizeof(garbage), junk);
            std::fclose(junk);
        }
        JSON args = JSON::object();
        args.set("track", JSON(0.0));
        args.set("file", JSON(junkPath));
        args.set("bar", JSON(1.0));
        JSON r = call(service, request("add_clip", args));
        check(status(r) != "ok", "add_clip on an undecodable file refuses");
        check(trackManager->getSourceManager().getAllSourceIDs().empty(),
              "an undecodable file leaves no source behind");

        std::error_code junkEc;
        std::filesystem::remove(junkPath, junkEc);
    }

    // --- bar is 1-based, and says so ---------------------------------------
    {
        JSON args = JSON::object();
        args.set("track", JSON(0.0));
        args.set("file", JSON(wavPath));
        args.set("bar", JSON(0.0));
        JSON r = call(service, request("add_clip", args));
        // bar 0 would place the clip at beat -4; refuse it rather than build
        // a clip at a negative timeline position.
        check(status(r) != "ok", "add_clip refuses bar 0 instead of placing at a negative beat");
        check(trackManager->getSourceManager().getAllSourceIDs().empty(),
              "a refused bar leaves no source behind");
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

    // --- atomicity beyond what the verbs can reach --------------------------
    {
        // A fresh project so the counts mean something.
        auto probeManager = std::make_shared<TrackManager>();
        probeManager->getUnitManager().setPatternManager(&probeManager->getPatternManager());
        testPlacementFailureLeavesNoOrphanSource(*probeManager, wavPath);

        auto seededManager = std::make_shared<TrackManager>();
        seededManager->getUnitManager().setPatternManager(&seededManager->getPatternManager());
        testPreexistingSourceSurvivesFailure(*seededManager, wavPath);
    }

    // --- render_song proves the audio, not just the model -------------------
    if (!clipId.empty()) {
        const std::string beforePath = (std::filesystem::temp_directory_path() / "muse_rt_before.wav").string();
        const std::string afterPath = (std::filesystem::temp_directory_path() / "muse_rt_after.wav").string();

        JSON args = JSON::object();
        args.set("file", JSON(beforePath));
        args.set("tail", JSON(0.0));
        JSON r = call(service, request("render_song", args));
        check(status(r) == "ok", "render_song bounces the arranged timeline");

        std::vector<float> before;
        uint32_t beforeChannels = 0;
        check(readFloatWav(beforePath, before, beforeChannels), "the rendered mix reads back");
        const double frontBefore = halfEnergyOf(before, beforeChannels, true);
        const double backBefore = halfEnergyOf(before, beforeChannels, false);
        check(frontBefore > backBefore * 2.0, "the rendered mix is front-loaded, like the imported file");

        // Reverse addressed by the id list_clips reported.
        r = call(service, verbWithId("reverse_clip", clipId));
        check(status(r) == "ok", "reverse_clip runs against the listed id");

        args = JSON::object();
        args.set("file", JSON(afterPath));
        args.set("tail", JSON(0.0));
        r = call(service, request("render_song", args));
        check(status(r) == "ok", "render_song bounces the reversed timeline");

        std::vector<float> after;
        uint32_t afterChannels = 0;
        check(readFloatWav(afterPath, after, afterChannels), "the reversed mix reads back");
        const double frontAfter = halfEnergyOf(after, afterChannels, true);
        const double backAfter = halfEnergyOf(after, afterChannels, false);
        // The whole point of the round trip: audio that actually left the
        // engine changed, in the direction reverse implies.
        check(backAfter > frontAfter * 2.0, "the reversed mix is back-loaded");

        r = call(service, request("undo", JSON::object()));
        check(status(r) == "ok", "the reverse undoes");

        std::error_code renderEc;
        std::filesystem::remove(beforePath, renderEc);
        std::filesystem::remove(afterPath, renderEc);
    }

    // --- commit_clip_edits is audibly equivalent ----------------------------
    if (!clipId.empty()) {
        const std::string preCommit = (std::filesystem::temp_directory_path() / "muse_rt_precommit.wav").string();
        const std::string postCommit = (std::filesystem::temp_directory_path() / "muse_rt_postcommit.wav").string();

        // Something to bake: a clear gain change.
        Aestra::Audio::AestraUUID parsedId;
        Aestra::Audio::AestraUUID::tryParse(clipId, parsedId);
        const Aestra::Audio::ClipInstanceID id(parsedId);
        if (auto* clip = trackManager->getPlaylistModel().getClip(id)) {
            Aestra::Audio::ClipEdits edits = clip->edits;
            edits.gainLinear = 0.35f;
            trackManager->getPlaylistModel().setClipEdits(id, edits);
        }

        JSON args = JSON::object();
        args.set("file", JSON(preCommit));
        args.set("tail", JSON(0.0));
        JSON r = call(service, request("render_song", args));
        check(status(r) == "ok", "render_song bounces before the commit");
        std::vector<float> pre;
        uint32_t preChannels = 0;
        check(readFloatWav(preCommit, pre, preChannels), "the pre-commit mix reads back");

        r = call(service, verbWithId("commit_clip_edits", clipId));
        check(status(r) == "ok", "commit_clip_edits runs against the listed id");

        args = JSON::object();
        args.set("file", JSON(postCommit));
        args.set("tail", JSON(0.0));
        r = call(service, request("render_song", args));
        check(status(r) == "ok", "render_song bounces after the commit");
        std::vector<float> post;
        uint32_t postChannels = 0;
        check(readFloatWav(postCommit, post, postChannels), "the post-commit mix reads back");

        // The defining invariant: committing clip-local edits must not change
        // what the timeline sounds like.
        check(preChannels == postChannels, "the commit does not change the channel count");
        const double preEnergy = halfEnergyOf(pre, preChannels, true) + halfEnergyOf(pre, preChannels, false);
        const double postEnergy = halfEnergyOf(post, postChannels, true) + halfEnergyOf(post, postChannels, false);
        const double denominator = std::max(preEnergy, 1.0e-9);
        check(std::fabs(preEnergy - postEnergy) / denominator < 0.02,
              "committing clip edits leaves the rendered mix audibly equivalent");

        std::error_code renderEc;
        std::filesystem::remove(preCommit, renderEc);
        std::filesystem::remove(postCommit, renderEc);
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
