// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// GoldenAudioHarness — shared helpers for golden-audio regression tests.
//
// Design: references are ANALYTIC (computed at runtime from closed-form
// signals), never stored waveforms — nothing binary lives in git. If a
// deliberate engine change shifts expected output, update the expectation
// math in the same PR as the engine change, with reasoning in the commit.
//
// Every comparison produces a DiffReport with enough diagnostics to
// understand a failure without opening audio files:
//   max abs error, RMS error (dB), first mismatching frame + channel,
//   per-buffer peaks and their difference, sample rate, rendered length.
#pragma once

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace GoldenAudio {

// =============================================================================
// Diff / report
// =============================================================================

struct DiffReport {
    double maxAbsError{0.0};
    double rmsErrorDb{-999.0};
    int64_t firstMismatchFrame{-1}; // -1 = no sample exceeded tolerance
    int firstMismatchChannel{-1};
    double peakActual{0.0};
    double peakExpected{0.0};
    double peakDifference{0.0};
    uint32_t sampleRate{0};
    uint64_t frames{0};
    uint32_t channels{0};
};

/// Compare interleaved buffers. `tolerance` is the per-sample abs threshold
/// used for first-mismatch localization (the pass/fail policy is the caller's).
inline DiffReport compareBuffers(const std::vector<float>& actual, const std::vector<float>& expected,
                                 uint32_t channels, uint32_t sampleRate, double tolerance) {
    DiffReport r;
    r.sampleRate = sampleRate;
    r.channels = channels;
    const size_t n = std::min(actual.size(), expected.size());
    r.frames = (channels > 0) ? static_cast<uint64_t>(n / channels) : 0;

    double sumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double a = static_cast<double>(actual[i]);
        const double e = static_cast<double>(expected[i]);
        const double diff = std::abs(a - e);
        sumSq += diff * diff;
        r.maxAbsError = std::max(r.maxAbsError, diff);
        r.peakActual = std::max(r.peakActual, std::abs(a));
        r.peakExpected = std::max(r.peakExpected, std::abs(e));
        if (diff > tolerance && r.firstMismatchFrame < 0) {
            r.firstMismatchFrame = static_cast<int64_t>(i / channels);
            r.firstMismatchChannel = static_cast<int>(i % channels);
        }
    }
    r.peakDifference = std::abs(r.peakActual - r.peakExpected);
    const double rms = (n > 0) ? std::sqrt(sumSq / static_cast<double>(n)) : 0.0;
    r.rmsErrorDb = 20.0 * std::log10(std::max(rms, 1e-15));
    return r;
}

/// Print a pass/fail line; on failure, dump full forensics plus a pointer to
/// the paths a regression most likely came from.
inline void printReport(const std::string& label, const DiffReport& r, bool pass,
                        const std::string& toleranceDescription) {
    std::cout << "[" << (pass ? "PASS" : "FAIL") << "] " << label
              << "  maxAbsErr=" << r.maxAbsError
              << "  rmsErr=" << r.rmsErrorDb << " dB\n";
    if (!pass) {
        std::cout << "  ---- golden-audio failure forensics ----\n"
                  << "  tolerance:        " << toleranceDescription << "\n"
                  << "  max abs error:    " << r.maxAbsError << "\n"
                  << "  RMS error:        " << r.rmsErrorDb << " dB\n"
                  << "  first mismatch:   frame " << r.firstMismatchFrame
                  << ", channel " << r.firstMismatchChannel
                  << (r.firstMismatchFrame >= 0 && r.sampleRate > 0
                          ? "  (~" + std::to_string(static_cast<double>(r.firstMismatchFrame) /
                                                    static_cast<double>(r.sampleRate)) + " s)"
                          : std::string())
                  << "\n"
                  << "  peak actual:      " << r.peakActual << "\n"
                  << "  peak expected:    " << r.peakExpected << "\n"
                  << "  peak difference:  " << r.peakDifference << "\n"
                  << "  sample rate:      " << r.sampleRate << " Hz\n"
                  << "  rendered length:  " << r.frames << " frames x " << r.channels << " ch\n"
                  << "  If this engine change is INTENTIONAL, update the analytic\n"
                  << "  expectation in this test in the same PR and explain why in the\n"
                  << "  commit message. Likely regression sources: AudioEngine::processBlock\n"
                  << "  master stage, AudioRenderer summing/pan, clip playback interpolation.\n";
    }
}

// =============================================================================
// Session building (mirrors the setup used by GoldenReferenceTest)
// =============================================================================

struct SessionConfig {
    uint32_t sampleRate{48000};
    uint32_t blockSize{512};
    uint32_t channels{2};
    float bpm{120.0f};
};

/// Add one audio track whose clip contains `interleaved` (stereo) samples,
/// starting at beat 0. Returns the lane's source id.
inline Aestra::Audio::ClipSourceID addAudioTrack(Aestra::Audio::TrackManager& tm, const std::string& label,
                                                 const std::vector<float>& interleaved, uint32_t frames,
                                                 const SessionConfig& cfg) {
    using namespace Aestra::Audio;
    auto* channel = tm.addChannel(label);

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = cfg.sampleRate;
    buffer->numChannels = cfg.channels;
    buffer->numFrames = frames;
    buffer->interleavedData = interleaved;

    const std::string path =
        (std::filesystem::temp_directory_path() / ("golden_audio_" + label + ".wav")).string();
    ClipSourceID sourceId = tm.getSourceManager().createRecordedSource(path, label, buffer);

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = static_cast<double>(frames) / cfg.sampleRate;
    payload.slices.push_back({0.0, payload.durationSeconds, 0.0, static_cast<double>(frames)});

    PlaylistLaneID laneId = tm.getPlaylistModel().createLane(label);
    const double durationBeats =
        payload.durationSeconds * (static_cast<double>(cfg.bpm) / 60.0);
    PatternID patternId =
        tm.getPatternManager().createAudioPattern(label, durationBeats, payload);
    if (channel) {
        tm.getPatternManager().setPatternMixerChannel(patternId, channel->getChannelId());
    }
    const ClipInstanceID clipId = tm.getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, durationBeats);
    if (!tm.getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
        throw std::runtime_error("Failed to configure unity-gain golden-audio clip");
    }
    return sourceId;
}

/// Configure an engine on the given session the same way the app does:
/// graph from track manager, slot map shared, limiter/metronome/audition off
/// (deterministic), transport at sample 0.
inline void prepareEngine(Aestra::Audio::AudioEngine& engine,
                          const std::shared_ptr<Aestra::Audio::TrackManager>& tm, const SessionConfig& cfg) {
    using namespace Aestra::Audio;
    engine.setSampleRate(cfg.sampleRate);
    engine.setBufferConfig(cfg.blockSize, cfg.channels);
    engine.setTrackManager(tm);
    engine.setBPM(cfg.bpm);
    engine.setSafetyLimiterEnabled(false);
    tm->buildAndShareSlotMap();
    if (auto slotMap = tm->getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    engine.initialize();
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGlobalSamplePos(0);
}

/// Drive processBlock exactly like the realtime device callback would:
/// zero-filled output blocks, sequential frames. Returns interleaved output.
inline std::vector<float> renderBlocks(Aestra::Audio::AudioEngine& engine, uint64_t totalFrames,
                                       const SessionConfig& cfg) {
    std::vector<float> output;
    output.reserve(static_cast<size_t>(totalFrames) * cfg.channels);
    std::vector<float> block(static_cast<size_t>(cfg.blockSize) * cfg.channels, 0.0f);
    uint64_t rendered = 0;
    while (rendered < totalFrames) {
        const uint32_t frames =
            static_cast<uint32_t>(std::min<uint64_t>(cfg.blockSize, totalFrames - rendered));
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, frames, 0.0);
        output.insert(output.end(), block.begin(),
                      block.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(frames) * cfg.channels));
        rendered += frames;
    }
    return output;
}

} // namespace GoldenAudio
