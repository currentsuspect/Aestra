// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// GoldenReferenceTest — Audio regression test suite comparing engine output against
// mathematically computed reference signals. Spec §15.
//
// Every engine change should run this suite. Comparison metrics:
//   - RMS error (dB)
//   - Peak error (linear)
//   - Correlation (-1 to 1, 1 = perfect)

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
constexpr double kTau = 6.28318530717958647692;
constexpr uint32_t kTestDurationFrames = kSampleRate; // 1 second per test
constexpr double kPassRmsDb = -100.0; // More negative = better
constexpr double kPanLawCenterGain = 1.0; // stereo-balance law: unity at center (strip pan-law fix 2026-08-14)

// =============================================================================
// Analysis
// =============================================================================

struct AnalysisResult {
    std::string label;
    bool pass;
    double rmsErrorDb;
    double peakErrorLinear;
    double correlation;
};

double computeRmsDb(const std::vector<float>& actual, const std::vector<float>& expected) {
    if (actual.empty() || actual.size() != expected.size()) return 0.0;
    double sumSq = 0.0;
    for (size_t i = 0; i < actual.size(); ++i) {
        double diff = static_cast<double>(actual[i]) - static_cast<double>(expected[i]);
        sumSq += diff * diff;
    }
    double rms = std::sqrt(sumSq / static_cast<double>(actual.size()));
    return 20.0 * std::log10(std::max(rms, 1e-15));
}

double computePeakError(const std::vector<float>& actual, const std::vector<float>& expected) {
    double peak = 0.0;
    size_t n = std::min(actual.size(), expected.size());
    for (size_t i = 0; i < n; ++i)
        peak = std::max(peak, std::abs(static_cast<double>(actual[i]) - static_cast<double>(expected[i])));
    return peak;
}

double computeCorrelation(const std::vector<float>& actual, const std::vector<float>& expected) {
    size_t n = std::min(actual.size(), expected.size());
    if (n == 0) return 0.0;
    double meanA = 0.0, meanB = 0.0;
    for (size_t i = 0; i < n; ++i) { meanA += actual[i]; meanB += expected[i]; }
    meanA /= static_cast<double>(n); meanB /= static_cast<double>(n);
    double num = 0.0, denA = 0.0, denB = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double da = static_cast<double>(actual[i]) - meanA;
        double db = static_cast<double>(expected[i]) - meanB;
        num += da * db; denA += da * da; denB += db * db;
    }
    if (denA <= 0.0 || denB <= 0.0) return 0.0;
    return num / std::sqrt(denA * denB);
}

// =============================================================================
// Test runner
// =============================================================================

AnalysisResult runSignalTest(const std::string& label, const std::vector<float>& refSamples) {
    // Create engine with a track that plays the reference signal
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));
    auto* channel = trackManager->addChannel("GoldenRef");

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = kSampleRate;
    buffer->numChannels = kChannels;
    buffer->numFrames = kTestDurationFrames;
    buffer->interleavedData = refSamples;

    std::string path = (std::filesystem::temp_directory_path() / ("golden_ref_" + label + ".wav")).string();
    ClipSourceID sourceId = trackManager->getSourceManager().createRecordedSource(path, label, buffer);
    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = static_cast<double>(kTestDurationFrames) / kSampleRate;
    payload.slices.push_back({0.0, payload.durationSeconds, 0.0, static_cast<double>(kTestDurationFrames)});

    PlaylistLaneID laneId = trackManager->getPlaylistModel().createLane("GoldenRef");
    PatternID patternId = trackManager->getPatternManager().createAudioPattern(
        "GoldenRef", payload.durationSeconds * 2.0, payload);
    if (channel) {
        trackManager->getPatternManager().setPatternMixerChannel(patternId, channel->getChannelId());
    }
    const ClipInstanceID clipId =
        trackManager->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, payload.durationSeconds * 2.0);
    if (!trackManager->getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
        return {label, false, 0.0, 0.0, 0.0};
    }

    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(trackManager);
    engine.setBPM(120.0f);
    engine.setSafetyLimiterEnabled(false);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    engine.initialize();
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);

    std::vector<float> output;
    std::vector<float> block(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    uint32_t rendered = 0;
    while (rendered < kTestDurationFrames) {
        uint32_t frames = std::min(kBlockSize, kTestDurationFrames - rendered);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, frames, 0.0);
        output.insert(output.end(), block.begin(), block.begin() + static_cast<ptrdiff_t>(frames * kChannels));
        rendered += frames;
    }
    engine.setTransportPlaying(false);

    // Trim startup transient (gainL ramp + fade-in) and clip-edge fade-out (128 samples)
    // Also apply pan-law gain: engine applies the stereo-balance law (unity at centre).
    constexpr size_t kTrimStart = static_cast<size_t>(kBlockSize) * kChannels;
    constexpr size_t kTrimEnd = static_cast<size_t>(256) * kChannels; // covers CLIP_EDGE_FADE_SAMPLES
    AnalysisResult result;
    result.label = label;
    if (output.size() > kTrimStart + kTrimEnd && refSamples.size() > kTrimStart + kTrimEnd) {
        std::vector<float> outputMid(output.begin() + static_cast<ptrdiff_t>(kTrimStart),
                                     output.end() - static_cast<ptrdiff_t>(kTrimEnd));
        std::vector<float> refMid(refSamples.begin() + static_cast<ptrdiff_t>(kTrimStart),
                                  refSamples.end() - static_cast<ptrdiff_t>(kTrimEnd));
        for (float& s : refMid) s *= static_cast<float>(kPanLawCenterGain);
        result.rmsErrorDb = computeRmsDb(outputMid, refMid);
        result.peakErrorLinear = computePeakError(outputMid, refMid);
        result.correlation = computeCorrelation(outputMid, refMid);
    } else {
        result.rmsErrorDb = 0.0;
        result.peakErrorLinear = 0.0;
        result.correlation = 0.0;
    }
    result.pass = (result.rmsErrorDb <= kPassRmsDb);
    return result;
}

std::vector<float> generateSineRef(double freqHz, float amplitude) {
    std::vector<float> samples(static_cast<size_t>(kTestDurationFrames) * kChannels, 0.0f);
    for (uint32_t i = 0; i < kTestDurationFrames; ++i) {
        float s = static_cast<float>(std::sin(kTau * freqHz * static_cast<double>(i) / kSampleRate)) *
                  amplitude;
        samples[static_cast<size_t>(i) * 2] = s;
        samples[static_cast<size_t>(i) * 2 + 1] = s;
    }
    return samples;
}

std::vector<float> generateImpulseRef(float amplitude) {
    std::vector<float> samples(static_cast<size_t>(kTestDurationFrames) * kChannels, 0.0f);
    constexpr uint32_t kImpulseFrame = kBlockSize + 64;
    samples[static_cast<size_t>(kImpulseFrame) * kChannels] = amplitude;
    samples[static_cast<size_t>(kImpulseFrame) * kChannels + 1] = amplitude;
    return samples;
}

} // namespace

int main() {
    std::cout << "=== Aestra Golden Reference Test Suite ===\n\n";

    std::vector<AnalysisResult> results;
    results.push_back(runSignalTest("Sine_1kHz",  generateSineRef(1000.0,  0.5f)));
    results.push_back(runSignalTest("Sine_440Hz", generateSineRef(440.0,   0.5f)));
    results.push_back(runSignalTest("Sine_10kHz", generateSineRef(10000.0, 0.5f)));
    results.push_back(runSignalTest("Impulse",    generateImpulseRef(0.5f)));

    size_t passed = 0;
    for (const auto& r : results) {
        std::cout << "[" << (r.pass ? "PASS" : "FAIL") << "] " << r.label
                  << "  RMS: " << r.rmsErrorDb << " dB"
                  << "  PeakErr: " << r.peakErrorLinear
                  << "  Corr: " << r.correlation << "\n";
        if (r.pass) ++passed;
    }
    std::cout << "\n" << passed << "/" << results.size() << " passed\n";

    return (passed == results.size() && !results.empty()) ? 0 : 1;
}
