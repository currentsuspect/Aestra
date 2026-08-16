// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SampleRateBufferTruthTest — sample-rate correctness and buffer-boundary
// behavior of the render path (audio-integrity series, area 5).
//
// Cases:
//   1. 96 kHz end-to-end: an impulse in a 96 kHz session lands at the exact
//      96 kHz frame position with the exact pan-law amplitude, and the
//      rendered length is exactly the requested number of samples. Wrong
//      sample-rate handling moves or smears the impulse — unmissable.
//   2. Cross-rate truth: a 48 kHz clip in a 96 kHz engine must play at the
//      correct pitch/time via the SRC path. Compared against the analytic
//      96 kHz reference with a documented (looser) tolerance.
//   3. Buffer-size independence: identical session rendered at block sizes
//      64 / 193 / 512 / 1024 / 2048 must produce identical audio. 193 is
//      deliberately odd to stress non-power-of-two boundaries.
//   4. Output-overwrite guarantee: processBlock must fully overwrite the
//      device buffer. Blocks are pre-filled with a sentinel; any surviving
//      sentinel means stale/unwritten output — the driver does NOT zero
//      device buffers, so accumulate-instead-of-overwrite would play garbage.
//      Also verifies exact silence after the clip ends (no stale data across
//      buffer boundaries).

#include "GoldenAudio/GoldenAudioHarness.h"

#include "DSP/PanLaw.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kTau = 6.28318530717958647692;
int g_failures = 0;

void verdict(bool pass, const std::string& label) {
    std::cout << "[" << (pass ? "PASS" : "FAIL") << "] " << label << "\n";
    if (!pass) ++g_failures;
}

std::vector<float> makeSine(double freqHz, float amplitude, uint32_t frames, uint32_t sampleRate) {
    std::vector<float> s(static_cast<size_t>(frames) * 2, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * freqHz * static_cast<double>(i) / sampleRate)) *
                        amplitude;
        s[static_cast<size_t>(i) * 2] = v;
        s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

// -----------------------------------------------------------------------------
// Case 1: 96 kHz impulse — exact position, amplitude, and render length
// -----------------------------------------------------------------------------
void run96kImpulseCase() {
    SessionConfig cfg;
    cfg.sampleRate = 96000;
    const uint32_t totalFrames = cfg.sampleRate; // 1 s
    const uint32_t impulseFrame = cfg.blockSize + 64;
    constexpr float kAmp = 0.5f;

    std::vector<float> clip(static_cast<size_t>(totalFrames) * 2, 0.0f);
    clip[static_cast<size_t>(impulseFrame) * 2] = kAmp;
    clip[static_cast<size_t>(impulseFrame) * 2 + 1] = kAmp;

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    addAudioTrack(*tm, "Imp96k", clip, totalFrames, cfg);

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);
    std::vector<float> output = renderBlocks(engine, totalFrames, cfg);
    engine.setTransportPlaying(false);

    verdict(output.size() == static_cast<size_t>(totalFrames) * cfg.channels,
            "96k: render length exactly " + std::to_string(totalFrames) + " frames");

    // Locate the peak sample; it must be at the exact impulse frame.
    size_t peakIdx = 0;
    float peakVal = 0.0f;
    for (size_t i = 0; i < output.size(); ++i) {
        if (std::abs(output[i]) > peakVal) {
            peakVal = std::abs(output[i]);
            peakIdx = i;
        }
    }
    const uint32_t peakFrame = static_cast<uint32_t>(peakIdx / cfg.channels);
    verdict(peakFrame == impulseFrame,
            "96k: impulse at exact frame (got " + std::to_string(peakFrame) + ", want " +
                std::to_string(impulseFrame) + ")");
    // stereo-balance law: unity at center (strip pan-law fix 2026-08-14)
    const float expectedAmp = kAmp * 1.0f;
    verdict(std::abs(peakVal - expectedAmp) <= 1e-6f,
            "96k: impulse amplitude = pan-law expectation (got " + std::to_string(peakVal) + ")");
}

// -----------------------------------------------------------------------------
// Case 2: 48 kHz clip in a 96 kHz engine — correct pitch/time via SRC
// -----------------------------------------------------------------------------
void runCrossRateCase() {
    SessionConfig cfg;
    cfg.sampleRate = 96000;
    const uint32_t engineFrames = cfg.sampleRate; // 1 s at 96 kHz
    const uint32_t clipRate = 48000;
    const uint32_t clipFrames = clipRate; // 1 s of source material

    // Build the clip at 48 kHz (buffer declares its true rate).
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    {
        auto* channel = tm->addChannel("Cross48in96");
        auto buffer = std::make_shared<AudioBufferData>();
        buffer->sampleRate = clipRate;
        buffer->numChannels = 2;
        buffer->numFrames = clipFrames;
        buffer->interleavedData = makeSine(1000.0, 0.5f, clipFrames, clipRate);
        const std::string path = "/tmp/aestra_cross48.wav";
        ClipSourceID sourceId = tm->getSourceManager().createRecordedSource(path, "Cross48", buffer);
        AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = 1.0;
        payload.slices.push_back({0.0, 1.0, 0.0, static_cast<double>(clipFrames)});
        PlaylistLaneID laneId = tm->getPlaylistModel().createLane("Cross48");
        PatternID patternId = tm->getPatternManager().createAudioPattern("Cross48", 2.0, payload);
        if (channel) {
            tm->getPatternManager().setPatternMixerChannel(patternId, channel->getChannelId());
        }
        const ClipInstanceID clipId = tm->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, 2.0);
        if (!tm->getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
            verdict(false, "Cross-rate fixture uses unity clip gain");
            return;
        }
    }

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);
    std::vector<float> output = renderBlocks(engine, engineFrames, cfg);
    engine.setTransportPlaying(false);

    // Expected: a 1 kHz sine at 96 kHz, pan-law scaled. If the engine ignored
    // the clip's rate it would play 2x too fast (2 kHz) and correlation with
    // the 1 kHz reference collapses toward 0.
    // Stereo-balance law: unity at center (strip pan-law fix 2026-08-14).
    std::vector<float> expected = makeSine(1000.0, 0.5f, engineFrames, cfg.sampleRate);

    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * 4 * cfg.channels;
    const size_t trimEnd = static_cast<size_t>(4096) * cfg.channels; // SRC tail headroom
    std::vector<float> outMid(output.begin() + static_cast<ptrdiff_t>(trimStart),
                              output.end() - static_cast<ptrdiff_t>(trimEnd));
    std::vector<float> expMid(expected.begin() + static_cast<ptrdiff_t>(trimStart),
                              expected.end() - static_cast<ptrdiff_t>(trimEnd));

    DiffReport r = compareBuffers(outMid, expMid, cfg.channels, cfg.sampleRate, 1e-3);
    // Measured baseline after the project-rate fix: -118.3 dB RMS / 2.4e-6
    // maxAbs. -80 dB keeps cross-platform SRC headroom while still catching a
    // pitch/time error instantly (a 2x pitch error measures ~-3 dB).
    const bool pass = (r.rmsErrorDb <= -80.0);
    printReport("CrossRate_48kClip_in_96kEngine", r, pass,
                "RMS <= -80 dB vs analytic 1 kHz @ 96 kHz (SRC path; measured -118 dB)");
    if (!pass) ++g_failures;
}

// -----------------------------------------------------------------------------
// Case 3: buffer-size independence — 64/193/512/1024/2048 render identically
// -----------------------------------------------------------------------------
void runBufferSizeSweepCase() {
    const uint32_t kRate = 48000;
    const uint32_t totalFrames = kRate; // 1 s
    const std::vector<float> clip = makeSine(440.0, 0.4f, totalFrames, kRate);

    auto renderWithBlock = [&](uint32_t blockSize) {
        SessionConfig cfg;
        cfg.sampleRate = kRate;
        cfg.blockSize = blockSize;
        auto tm = std::make_shared<TrackManager>();
        tm->setOutputSampleRate(static_cast<double>(kRate));
        addAudioTrack(*tm, "Sweep" + std::to_string(blockSize), clip, totalFrames, cfg);
        AudioEngine engine;
        prepareEngine(engine, tm, cfg);
        engine.setTransportPlaying(true);
        std::vector<float> out = renderBlocks(engine, totalFrames, cfg);
        engine.setTransportPlaying(false);
        return out;
    };

    const std::vector<float> baseline = renderWithBlock(512);
    for (uint32_t bs : {64u, 193u, 1024u, 2048u}) {
        std::vector<float> other = renderWithBlock(bs);
        // Trim the transport fade-in region, whose length legitimately depends
        // on block size; steady state must be identical.
        const size_t trim = static_cast<size_t>(4096) * 2;
        const size_t n = std::min(baseline.size(), other.size());
        std::vector<float> a(baseline.begin() + static_cast<ptrdiff_t>(trim),
                             baseline.begin() + static_cast<ptrdiff_t>(n));
        std::vector<float> b(other.begin() + static_cast<ptrdiff_t>(trim),
                             other.begin() + static_cast<ptrdiff_t>(n));
        DiffReport r = compareBuffers(a, b, 2, kRate, 1e-6);
        const bool pass = (r.maxAbsError <= 1e-6);
        verdict(pass, "buffer sweep: block " + std::to_string(bs) + " == block 512 (maxAbs " +
                          std::to_string(r.maxAbsError) + ")");
        if (!pass) {
            printReport("BufferSweep_" + std::to_string(bs), r, false, "maxAbs <= 1e-6 vs 512 baseline");
        }
    }
}

// -----------------------------------------------------------------------------
// Case 4: output-overwrite guarantee + exact silence after clip end
// -----------------------------------------------------------------------------
void runSentinelOverwriteCase() {
    SessionConfig cfg;
    const uint32_t clipFrames = cfg.sampleRate / 2; // 0.5 s of material
    const uint32_t renderFrames = cfg.sampleRate;   // render a full second
    const std::vector<float> clip = makeSine(440.0, 0.4f, clipFrames, cfg.sampleRate);

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    addAudioTrack(*tm, "Sentinel", clip, clipFrames, cfg);

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);

    constexpr float kSentinel = 0.123456f;
    std::vector<float> output;
    output.reserve(static_cast<size_t>(renderFrames) * cfg.channels);
    std::vector<float> block(static_cast<size_t>(cfg.blockSize) * cfg.channels, 0.0f);
    uint32_t rendered = 0;
    while (rendered < renderFrames) {
        const uint32_t frames = std::min(cfg.blockSize, renderFrames - rendered);
        std::fill(block.begin(), block.end(), kSentinel); // device buffers are NOT zeroed
        engine.processBlock(block.data(), nullptr, frames, 0.0);
        output.insert(output.end(), block.begin(),
                      block.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(frames) * cfg.channels));
        rendered += frames;
    }
    engine.setTransportPlaying(false);

    // 4a: no sentinel value may survive anywhere (overwrite guarantee).
    size_t sentinelSurvivors = 0;
    for (float s : output) {
        if (s == kSentinel) ++sentinelSurvivors;
    }
    verdict(sentinelSurvivors == 0,
            "sentinel: processBlock fully overwrites the device buffer (" +
                std::to_string(sentinelSurvivors) + " survivors)");

    // 4b: after the clip ends (+ generous fade allowance), output is EXACT zeros.
    const size_t tailStart = (static_cast<size_t>(clipFrames) + 4096) * cfg.channels;
    size_t nonZeroTail = 0;
    float maxTail = 0.0f;
    for (size_t i = tailStart; i < output.size(); ++i) {
        if (output[i] != 0.0f) {
            ++nonZeroTail;
            maxTail = std::max(maxTail, std::abs(output[i]));
        }
    }
    verdict(nonZeroTail == 0, "tail: exact silence after clip end (" + std::to_string(nonZeroTail) +
                                  " nonzero samples, max " + std::to_string(maxTail) + ")");
}

} // namespace

int main() {
    std::cout << "=== Aestra Sample-Rate & Buffer Truth Test ===\n\n";
    run96kImpulseCase();
    runCrossRateCase();
    runBufferSizeSweepCase();
    runSentinelOverwriteCase();
    std::cout << "\n" << (g_failures == 0 ? "ALL PASS" : "FAILURES: " + std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
