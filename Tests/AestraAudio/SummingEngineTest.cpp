// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SummingEngineTest — Verifies digital summing accuracy against mathematical reference.
// Spec §3: N × 1kHz -20dBFS sine tracks summed, RMS error < -140 dB vs mathematical addition.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kChannels = 2;
constexpr double kFrequencyHz = 1000.0;
constexpr float kAmplitudeDbfs = -20.0f;
constexpr double kAmplitudeLinear = 0.1; // -20 dBFS
constexpr uint32_t kNumTracks = 10;
constexpr double kDurationSeconds = 1.0;
constexpr uint32_t kTotalFrames = static_cast<uint32_t>(kSampleRate * kDurationSeconds);
constexpr double kTau = 6.28318530717958647692;
constexpr double kPassRmsDb = -140.0;

} // namespace

int main() {
    std::cout << "=== Aestra Summing Engine Test ===\n\n";

    // Create TrackManager with N tracks
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));
    for (uint32_t t = 0; t < kNumTracks; ++t)
        trackManager->addChannel("SumTrack_" + std::to_string(t));

    // Create a shared audio buffer: 1kHz sine at -20 dBFS
    auto sineBuffer = std::make_shared<AudioBufferData>();
    sineBuffer->sampleRate = kSampleRate;
    sineBuffer->numChannels = kChannels;
    sineBuffer->numFrames = kTotalFrames;
    sineBuffer->interleavedData.resize(static_cast<size_t>(kTotalFrames) * kChannels, 0.0f);
    for (uint32_t i = 0; i < kTotalFrames; ++i) {
        double s = std::sin(kTau * kFrequencyHz * static_cast<double>(i) / kSampleRate) * kAmplitudeLinear;
        sineBuffer->interleavedData[static_cast<size_t>(i) * 2] = static_cast<float>(s);
        sineBuffer->interleavedData[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(s);
    }

    // Assign the same sine source to every track lane
    for (uint32_t t = 0; t < kNumTracks; ++t) {
        std::string stem = "summing_source_" + std::to_string(t);
        std::string path = (std::filesystem::temp_directory_path() / (stem + ".wav")).string();
        ClipSourceID sourceId = trackManager->getSourceManager().createRecordedSource(path, stem, sineBuffer);
        AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = kDurationSeconds;
        payload.slices.push_back({0.0, kDurationSeconds, 0.0, static_cast<double>(kTotalFrames)});
        PlaylistLaneID laneId = trackManager->getPlaylistModel().createLane(stem);
        PatternID patternId = trackManager->getPatternManager().createAudioPattern(
            stem, kDurationSeconds * 2.0, payload);
        if (auto* channel = trackManager->getChannel(t)) {
            trackManager->getPatternManager().setPatternMixerChannel(patternId, channel->getChannelId());
        }
        const ClipInstanceID clipId =
            trackManager->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, kDurationSeconds * 2.0);
        if (!trackManager->getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
            std::cerr << "Failed to configure unity-gain summing fixture\n";
            return 1;
        }
    }

    // Initialize engine
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(trackManager);
    engine.setBPM(120.0f);
    engine.setSafetyLimiterEnabled(false);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    engine.initialize();

    // Render through engine
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);

    std::vector<float> engineOutput;
    engineOutput.reserve(static_cast<size_t>(kTotalFrames) * kChannels);
    std::vector<float> block(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);

    uint32_t rendered = 0;
    while (rendered < kTotalFrames) {
        uint32_t frames = std::min(kBlockSize, kTotalFrames - rendered);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, frames, 0.0);
        engineOutput.insert(engineOutput.end(), block.begin(),
                            block.begin() + static_cast<ptrdiff_t>(frames * kChannels));
        rendered += frames;
    }
    engine.setTransportPlaying(false);

    // Compute mathematical expectation:
    // N × sin(2πft) × 10^(-20/20) × panLawGain
    // Pan law: at center (pan=0), fastStereoBalanceGainsD gives unity per channel.
    constexpr double kPanLawCenterGain = 1.0; // stereo-balance law: unity at center (strip pan-law fix 2026-08-14)
    std::vector<double> expected(static_cast<size_t>(kTotalFrames) * kChannels, 0.0);
    for (uint32_t i = 0; i < kTotalFrames; ++i) {
        double val = static_cast<double>(kNumTracks) * kPanLawCenterGain *
                     std::sin(kTau * kFrequencyHz * static_cast<double>(i) / kSampleRate) *
                     kAmplitudeLinear;
        expected[static_cast<size_t>(i) * 2] = val;
        expected[static_cast<size_t>(i) * 2 + 1] = val;
    }

    // Trim startup transient (gainL ramp 512 + fade-in 256) and clip-edge fade-out (128).
    // Both are intentional click prevention, not summing errors.
    constexpr size_t kTrimStart = kBlockSize;
    constexpr size_t kTrimEnd = 256; // covers CLIP_EDGE_FADE_SAMPLES (128) with margin
    const size_t startSample = kTrimStart * kChannels;
    const size_t endSample = (kTotalFrames - kTrimEnd) * kChannels;
    double rmsSumSq = 0.0;
    double peakErr = 0.0;
    size_t compared = 0;
    float engPeak = 0.0f;
    float expPeak = 0.0f;
    for (size_t i = startSample; i < engineOutput.size() && i < expected.size() && i < endSample; ++i) {
        float e = engineOutput[i];
        float ex = static_cast<float>(expected[i]);
        engPeak = std::max(engPeak, std::abs(e));
        expPeak = std::max(expPeak, std::abs(ex));
        double diff = static_cast<double>(e) - static_cast<double>(ex);
        rmsSumSq += diff * diff;
        peakErr = std::max(peakErr, std::abs(diff));
        ++compared;
    }
    double rmsDb = (compared > 0) ? 20.0 * std::log10(std::sqrt(rmsSumSq / static_cast<double>(compared))) : 0.0;

    std::cout << "Tracks: " << kNumTracks << "\n";
    std::cout << "Signal: " << static_cast<int>(kFrequencyHz) << " Hz @ " << kAmplitudeDbfs << " dBFS\n";
    std::cout << "Compared samples: " << compared << " (trimmed " << kTrimStart
              << " start + " << kTrimEnd << " tail)\n";
    std::cout << "RMS error: " << rmsDb << " dB\n";
    std::cout << "Peak error: " << peakErr << " (linear)\n";
    std::cout << "Threshold: " << kPassRmsDb << " dB\n";
    bool pass = (rmsDb <= kPassRmsDb);
    std::cout << "Status: " << (pass ? "[PASS]" : "[FAIL]") << "\n";

    return pass ? 0 : 1;
}
