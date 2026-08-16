// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// GoldenAudioRegressionTest — deterministic golden-audio cases for the render
// path. Complements GoldenReferenceTest (sine/impulse fidelity) with the cases
// that catch silent contamination and summing/gain/pan regressions:
//
//   1. Silence — an empty playing project must render EXACT zeros. Catches
//      metronome leakage, test-tone leakage, uninitialized buffers, denormal
//      noise, and any hidden master processing that manufactures output.
//   2. (added below in this series) Impulse through the master path.
//   3. (added below in this series) Multi-track mix with gain and pan.
//
// References are analytic; tolerance policy and update procedure are
// documented in Aestra-Internals: aestra-docs/audio-integrity-infrastructure.md.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "Core/ChannelSlotMap.h"
#include "DSP/ContinuousParamBuffer.h"
#include "DSP/PanLaw.h"
#include "Plugin/SamplerPlugin.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr uint32_t kSeconds = 1;
constexpr double kTau = 6.28318530717958647692;

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
// Case 1: silence / empty project → bit-exact zeros
// -----------------------------------------------------------------------------
bool runSilenceCase() {
    SessionConfig cfg;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    tm->addChannel("Empty"); // a real (empty) channel so the graph is non-trivial

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);

    const uint64_t totalFrames = static_cast<uint64_t>(cfg.sampleRate) * kSeconds;
    std::vector<float> output = renderBlocks(engine, totalFrames, cfg);
    engine.setTransportPlaying(false);

    const std::vector<float> expected(output.size(), 0.0f);
    DiffReport r = compareBuffers(output, expected, cfg.channels, cfg.sampleRate, 0.0);

    // Policy: EXACT zeros. Any nonzero sample in an empty project is
    // manufactured audio and is a bug, full stop.
    const bool pass = (r.maxAbsError == 0.0) && (r.frames == totalFrames);
    printReport("Silence_EmptyProject", r, pass, "exact zeros (0.0, no tolerance)");
    if (r.frames != totalFrames) {
        std::cout << "  render length wrong: got " << r.frames << " frames, expected " << totalFrames
                  << "\n";
    }
    return pass;
}

// -----------------------------------------------------------------------------
// Case 2: single impulse through the master path — exact position + amplitude
// -----------------------------------------------------------------------------
// The impulse is placed past the first block so the transport fade-in has
// completed. Expectation models the engine's centre pan law (stereo-balance: unity at centre).
// A wrong sample rate, a shifted render, or a master-gain change moves or
// scales the impulse and fails with the exact frame in the report.
bool runImpulseCase() {
    SessionConfig cfg;
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;
    const uint32_t impulseFrame = cfg.blockSize + 64;
    constexpr float kAmp = 0.5f;

    std::vector<float> clip(static_cast<size_t>(totalFrames) * 2, 0.0f);
    clip[static_cast<size_t>(impulseFrame) * 2] = kAmp;
    clip[static_cast<size_t>(impulseFrame) * 2 + 1] = kAmp;

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    addAudioTrack(*tm, "Impulse", clip, totalFrames, cfg);

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);
    std::vector<float> output = renderBlocks(engine, totalFrames, cfg);
    engine.setTransportPlaying(false);

    // Expected: pan-law-scaled impulse at the same frame; zeros elsewhere.
    // The impulse passes through a channel strip, which uses the
    // stereo-balance law — unity at centre, matching the direct-to-Master
    // reference. (Previously the strip used equalPower, pinning -3.01 dB at
    // centre; see the strip pan-law fix in the same PR.)
    std::vector<float> expected(output.size(), 0.0f);
    const float panGain = 1.0f;
    expected[static_cast<size_t>(impulseFrame) * 2] = kAmp * panGain;
    expected[static_cast<size_t>(impulseFrame) * 2 + 1] = kAmp * panGain;

    // Trim start (fade-in ramp) and end (clip-edge fade), as GoldenReferenceTest does.
    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * cfg.channels;
    const size_t trimEnd = static_cast<size_t>(256) * cfg.channels;
    std::vector<float> outMid(output.begin() + static_cast<ptrdiff_t>(trimStart),
                              output.end() - static_cast<ptrdiff_t>(trimEnd));
    std::vector<float> expMid(expected.begin() + static_cast<ptrdiff_t>(trimStart),
                              expected.end() - static_cast<ptrdiff_t>(trimEnd));

    // Measured on develop@80fbbf87: bit-exact (maxAbsErr = 0). Tolerance leaves
    // headroom for cross-platform FP/SIMD variation only.
    DiffReport r = compareBuffers(outMid, expMid, cfg.channels, cfg.sampleRate, 1e-6);
    const bool lengthOk = (output.size() == static_cast<size_t>(totalFrames) * cfg.channels);
    const bool pass = (r.rmsErrorDb <= -120.0) && (r.maxAbsError <= 1e-6) && lengthOk;
    printReport("Impulse_MasterPath", r, pass, "RMS <= -120 dB and maxAbs <= 1e-6 after documented trims");
    if (!lengthOk) {
        std::cout << "  render length wrong: got " << output.size() / cfg.channels << " frames, expected "
                  << totalFrames << "\n";
    }
    return pass;
}

// -----------------------------------------------------------------------------
// Case 3: multi-track mix — 3 sines with distinct fader gains and pans, plus a
// bypassed insert on one track (exercises the effect-chain path while keeping
// the expectation analytic; bypass parity is engine policy, AGENTS §11).
// -----------------------------------------------------------------------------
bool runMultiTrackMixCase() {
    SessionConfig cfg;
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;

    struct TrackSpec {
        double freq;
        float amp;
        float faderDb; // mixer fader position (dB) — drives channel volume
        float pan;
    };
    const TrackSpec specs[3] = {
        {440.0, 0.30f, 0.0f, 0.0f},   // unity, centre
        {660.0, 0.20f, -6.0f, -0.5f}, // -6 dB, half-left
        {880.0, 0.15f, -3.0f, 0.75f}, // -3 dB, right-leaning
    };

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    for (int t = 0; t < 3; ++t) {
        addAudioTrack(*tm, "Mix" + std::to_string(t), makeSine(specs[t].freq, specs[t].amp, totalFrames, cfg.sampleRate),
                      totalFrames, cfg);
    }

    // Gain staging contract (updated with the strip gain fix): the mixer
    // fader lives on the channel (track.volume, SetVolumeCommand/undo path)
    // and pan lives on the channel too; the continuous buffer's fader/pan are
    // display mirrors. Trim remains in the continuous slot. Drive the channel
    // stores exactly like the UI's fader/pan commands do.
    const auto faderDbToLinear = [](float faderDb) {
        return faderDb <= -90.0f ? 0.0f : std::pow(10.0f, faderDb / 20.0f);
    };
    for (int t = 0; t < 3; ++t) {
        if (auto* ch = tm->getChannel(static_cast<size_t>(t))) {
            ch->setVolume(faderDbToLinear(specs[t].faderDb));
            ch->setPan(specs[t].pan);
        }
    }

    // Bypassed insert on track 1: the chain still runs its processing path,
    // but a bypassed slot must be acoustically transparent.
    if (auto* ch = tm->getChannel(1)) {
        auto plugin = std::make_shared<Plugins::SamplerPlugin>();
        plugin->initialize(static_cast<double>(cfg.sampleRate), cfg.blockSize);
        ch->getEffectChain().prepare(static_cast<double>(cfg.sampleRate), cfg.blockSize);
        ch->getEffectChain().insertPlugin(0, plugin);
        ch->getEffectChain().setSlotBypassed(0, true);
    }

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);

    engine.setTransportPlaying(true);
    std::vector<float> output = renderBlocks(engine, totalFrames, cfg);
    engine.setTransportPlaying(false);

    // Expected: sum of stereo-balance-scaled, fader-scaled sines. The strip
    // pan law is stereoBalance (unity at centre — equalPower would have
    // applied a second -3.01 dB centre law to already-stereo content).
    std::vector<float> expected(static_cast<size_t>(totalFrames) * 2, 0.0f);
    for (const auto& s : specs) {
        const double gain = static_cast<double>(faderDbToLinear(s.faderDb));
        double gL = 0.0, gR = 0.0;
        PanLaw::stereoBalance(static_cast<double>(s.pan), gain, gL, gR);
        for (uint32_t i = 0; i < totalFrames; ++i) {
            const double v = std::sin(kTau * s.freq * static_cast<double>(i) / cfg.sampleRate) *
                             static_cast<double>(s.amp);
            expected[static_cast<size_t>(i) * 2] += static_cast<float>(v * gL);
            expected[static_cast<size_t>(i) * 2 + 1] += static_cast<float>(v * gR);
        }
    }

    // Trim start generously (fade-in + any fader/pan smoothing ramp) and the
    // clip-edge fade at the end.
    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * 4 * cfg.channels;
    const size_t trimEnd = static_cast<size_t>(256) * cfg.channels;
    std::vector<float> outMid(output.begin() + static_cast<ptrdiff_t>(trimStart),
                              output.end() - static_cast<ptrdiff_t>(trimEnd));
    std::vector<float> expMid(expected.begin() + static_cast<ptrdiff_t>(trimStart),
                              expected.end() - static_cast<ptrdiff_t>(trimEnd));

    // Measured on develop@80fbbf87: maxAbsErr = 5.96e-8 (~1 float ulp at this
    // scale), RMS = -159.8 dB. Tolerance leaves headroom for cross-platform
    // FP/SIMD reassociation while staying a real tripwire.
    DiffReport r = compareBuffers(outMid, expMid, cfg.channels, cfg.sampleRate, 1e-6);
    const bool pass = (r.rmsErrorDb <= -120.0) && (r.maxAbsError <= 1e-6);
    printReport("MultiTrack_Gain_Pan_BypassedFX", r, pass,
                "RMS <= -120 dB and maxAbs <= 1e-6 after documented trims");
    return pass;
}

// -----------------------------------------------------------------------------
// Case 0 (meta): prove the diff forensics themselves are correct, so a future
// failure report can be trusted without re-deriving it by hand.
// -----------------------------------------------------------------------------
bool runDiffSelfCheck() {
    // Stereo, 4 frames. Inject a known mismatch at frame 2, channel R.
    const std::vector<float> expected = {0.0f, 0.0f, 0.1f, 0.1f, 0.2f, 0.2f, 0.3f, 0.3f};
    std::vector<float> actual = expected;
    actual[5] += 0.25f; // frame 2, channel 1

    DiffReport r = compareBuffers(actual, expected, 2, 48000, 1e-6);
    // Inputs are float32, so 0.45f − 0.2f only equals 0.25 to float precision.
    const bool pass = r.firstMismatchFrame == 2 && r.firstMismatchChannel == 1 &&
                      std::abs(r.maxAbsError - 0.25) < 1e-6 && r.frames == 4 &&
                      std::abs(r.peakActual - 0.45) < 1e-6 && std::abs(r.peakExpected - 0.3) < 1e-6;
    std::cout << "[" << (pass ? "PASS" : "FAIL") << "] DiffReport_SelfCheck"
              << "  (mismatch localized to frame " << r.firstMismatchFrame << ", ch "
              << r.firstMismatchChannel << ")\n";
    return pass;
}

} // namespace

int main() {
    std::cout << "=== Aestra Golden Audio Regression Suite ===\n\n";
    int failures = 0;
    if (!runDiffSelfCheck()) ++failures;
    if (!runSilenceCase()) ++failures;
    if (!runImpulseCase()) ++failures;
    if (!runMultiTrackMixCase()) ++failures;

    std::cout << "\n" << (failures == 0 ? "ALL PASS" : "FAILURES: " + std::to_string(failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
