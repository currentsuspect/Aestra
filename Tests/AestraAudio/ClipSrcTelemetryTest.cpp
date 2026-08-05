// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SRC telemetry driven by actual rendering.
//
// The characterization harness compares rendered audio, so it is structurally
// blind to this: `srcActiveThisBlock` is pure telemetry and never reaches the
// mix. Extracting the clip body turns the mono resampling branch's
// clip-level `continue` statements into `return`s, and a return that forgets
// to carry the flag loses the counter while every digest stays identical.
//
// "SRC active" means *sample-rate conversion ran*, not "a clip contributed
// audio". A unity-rate clip mixes real audio and must NOT set it.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using Aestra::Audio::AudioBufferData;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::ClipEdits;
using Aestra::Audio::ClipInstance;
using Aestra::Audio::ClipInstanceID;
using Aestra::Audio::ClipSourceID;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::PatternID;
using Aestra::Audio::PlaylistLaneID;
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

std::shared_ptr<AudioBufferData> makeSource(uint32_t sampleRate, uint32_t channels, uint64_t frames) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = sampleRate;
    buffer->numChannels = channels;
    buffer->numFrames = frames;
    buffer->interleavedData.resize(static_cast<size_t>(frames) * channels);
    for (uint64_t i = 0; i < frames; ++i) {
        const float step = ((i / 64) % 2 == 0) ? 0.7f : -0.7f;
        for (uint32_t c = 0; c < channels; ++c) {
            buffer->interleavedData[i * channels + c] = step;
        }
    }
    return buffer;
}

struct Rig {
    std::shared_ptr<TrackManager> tm;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MuseService> service;
    PlaylistLaneID lane;
    std::string dir;

    bool init(const std::string& d) {
        dir = d;
        tm = std::make_shared<TrackManager>();
        tm->getUnitManager().setPatternManager(&tm->getPatternManager());
        engine = std::make_unique<AudioEngine>();
        engine->setSampleRate(48000);
        engine->setBufferConfig(4096, 2);
        MuseService::wireHeadlessEngine(tm, *engine);
        if (!engine->initialize()) return false;
        service = std::make_unique<MuseService>(tm.get(), engine.get());
        lane = tm->getPlaylistModel().createLane("src");
        return lane.isValid();
    }

    void addClip(const std::string& name, std::shared_ptr<AudioBufferData> buffer, double startBeat = 0.0,
                 const ClipEdits& edits = ClipEdits{}) {
        const double seconds = buffer->durationSeconds();
        const uint64_t frames = buffer->numFrames;
        const ClipSourceID sourceId =
            tm->getSourceManager().createRecordedSource(dir + "/" + name + ".src", name, std::move(buffer));

        Aestra::Audio::AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = seconds;
        Aestra::Audio::AudioSlice slice;
        slice.startSamples = 0.0;
        slice.lengthSamples = static_cast<double>(frames);
        payload.slices.push_back(slice);

        const double beats = tm->getPlaylistModel().secondsToBeats(seconds);
        const PatternID pattern = tm->getPatternManager().createAudioPattern(name, beats, payload);

        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.name = name;
        clip.startBeat = startBeat;
        clip.durationBeats = beats;
        clip.durationSeconds = seconds;
        clip.patternId = pattern;
        clip.sourceId = pattern.value;
        clip.edits = edits;
        tm->getPlaylistModel().addClip(lane, clip);
    }

    /** Total squared sample energy of the last render, for "did it contribute?". */
    double lastRenderEnergy{0.0};
    /** Samples actually read from the data chunk, so a misread cannot look like silence. */
    size_t lastRenderSamples{0};

    /** Render the timeline and report how far the SRC counter advanced. */
    uint64_t renderAndCountSrcBlocks(const std::string& file) {
        const uint64_t before = engine->telemetry().getSrcActiveBlocks();
        JSON args = JSON::object();
        args.set("file", JSON(file));
        args.set("tail", JSON(0.0));
        JSON req = JSON::object();
        req.set("id", JSON(1.0));
        req.set("verb", JSON(std::string("render_song")));
        req.set("args", args);
        JSON r = JSON::parse(service->handleRequest(req.toString()));
        if (!r.has("status") || r["status"].asString() != "ok") return UINT64_MAX;

        // Scan for the data chunk rather than assuming a 44-byte header.
        // AudioExporter emits exactly RIFF+WAVE+fmt+data today, so a fixed
        // seek happens to work — but ClipRenderService's writer already emits
        // a fact chunk, and a misaligned seek would sum garbage floats into a
        // value still greater than zero. This test exists to catch a silently
        // lost clip; it cannot afford to pass or fail for an unrelated reason.
        lastRenderEnergy = 0.0;
        lastRenderSamples = 0;
        std::FILE* f = std::fopen(file.c_str(), "rb");
        if (f) {
            char riff[4], wave[4];
            uint32_t riffSize = 0;
            if (std::fread(riff, 1, 4, f) == 4 && std::fread(&riffSize, 4, 1, f) == 1 &&
                std::fread(wave, 1, 4, f) == 4 && std::memcmp(riff, "RIFF", 4) == 0 &&
                std::memcmp(wave, "WAVE", 4) == 0) {
                char id[4];
                uint32_t size = 0;
                while (std::fread(id, 1, 4, f) == 4 && std::fread(&size, 4, 1, f) == 1) {
                    if (std::memcmp(id, "data", 4) == 0) {
                        const size_t count = size / sizeof(float);
                        float sample = 0.0f;
                        for (size_t i = 0; i < count && std::fread(&sample, sizeof(float), 1, f) == 1; ++i) {
                            if (std::isfinite(sample)) {
                                lastRenderEnergy += static_cast<double>(sample) * sample;
                            }
                            ++lastRenderSamples;
                        }
                        break;
                    }
                    std::fseek(f, static_cast<long>(size + (size & 1u)), SEEK_CUR);
                }
            }
            std::fclose(f);
        }
        return engine->telemetry().getSrcActiveBlocks() - before;
    }
};

/** Set the engine's interpolation quality, exercising each mono switch case. */
void runQualityCase(const std::string& dir, Aestra::Audio::Interpolators::InterpolationQuality quality,
                    const std::string& label) {
    Rig rig;
    if (!rig.init(dir)) {
        check(false, label + ": rig initialises");
        return;
    }
    rig.engine->setInterpolationQuality(quality);
    // Mono at a non-engine rate: forces the mono resampling branch, whose
    // clip-level `continue` statements become returns once the body moves.
    rig.addClip("mono44k_" + label, makeSource(44100, 1, 22050));
    const uint64_t blocks = rig.renderAndCountSrcBlocks(dir + "/mono_" + label + ".wav");
    check(blocks != UINT64_MAX, label + ": render succeeds");
    check(blocks > 0, label + ": mono resampling marks SRC active");
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) {
        std::cout << "FAIL: plugin manager initialize\n";
        return 1;
    }
    CommandRegistry::initialize();

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "aestra_src_telemetry";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) {
        std::cout << "FAIL: could not create the working directory\n";
        return 1;
    }
    const std::string d = dir.string();

    std::cout << "=== SRC telemetry driven by rendering ===\n";

    // A unity-rate clip mixes real audio through the direct-copy path. SRC
    // must stay untouched: the flag means conversion ran, not "audio played".
    {
        Rig rig;
        if (rig.init(d)) {
            rig.addClip("unity", makeSource(48000, 2, 24000));
            const uint64_t blocks = rig.renderAndCountSrcBlocks(d + "/unity.wav");
            check(blocks != UINT64_MAX, "unity-rate: render succeeds");
            check(blocks == 0, "unity-rate: mixes audio without marking SRC active");
        } else {
            check(false, "unity-rate: rig initialises");
        }
    }

    // Stereo at a different rate: the stereo resampling branch.
    {
        Rig rig;
        if (rig.init(d)) {
            rig.addClip("stereo44k", makeSource(44100, 2, 22050));
            const uint64_t blocks = rig.renderAndCountSrcBlocks(d + "/stereo.wav");
            check(blocks != UINT64_MAX, "stereo resampled: render succeeds");
            check(blocks > 0, "stereo resampled: marks SRC active");
        } else {
            check(false, "stereo resampled: rig initialises");
        }
    }

    // Every mono switch case, because each ends in its own clip-level exit.
    using Q = Aestra::Audio::Interpolators::InterpolationQuality;
    runQualityCase(d, Q::Cubic, "cubic");
    runQualityCase(d, Q::Sinc8, "sinc8");
    runQualityCase(d, Q::Sinc16, "sinc16");
    runQualityCase(d, Q::Sinc32, "sinc32");
    runQualityCase(d, Q::Sinc64, "sinc64");

    // The counter is one increment per *block* in which any clip converted,
    // not one per clip. Two identical resampled clips over the same span must
    // therefore advance it exactly as far as one does.
    {
        Rig one, two;
        uint64_t oneClip = UINT64_MAX, twoClips = UINT64_MAX;
        if (one.init(d)) {
            one.addClip("solo", makeSource(44100, 2, 22050));
            oneClip = one.renderAndCountSrcBlocks(d + "/solo.wav");
        }
        if (two.init(d)) {
            two.addClip("dupA", makeSource(44100, 2, 22050));
            two.addClip("dupB", makeSource(44100, 2, 22050));
            twoClips = two.renderAndCountSrcBlocks(d + "/dup.wav");
        }
        check(oneClip != UINT64_MAX && twoClips != UINT64_MAX, "overlap: both renders succeed");
        check(oneClip > 0, "overlap: a single resampled clip advances the counter");
        check(twoClips == oneClip,
              "overlap: two resampled clips in the same blocks advance it once per block, not per clip");

        // The counter alone cannot see this. If the caller ever aggregates the
        // helper result with short-circuiting || instead of |=, the first
        // resampled clip sets the flag and every later clip in the block is
        // never rendered at all — telemetry still reports one SRC-active
        // block, correctly, while the audio silently loses a clip.
        check(one.lastRenderSamples > 0 && two.lastRenderSamples > 0,
              "overlap: the data chunk was located in both renders");
        check(one.lastRenderEnergy > 0.0, "overlap: the single-clip render carries audio");
        check(two.lastRenderEnergy > one.lastRenderEnergy * 1.5,
              "overlap: the second clip still contributes audio after the first set the flag");
    }

    // A resampled clip that reaches the frame-count clamp but yields
    // framesToRender == 0 must not mark SRC active. The flag means conversion
    // actually ran, and it is set only *after* that rejection — so hoisting it
    // earlier changes telemetry while leaving every audio sample identical.
    //
    // Constructed to land in exactly that window: ratio > 1 puts it on the
    // resampling branch, and leaving fewer source frames than the ratio makes
    // maxFrames = remaining / ratio truncate to zero. It must clear the
    // preceding phase >= totalFrames guard, so remaining stays positive.
    {
        constexpr uint64_t kSourceFrames = 22050;
        constexpr uint32_t kSourceRate = 48000;
        constexpr uint32_t kEngineRate = 48000;
        constexpr float kPlaybackRate = 4.0f;
        constexpr double kRemaining = 2.0;

        // Assert the predicate this witness exists to exercise, mirroring the
        // engine's own arithmetic. Without this, a later change to the
        // playback-rate clamp, the rate calculation or offset conversion could
        // push the clip back to the earlier phase >= totalFrames exit while
        // the telemetry expectation still passed — an outcome match that no
        // longer witnesses the path.
        const double ratio = (static_cast<double>(kSourceRate) / static_cast<double>(kEngineRate)) *
                             static_cast<double>(kPlaybackRate);
        const double phase = static_cast<double>(kSourceFrames) - kRemaining;
        const double remaining = static_cast<double>(kSourceFrames) - phase;
        check(std::abs(ratio - 1.0) >= 1e-9, "zero-frame clip: ratio is non-unity, so it takes the resampling branch");
        check(phase < static_cast<double>(kSourceFrames),
              "zero-frame clip: clears the phase >= totalFrames guard, so it reaches the length clamp");
        check(remaining > 0.0 && remaining < ratio, "zero-frame clip: fewer source frames remain than the ratio");
        check(static_cast<uint32_t>(remaining / ratio) == 0,
              "zero-frame clip: the length clamp truncates framesToRender to zero");

        Rig rig;
        if (rig.init(d)) {
            ClipEdits starved;
            starved.playbackRate = kPlaybackRate;
            starved.sourceStart = phase;
            rig.addClip("starved", makeSource(kSourceRate, 2, kSourceFrames), 0.0, starved);
            const uint64_t blocks = rig.renderAndCountSrcBlocks(d + "/starved.wav");
            check(blocks != UINT64_MAX, "zero-frame clip: render succeeds");
            check(blocks == 0,
                  "zero-frame clip: a resampled clip clamped to zero frames does not mark SRC active");
        } else {
            check(false, "zero-frame clip: rig initialises");
        }
    }

    // Stereo interpolation quality must actually be consulted.
    //
    // Guard for the block-snapshot normalization: pinning stereo to one
    // quality by accident would leave every characterization digest matching,
    // because none of those fixtures changes quality. Rendering the same
    // resampled stereo clip at the cheapest and most expensive settings must
    // produce different audio.
    {
        using Q = Aestra::Audio::Interpolators::InterpolationQuality;
        double cubicEnergy = -1.0, sincEnergy = -1.0;
        size_t cubicSamples = 0, sincSamples = 0;
        {
            Rig rig;
            if (rig.init(d)) {
                rig.engine->setInterpolationQuality(Q::Cubic);
                rig.addClip("stereoCubic", makeSource(44100, 2, 22050));
                rig.renderAndCountSrcBlocks(d + "/stereo_cubic.wav");
                cubicEnergy = rig.lastRenderEnergy;
                cubicSamples = rig.lastRenderSamples;
            }
        }
        {
            Rig rig;
            if (rig.init(d)) {
                rig.engine->setInterpolationQuality(Q::Sinc64);
                rig.addClip("stereoSinc", makeSource(44100, 2, 22050));
                rig.renderAndCountSrcBlocks(d + "/stereo_sinc.wav");
                sincEnergy = rig.lastRenderEnergy;
                sincSamples = rig.lastRenderSamples;
            }
        }
        check(cubicSamples > 0 && sincSamples > 0, "stereo quality: both renders produced samples");
        check(cubicEnergy > 0.0 && sincEnergy > 0.0, "stereo quality: both renders carry audio");
        check(cubicEnergy != sincEnergy,
              "stereo quality: cubic and sinc64 render differently, so quality is consulted");
    }

    fs::remove_all(dir, ec);

    if (g_failures != 0) {
        std::cout << "FAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "All SRC telemetry checks passed.\n";
    return 0;
}
