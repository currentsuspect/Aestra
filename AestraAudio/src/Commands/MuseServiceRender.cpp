// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Offline render actions: render_song and render_pattern.
//

//
// Part of the split that took MuseService.cpp from one 2200-line dispatch
// chain to one file per family. See Commands/MuseServiceInternal.h for why, and
// for the contract these handlers share.
//
// The body below is the text it was before the move, dedented by four spaces
// and otherwise untouched. If you change a verb's behaviour, change it here.

#include "Commands/MuseServiceInternal.h"
#include "Commands/MuseFileDigest.h"
#include "Core/AudioEngine.h"
#include "Core/PlaybackGraphController.h"
#include "IO/AudioExporter.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

// Inside Aestra::Audio, as it was in MuseService.cpp: JSON is Aestra::JSON, so
// these helpers cannot sit at global scope and still name it unqualified.

namespace {

// Interleaved stereo float32 WAV — the format the offline test renderer uses.
bool writeFloat32Wav(const std::string& path, const std::vector<float>& samples,
                     uint32_t sampleRate) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    const uint16_t channels = 2;
    const uint16_t bitsPerSample = 32;
    const uint16_t blockAlign = channels * bitsPerSample / 8;

    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    file.write("RIFF", 4);
    u32(36 + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    u32(16);
    u16(3); // IEEE float
    u16(channels);
    u32(sampleRate);
    u32(sampleRate * blockAlign);
    u16(blockAlign);
    u16(bitsPerSample);
    file.write("data", 4);
    u32(dataBytes);
    file.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
    file.close(); // a failed flush on close must not report success
    return !file.fail();
}

// Render the whole timeline through AudioExporter and report the measurements
// the exporter already made.
//
// Shared by render_song and verify_render so the two cannot drift: a verdict
// computed from a different render path than the one the agent used to make the
// file would be measuring something other than what it claims to confirm.
//
// The body is render_song's, lifted out unchanged. What it guarantees on every
// exit path: transport stopped, pattern mode restored, slot map rebuilt for
// tracks added since the engine was wired, and the audio graph drained — which
// in the app happens continuously in AestraApp::run() and has to be done by
// hand here.
bool renderFullSong(AudioEngine& engine, TrackManager& trackManager,
                    const std::string& outputPath, double tailSeconds,
                    AudioExporter::Result& outResult) {
    trackManager.buildAndShareSlotMap();
    if (auto slotMap = trackManager.getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }

    // The timeline render path routes tracks to master through the published
    // audio graph; in the app AestraApp::run() drains rebuilds continuously,
    // headless we drain here.
    PlaybackGraphController graphController;
    graphController.setTrackManager(&trackManager);
    graphController.setAudioEngine(&engine);
    graphController.requestRebuild(GraphDirtyReason::TimelineChanged);
    graphController.drainIfDirty(static_cast<double>(engine.getSampleRate()));

    {
        // TrackManager::play() is what schedules timeline MIDI clip instances
        // into the pattern engine — the exporter only drives the engine
        // transport. Guarantee stop + pattern-mode restore on every exit.
        struct TimelineGuard {
            TrackManager& trackManager;
            AudioEngine& engine;
            ~TimelineGuard() {
                trackManager.stop();
                std::vector<float> settle(1024, 0.0f);
                for (int i = 0; i < 2; ++i) {
                    engine.processBlock(settle.data(), nullptr, 512, 0.0);
                    engine.performNonRealtimeMaintenance();
                }
            }
        } guard{trackManager, engine};

        // Leave any Arsenal pattern-mode state behind: TrackManager's play() only
        // schedules timeline MIDI clip instances when its own pattern-mode flag
        // is clear.
        trackManager.stopArsenalPlayback(false);
        engine.setPatternPlaybackMode(false, 4.0);
        trackManager.setPosition(0.0);
        trackManager.play();

        AudioExporter exporter(engine, trackManager);
        AudioExporter::Config config;
        config.outputPath = outputPath;
        config.scope = AudioExporter::RenderScope::FullSong;
        config.sampleRate = engine.getSampleRate();
        config.bitDepth = AudioExporter::BitDepth::Float_32;
        config.numChannels = 2;
        config.tailSeconds = tailSeconds;
        outResult = exporter.render(config);
    }
    return outResult.success;
}

} // namespace

namespace MuseInternal {

std::optional<std::string> handleRenderVerbs(const RequestContext& ctx, const ResponseEnvelope& env) {
    // Bound to the names the moved bodies already use, so no verb body had to be
    // edited to make the move.
    const double id = ctx.id;
    const std::string& verb = ctx.verb;
    JSON& request = *ctx.request;
    TrackManager* const m_trackManager = ctx.trackManager;
    AudioEngine* const m_engine = ctx.engine;
    // A reference, not a pointer: get_project_load_report tests this for a value
    // and then dereferences it, so it has to carry std::optional's own semantics.
    // A pointer would be truthy whenever the address is non-null — including when
    // the optional it points at is empty — which is not the same question.
    const std::optional<JSON>& m_projectLoadReport = *ctx.projectLoadReport;

    const auto makeOk = [&env]() { return env.ok(); };
    const auto finish = [&env](JSON& response) { return env.finish(response); };

    if (verb == "render_song") {
        if (!m_trackManager || !m_engine) {
            return makeError(id, "execution_error",
                             "render_song needs a track manager and an audio engine", verb)
                .toString();
        }
        if (!request.has("args") || !request["args"].isObject()) {
            return makeError(id, "validation_error",
                             "render_song requires args: {\"file\": <path>}", verb)
                .toString();
        }
        JSON& args = request["args"];
        for (auto& entry : args.asObject()) {
            if (entry.first != "file" && entry.first != "tail") {
                return makeError(id, "validation_error",
                                 "unknown arg for render_song: " + entry.first, verb)
                    .toString();
            }
        }
        if (!args.has("file") || !args["file"].isString() || args["file"].asString().empty()) {
            return makeError(id, "validation_error", "arg 'file' must be a non-empty string",
                             verb)
                .toString();
        }
        double tailSeconds = 1.0;
        if (args.has("tail")) {
            if (!args["tail"].isNumber()) {
                return makeError(id, "validation_error", "arg 'tail' must be a number", verb)
                    .toString();
            }
            tailSeconds = args["tail"].asNumber();
            if (!(tailSeconds >= 0.0 && tailSeconds <= 30.0)) {
                return makeError(id, "validation_error", "arg 'tail' must be 0..30 seconds",
                                 verb)
                    .toString();
            }
        }

        AudioExporter::Result exportResult;
        renderFullSong(*m_engine, *m_trackManager, args["file"].asString(), tailSeconds,
                       exportResult);

        if (!exportResult.success) {
            return makeError(id, "execution_error", exportResult.errorMessage, verb)
                .toString();
        }

        JSON result = JSON::object();
        result.set("file", JSON(exportResult.outputPath));
        result.set("durationSeconds", JSON(exportResult.durationSeconds));
        result.set("frames", JSON(static_cast<double>(exportResult.framesRendered)));
        result.set("sampleRate", JSON(static_cast<double>(m_engine->getSampleRate())));
        result.set("peakDb", JSON(exportResult.peakDb));
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "render_pattern") {
        if (!m_trackManager || !m_engine) {
            return makeError(id, "execution_error",
                             "render_pattern needs a track manager and an audio engine", verb)
                .toString();
        }
        if (!request.has("args") || !request["args"].isObject()) {
            return makeError(
                       id, "validation_error",
                       "render_pattern requires args: {\"pattern\": <id>, \"file\": <path>}",
                       verb)
                .toString();
        }
        JSON& args = request["args"];
        for (auto& entry : args.asObject()) {
            if (entry.first != "pattern" && entry.first != "file" && entry.first != "tail") {
                return makeError(id, "validation_error",
                                 "unknown arg for render_pattern: " + entry.first, verb)
                    .toString();
            }
        }
        uint64_t patternValue = 0;
        if (!args.has("pattern") || !args["pattern"].isNumber() ||
            !numberToId(args["pattern"].asNumber(), patternValue)) {
            return makeError(id, "validation_error",
                             "arg 'pattern' must be a non-negative integer", verb)
                .toString();
        }
        if (!args.has("file") || !args["file"].isString() || args["file"].asString().empty()) {
            return makeError(id, "validation_error", "arg 'file' must be a non-empty string",
                             verb)
                .toString();
        }
        double tailSeconds = 1.0;
        if (args.has("tail")) {
            if (!args["tail"].isNumber()) {
                return makeError(id, "validation_error", "arg 'tail' must be a number", verb)
                    .toString();
            }
            tailSeconds = args["tail"].asNumber();
            if (!(tailSeconds >= 0.0 && tailSeconds <= 30.0)) {
                return makeError(id, "validation_error", "arg 'tail' must be 0..30 seconds",
                                 verb)
                    .toString();
            }
        }

        const PatternID patternId{static_cast<uint64_t>(args["pattern"].asNumber())};
        const PatternSource* pattern =
            m_trackManager->getPatternManager().getPattern(patternId);
        if (!pattern) {
            return makeError(id, "execution_error",
                             "no such pattern: " + std::to_string(patternId.value), verb)
                .toString();
        }
        if (!pattern->isMidi()) {
            return makeError(id, "execution_error", "pattern is not a MIDI pattern", verb)
                .toString();
        }

        const double bpm = std::max(1.0, static_cast<double>(m_engine->getBPM()));
        // playPatternInArsenal resolves pattern length to at least 8 beats.
        const double lengthBeats = std::max(8.0, pattern->lengthBeats);
        const double durationSeconds = lengthBeats * 60.0 / bpm + tailSeconds;
        const uint32_t sampleRate = m_engine->getSampleRate();
        const uint64_t totalFrames =
            static_cast<uint64_t>(durationSeconds * static_cast<double>(sampleRate));
        constexpr uint32_t kBlockFrames = 512;

        // Bound the render before touching engine state: the whole take
        // is buffered in memory and the RIFF format caps a WAV at 4 GiB.
        constexpr double kMaxRenderSeconds = 600.0;
        const uint64_t dataBytes = totalFrames * 2ull * sizeof(float);
        if (durationSeconds > kMaxRenderSeconds ||
            36ull + dataBytes > 0xFFFFFFFFull) {
            return makeError(id, "validation_error",
                             "render too long: " + std::to_string(durationSeconds) +
                                 "s (max " + std::to_string(kMaxRenderSeconds) + "s)",
                             verb)
                .toString();
        }

        // Pattern playback now shares the mixer graph with Timeline playback.
        // Headless callers do not have the app loop to publish recent unit or
        // channel changes, so prepare the current graph before pumping audio.
        m_trackManager->buildAndShareSlotMap();
        if (auto slotMap = m_trackManager->getChannelSlotMapShared()) {
            m_engine->setChannelSlotMap(slotMap);
        }
        PlaybackGraphController graphController;
        graphController.setTrackManager(m_trackManager);
        graphController.setAudioEngine(m_engine);
        graphController.requestRebuild(GraphDirtyReason::TimelineChanged);
        graphController.drainIfDirty(static_cast<double>(sampleRate));

        // Keep-length samplers play pre-rendered copies: never bounce the resampled fallback.
        if (!m_trackManager->prewarmSamplerKeepLength(true)) {
            return makeError(id, "execution_error", "keep-length sampler renders did not finish in time", verb)
                .toString();
        }

        std::vector<float> rendered;
        rendered.reserve(static_cast<size_t>(totalFrames) * 2u);
        std::vector<float> block(static_cast<size_t>(kBlockFrames) * 2u, 0.0f);
        float peak = 0.0f;
        bool nonFinite = false;

        {
            // Everything after playback starts must be undone even if the
            // pump throws: stop the transport, drain the stop command with
            // settle blocks, and leave pattern mode (app default length).
            struct TransportGuard {
                AudioEngine& engine;
                TrackManager& trackManager;
                std::vector<float>& block;
                ~TransportGuard() {
                    // Full Arsenal teardown: stop, clear the scheduled
                    // instance, and leave pattern mode — a lingering
                    // pattern-mode flag or instance would bleed into the
                    // next timeline play/render.
                    trackManager.stopArsenalPlayback(false);
                    for (int i = 0; i < 2; ++i) {
                        std::memset(block.data(), 0, block.size() * sizeof(float));
                        engine.processBlock(block.data(), nullptr, kBlockFrames, 0.0);
                        engine.performNonRealtimeMaintenance();
                    }
                    engine.setPatternPlaybackMode(false, 4.0);
                }
            } guard{*m_engine, *m_trackManager, block};

            // Offline bounce through the exact live engine path, the same
            // way the headless test renderer pumps it. Arsenal preview
            // routing is what the user hears when a pattern plays, so it
            // is what the agent gets back. Pattern playback needs both
            // sides armed: the scheduler (playPatternInArsenal) and the
            // engine's pattern mode (the app sets it wherever it starts
            // pattern playback).
            m_engine->setPatternPlaybackMode(true, lengthBeats);
            m_trackManager->playPatternInArsenal(patternId, 0.0);

            uint64_t framesRemaining = totalFrames;
            while (framesRemaining > 0 && !nonFinite) {
                const uint32_t framesThisBlock =
                    static_cast<uint32_t>(std::min<uint64_t>(kBlockFrames, framesRemaining));
                std::memset(block.data(), 0, block.size() * sizeof(float));
                m_engine->processBlock(block.data(), nullptr, framesThisBlock, 0.0);
                // The pattern scheduler's RT queue is refilled from the
                // control thread; offline that's us (AudioExporter does
                // the same per block).
                m_engine->performNonRealtimeMaintenance();
                for (uint32_t i = 0; i < framesThisBlock * 2u; ++i) {
                    // Plugin output is untrusted: NaN/Inf must fail the
                    // render, not land in the file as "ok".
                    if (!std::isfinite(block[i])) {
                        nonFinite = true;
                        break;
                    }
                    peak = std::max(peak, std::abs(block[i]));
                }
                if (nonFinite) break;
                rendered.insert(rendered.end(), block.begin(),
                                block.begin() + static_cast<size_t>(framesThisBlock) * 2u);
                framesRemaining -= framesThisBlock;
            }
        }

        if (nonFinite) {
            return makeError(id, "execution_error",
                             "engine produced non-finite audio; render aborted", verb)
                .toString();
        }

        if (!writeFloat32Wav(args["file"].asString(), rendered, sampleRate)) {
            return makeError(id, "execution_error",
                             "cannot write output file: " + args["file"].asString(), verb)
                .toString();
        }

        const double peakDb = peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak))
                                          : -144.0;
        JSON result = JSON::object();
        result.set("file", JSON(args["file"].asString()));
        result.set("durationSeconds", JSON(durationSeconds));
        result.set("frames", JSON(static_cast<double>(totalFrames)));
        result.set("sampleRate", JSON(static_cast<double>(sampleRate)));
        result.set("peakDb", JSON(peakDb));
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }
    // --- verify_render -------------------------------------------------------
    //
    // The confirmation verb. render_song reports what came out; this one says
    // whether that is acceptable, so an agent is not left eyeballing peakDb and
    // deciding for itself whether -96 dBFS means "quiet" or "broken".
    //
    // It reports the exporter's OWN measurements rather than re-deriving them
    // from the samples: peakDb and the true-peak ceiling are already computed
    // during the render, and a second estimate of the same number would be a
    // second thing that can be wrong.
    if (verb == "verify_render") {
        if (!m_trackManager || !m_engine) {
            return makeError(id, "execution_error",
                             "verify_render needs a track manager and an audio engine", verb)
                .toString();
        }
        if (!request.has("args") || !request["args"].isObject()) {
            return makeError(id, "validation_error",
                             "verify_render requires args: {\"file\": <path>}", verb)
                .toString();
        }
        JSON& args = request["args"];
        for (auto& entry : args.asObject()) {
            const std::string& key = entry.first;
            const bool known = key == "file" || key == "tail" || key == "against" ||
                               key == "expect_peak_min_db" || key == "expect_peak_max_db" ||
                               key == "expect_not_silent" || key == "expect_no_clipping";
            if (!known) {
                return makeError(id, "validation_error", "unknown arg for verify_render: " + key,
                                 verb)
                    .toString();
            }
        }
        if (!args.has("file") || !args["file"].isString() || args["file"].asString().empty()) {
            return makeError(id, "validation_error", "arg 'file' must be a non-empty string",
                             verb)
                .toString();
        }

        double tailSeconds = 1.0;
        if (args.has("tail")) {
            if (!args["tail"].isNumber()) {
                return makeError(id, "validation_error", "arg 'tail' must be a number", verb)
                    .toString();
            }
            tailSeconds = args["tail"].asNumber();
            if (!(tailSeconds >= 0.0 && tailSeconds <= 30.0)) {
                return makeError(id, "validation_error", "arg 'tail' must be 0..30 seconds",
                                 verb)
                    .toString();
            }
        }

        // Defaults chosen to catch the two failures that actually happen: a
        // render that came out silent, and one that clipped. Both default on,
        // because "did I hear anything" and "is it too loud" are questions with
        // no comfortable answer when you have to guess. A caller who wants a
        // quiet render on purpose turns the first off explicitly.
        double peakMinDb = -89.0;
        if (args.has("expect_peak_min_db")) {
            if (!args["expect_peak_min_db"].isNumber()) {
                return makeError(id, "validation_error",
                                 "arg 'expect_peak_min_db' must be a number", verb)
                    .toString();
            }
            peakMinDb = args["expect_peak_min_db"].asNumber();
        }
        double peakMaxDb = 0.0;
        if (args.has("expect_peak_max_db")) {
            if (!args["expect_peak_max_db"].isNumber()) {
                return makeError(id, "validation_error",
                                 "arg 'expect_peak_max_db' must be a number", verb)
                    .toString();
            }
            peakMaxDb = args["expect_peak_max_db"].asNumber();
        }
        bool expectNotSilent = true;
        if (args.has("expect_not_silent")) {
            if (!args["expect_not_silent"].isBool()) {
                return makeError(id, "validation_error",
                                 "arg 'expect_not_silent' must be true or false", verb)
                    .toString();
            }
            expectNotSilent = args["expect_not_silent"].asBool();
        }
        bool expectNoClipping = true;
        if (args.has("expect_no_clipping")) {
            if (!args["expect_no_clipping"].isBool()) {
                return makeError(id, "validation_error",
                                 "arg 'expect_no_clipping' must be true or false", verb)
                    .toString();
            }
            expectNoClipping = args["expect_no_clipping"].asBool();
        }
        std::string against;
        if (args.has("against")) {
            if (!args["against"].isString() || args["against"].asString().empty()) {
                return makeError(id, "validation_error",
                                 "arg 'against' must be a non-empty string", verb)
                    .toString();
            }
            against = args["against"].asString();
        }

        AudioExporter::Result exportResult;
        renderFullSong(*m_engine, *m_trackManager, args["file"].asString(), tailSeconds,
                       exportResult);
        if (!exportResult.success) {
            // A render that could not happen is not a verdict, so it stays an
            // error rather than becoming verdict:"fail".
            return makeError(id, "execution_error", exportResult.errorMessage, verb).toString();
        }

        JSON checks = JSON::array();
        bool allPassed = true;
        const auto addCheck = [&](const char* name, bool passed, const std::string& expected,
                                  const std::string& actual) {
            JSON check = JSON::object();
            check.set("name", JSON(std::string(name)));
            check.set("pass", JSON(passed));
            check.set("expected", JSON(expected));
            check.set("actual", JSON(actual));
            checks.push(check);
            if (!passed) allPassed = false;
        };
        const auto fmt = [](double value, int places) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.*f", places, value);
            return std::string(buf);
        };

        if (expectNotSilent) {
            const bool silent = !(exportResult.peakDb > peakMinDb);
            addCheck("not_silent", !silent,
                     "peak above " + fmt(peakMinDb, 1) + " dBFS",
                     "peak " + fmt(exportResult.peakDb, 2) + " dBFS");
        }
        {
            const bool inRange = exportResult.peakDb <= peakMaxDb;
            addCheck("peak_in_range", inRange,
                     "peak at or below " + fmt(peakMaxDb, 1) + " dBFS",
                     "peak " + fmt(exportResult.peakDb, 2) + " dBFS");
        }
        if (expectNoClipping) {
            const bool clipped = exportResult.truePeakCeilingExceeded;
            addCheck("no_clipping", !clipped, "true peak under the ceiling",
                     fmt(static_cast<double>(exportResult.maxTruePeakdBTP), 2) + " dBTP" +
                         (clipped ? " (ceiling exceeded)" : ""));
        }

        JSON result = JSON::object();
        result.set("file", JSON(exportResult.outputPath));
        result.set("durationSeconds", JSON(exportResult.durationSeconds));
        result.set("frames", JSON(static_cast<double>(exportResult.framesRendered)));
        result.set("sampleRate", JSON(static_cast<double>(m_engine->getSampleRate())));
        result.set("peakDb", JSON(exportResult.peakDb));
        result.set("maxTruePeakDbTtp", JSON(static_cast<double>(exportResult.maxTruePeakdBTP)));
        result.set("truePeakCeilingExceeded", JSON(exportResult.truePeakCeilingExceeded));

        if (!against.empty()) {
            // Byte-for-byte reproducibility: an unchanged session must render
            // to an identical file. A digest is the only way to say that.
            const std::string got = fnv1aFileDigest(exportResult.outputPath);
            const std::string want = fnv1aFileDigest(against);
            if (got.empty() || want.empty()) {
                return makeError(id, "execution_error",
                                 "could not digest '" +
                                     (got.empty() ? exportResult.outputPath : against) +
                                     "' for comparison", verb)
                    .toString();
            }
            result.set("digest", JSON(got));
            result.set("comparedWith", JSON(against));
            addCheck("matches_previous_render", got == want,
                     "identical bytes to " + against, got == want ? got : got + " != " + want);
        }

        result.set("checks", checks);
        result.set("verdict", JSON(std::string(allPassed ? "pass" : "fail")));

        // status stays "ok" even when the verdict is "fail": the verb did its job
        // and the thing it judged is wrong. Conflating the two would teach an
        // agent to retry a failing render instead of fixing the mix.
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    // No verb matched. In the single-file version this fell out of the if-chain
    // into the next family's checks and eventually the mutation path, which is
    // exactly what nullopt asks handleRequest to do.
    return std::nullopt;
}

} // namespace MuseInternal
} // namespace Audio
} // namespace Aestra
