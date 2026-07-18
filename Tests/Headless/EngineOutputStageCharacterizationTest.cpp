// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// EngineOutputStageCharacterizationTest — Pins observable behavior of the
// AudioEngine::processBlock output stage ahead of its decomposition:
//   * silence in -> silence out (no signal invented by the output chain)
//   * test-tone injection: 440 Hz at -26 dBFS on both channels
//   * waveform history ring: captureWaveformHistory/copyWaveformHistory keep
//     the most recent frames in chronological order across ring wraps (the
//     capture side is driven by the device callback in AestraAudioController,
//     so the ring pair is exercised directly here)
//   * master meter publication into MeterSnapshotBuffer (peak/RMS at slot 127)
//   * LUFS pipeline: block energy accumulated in processBlock reaches the
//     loudness worker and produces a plausible integrated LUFS readout
//
// These are characterization tests: they encode what the engine does today so
// the renderGraph/processBlock refactor can prove it changed nothing.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/ChannelSlotMap.h"
#include "Models/MeterSnapshot.h"
#include "Models/TrackManager.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kBlockFrames = 512;

// Test tone constants owned by AudioEngine::processBlock ("Test Tone Injection").
constexpr double kToneFrequencyHz = 440.0;
constexpr float kToneAmplitude = 0.05f; // -26 dBFS

struct EngineFixture {
    std::shared_ptr<TrackManager> trackManager;
    std::shared_ptr<MeterSnapshotBuffer> meters;
    std::unique_ptr<AudioEngine> engine;
};

EngineFixture makeFixture() {
    EngineFixture f;
    f.trackManager = std::make_shared<TrackManager>();
    f.trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));
    f.meters = std::make_shared<MeterSnapshotBuffer>();

    f.engine = std::make_unique<AudioEngine>();
    f.engine->setSampleRate(kSampleRate);
    f.engine->setBufferConfig(kBlockFrames, kChannels);
    f.engine->setTrackManager(f.trackManager);
    f.engine->setGraph(AudioGraphBuilder::buildFromTrackManager(*f.trackManager));
    f.engine->setMeterSnapshots(f.meters);
    f.engine->initialize();
    f.engine->setMetronomeEnabled(false);
    return f;
}

// Renders totalFrames through processBlock, appending to out (interleaved).
void renderBlocks(AudioEngine& engine, uint32_t totalFrames, std::vector<float>& out) {
    std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    uint32_t rendered = 0;
    while (rendered < totalFrames) {
        const uint32_t frames = std::min(kBlockFrames, totalFrames - rendered);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, frames, 0.0);
        out.insert(out.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(frames * kChannels));
        rendered += frames;
    }
}

float peakOf(const std::vector<float>& samples, size_t begin = 0) {
    float peak = 0.0f;
    for (size_t i = begin; i < samples.size(); ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }
    return peak;
}

// Estimates the dominant frequency of channel 0 via zero-crossing count.
double zeroCrossingFrequency(const std::vector<float>& interleaved, uint32_t sampleRate) {
    size_t crossings = 0;
    size_t frames = interleaved.size() / kChannels;
    for (size_t i = 1; i < frames; ++i) {
        const float prev = interleaved[(i - 1) * kChannels];
        const float cur = interleaved[i * kChannels];
        if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f)) {
            ++crossings;
        }
    }
    const double seconds = static_cast<double>(frames) / static_cast<double>(sampleRate);
    return seconds > 0.0 ? static_cast<double>(crossings) / (2.0 * seconds) : 0.0;
}

bool testSilencePassthrough() {
    EngineFixture f = makeFixture();
    std::vector<float> out;
    renderBlocks(*f.engine, kSampleRate / 2, out); // 0.5 s

    const float peak = peakOf(out);
    if (peak > 1e-4f) {
        std::cerr << "[FAIL] silence: output stage invented signal, peak=" << peak << "\n";
        return false;
    }
    std::cout << "[PASS] silence in -> silence out (peak=" << peak << ")\n";
    return true;
}

bool testToneInjectionAndWaveformHistory() {
    EngineFixture f = makeFixture();
    f.engine->setTestToneEnabled(true);

    std::vector<float> out;
    renderBlocks(*f.engine, kSampleRate, out); // 1 s of tone

    // Skip the first 0.1 s so one-pole safety filters settle before measuring.
    const size_t steadyBegin = static_cast<size_t>(kSampleRate / 10) * kChannels;
    const float peak = peakOf(out, steadyBegin);
    if (peak < kToneAmplitude * 0.85f || peak > kToneAmplitude * 1.15f) {
        std::cerr << "[FAIL] tone: peak " << peak << " outside +/-15% of " << kToneAmplitude << "\n";
        return false;
    }

    std::vector<float> steady(out.begin() + static_cast<std::ptrdiff_t>(steadyBegin), out.end());
    const double freq = zeroCrossingFrequency(steady, kSampleRate);
    if (freq < kToneFrequencyHz - 5.0 || freq > kToneFrequencyHz + 5.0) {
        std::cerr << "[FAIL] tone: frequency " << freq << " Hz not ~" << kToneFrequencyHz << " Hz\n";
        return false;
    }

    // Both channels carry the same mono tone.
    float maxLrDiff = 0.0f;
    for (size_t i = steadyBegin; i + 1 < out.size(); i += kChannels) {
        maxLrDiff = std::max(maxLrDiff, std::abs(out[i] - out[i + 1]));
    }
    if (maxLrDiff > 1e-3f) {
        std::cerr << "[FAIL] tone: L/R diverged by " << maxLrDiff << "\n";
        return false;
    }

    std::cout << "[PASS] test tone (peak=" << peak << ", freq=" << freq << " Hz)\n";
    return true;
}

// Pins the waveform-history ring pair: after pushing more frames than the ring
// holds, copyWaveformHistory returns exactly the last `cap` frames in
// chronological order. Capture is fed directly (the production caller is the
// device callback in AestraAudioController, outside processBlock).
bool testWaveformHistoryRing() {
    EngineFixture f = makeFixture();

    const uint32_t cap = f.engine->getWaveformHistoryCapacity();
    if (cap == 0) {
        std::cerr << "[FAIL] history: capacity is 0 after initialize()\n";
        return false;
    }

    // Push cap + extra frames of a ramp in uneven chunks to force a wrap.
    const uint32_t total = cap + cap / 3 + 41;
    std::vector<float> pushed(static_cast<size_t>(total) * kChannels);
    for (uint32_t frame = 0; frame < total; ++frame) {
        pushed[static_cast<size_t>(frame) * 2] = static_cast<float>(frame);
        pushed[static_cast<size_t>(frame) * 2 + 1] = -static_cast<float>(frame);
    }
    const uint32_t chunks[] = {193, 512, 480, 64};
    uint32_t sent = 0;
    size_t chunkIdx = 0;
    while (sent < total) {
        const uint32_t n = std::min(chunks[chunkIdx % 4], total - sent);
        f.engine->captureWaveformHistory(pushed.data() + static_cast<size_t>(sent) * kChannels, n);
        sent += n;
        ++chunkIdx;
    }

    std::vector<float> history(static_cast<size_t>(cap) * kChannels, 0.0f);
    const uint32_t got = f.engine->copyWaveformHistory(history.data(), cap);
    if (got != cap) {
        std::cerr << "[FAIL] history: copied " << got << " frames, expected " << cap << "\n";
        return false;
    }
    const size_t tailStart = static_cast<size_t>(total - cap) * kChannels;
    for (size_t i = 0; i < static_cast<size_t>(cap) * kChannels; ++i) {
        if (history[i] != pushed[tailStart + i]) {
            std::cerr << "[FAIL] history: sample " << i << " differs (" << history[i]
                      << " vs pushed " << pushed[tailStart + i] << ")\n";
            return false;
        }
    }

    std::cout << "[PASS] waveform history ring (cap=" << cap << ", wrap preserved, tail byte-equal)\n";
    return true;
}

bool testMasterMeterAndLufsPipeline() {
    EngineFixture f = makeFixture();
    f.engine->setTestToneEnabled(true);

    // > 2 LUFS accumulation windows (19200 samples each) so processBlock
    // pushes energy blocks to the loudness worker.
    std::vector<float> out;
    renderBlocks(*f.engine, kSampleRate, out);

    const auto& slot = f.meters->slots[ChannelSlotMap::MASTER_SLOT_INDEX];
    const float peakL = slot.peakL.load(std::memory_order_relaxed);
    const float peakR = slot.peakR.load(std::memory_order_relaxed);
    const float rmsL = slot.rmsL.load(std::memory_order_relaxed);

    if (peakL < kToneAmplitude * 0.7f || peakL > kToneAmplitude * 1.3f || peakR < kToneAmplitude * 0.7f) {
        std::cerr << "[FAIL] meters: master peak L=" << peakL << " R=" << peakR
                  << " not ~" << kToneAmplitude << "\n";
        return false;
    }
    // Sine RMS = amplitude / sqrt(2); the meter ballistics may lag, so bound loosely.
    const float expectedRms = kToneAmplitude / 1.41421356f;
    if (rmsL < expectedRms * 0.4f || rmsL > expectedRms * 1.6f) {
        std::cerr << "[FAIL] meters: master RMS L=" << rmsL << " not ~" << expectedRms << "\n";
        return false;
    }
    if (slot.clipL.load(std::memory_order_relaxed) != 0 || slot.clipR.load(std::memory_order_relaxed) != 0) {
        std::cerr << "[FAIL] meters: clip flagged for a -26 dBFS tone\n";
        return false;
    }

    // Integrated LUFS is computed by the background loudness worker from the
    // energy blocks processBlock queued above. A 440 Hz sine at -26 dBFS on
    // both channels lands near -26.7 LUFS under BS.1770 K-weighting; accept a
    // wide band — the pin is that the pipeline produces a sane value at all.
    float integrated = -144.0f;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        // Keep feeding blocks so gating windows fill regardless of worker cadence.
        renderBlocks(*f.engine, 19200, out);
        integrated = slot.integratedLufs.load(std::memory_order_relaxed);
        if (integrated > -100.0f) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (integrated < -35.0f || integrated > -18.0f) {
        std::cerr << "[FAIL] lufs: integrated=" << integrated
                  << " LUFS, expected roughly -26.7 for the test tone\n";
        return false;
    }

    std::cout << "[PASS] master meters (peak=" << peakL << ", rms=" << rmsL
              << ") + LUFS pipeline (integrated=" << integrated << " LUFS)\n";
    return true;
}

} // namespace

int main() {
    const bool silenceOk = testSilencePassthrough();
    const bool toneOk = testToneInjectionAndWaveformHistory();
    const bool ringOk = testWaveformHistoryRing();
    const bool metersOk = testMasterMeterAndLufsPipeline();
    return (silenceOk && toneOk && ringOk && metersOk) ? 0 : 1;
}
