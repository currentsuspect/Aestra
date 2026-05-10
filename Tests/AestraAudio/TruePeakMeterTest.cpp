// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Phase 2 — True Peak Metering tests (ITU-R BS.1770-4 inspired).
//
// Goals:
//   1. Resetting and silence produce zero peaks.
//   2. A clean sine reports true peak ≥ sample peak and ≈ amplitude.
//   3. The classic intersample worst case (alternating ±A) reports a true
//      peak strictly greater than the sample peak — and within a sane bound.
//   4. Running peaks accumulate (max-hold) and reset() clears them.

#include "DSP/TruePeakMeter.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using Aestra::Audio::TruePeakMeter;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeStereoSine(double freqHz, double sampleRate, double seconds, float amplitude) {
    const auto numFrames = static_cast<size_t>(sampleRate * seconds);
    std::vector<float> buf(numFrames * 2, 0.0f);
    const double w = 2.0 * kPi * freqHz / sampleRate;
    for (size_t i = 0; i < numFrames; ++i) {
        const float s = static_cast<float>(amplitude * std::sin(w * static_cast<double>(i)));
        buf[i * 2 + 0] = s;
        buf[i * 2 + 1] = s;
    }
    return buf;
}

std::vector<float> makeStereoAlternating(float amplitude, size_t numFrames) {
    std::vector<float> buf(numFrames * 2, 0.0f);
    for (size_t i = 0; i < numFrames; ++i) {
        const float s = (i & 1u) ? -amplitude : amplitude;
        buf[i * 2 + 0] = s;
        buf[i * 2 + 1] = s;
    }
    return buf;
}

void testSilenceAndReset() {
    std::printf("Test: silence and reset...\n");
    TruePeakMeter meter;
    meter.initialize(48000);

    std::array<float, 2 * 256> silence{}; // zero-initialised
    meter.processStereo(silence.data(), 256);

    assert(meter.getMaxSamplePeak() == 0.0f);
    assert(meter.getMaxTruePeak() == 0.0f);
    assert(meter.getMaxTruePeakdBTP() <= -100.0f);

    // Run something non-zero, then reset, expect zero again.
    auto sine = makeStereoSine(1000.0, 48000.0, 0.05, 0.5f);
    meter.processStereo(sine.data(), sine.size() / 2);
    assert(meter.getMaxTruePeak() > 0.0f);

    meter.reset();
    assert(meter.getMaxSamplePeak() == 0.0f);
    assert(meter.getMaxTruePeak() == 0.0f);
    std::printf("  [PASS] silence -> 0; reset() clears running peaks\n");
}

void testSineDetection() {
    std::printf("Test: sine wave true peak >= sample peak, near amplitude...\n");
    TruePeakMeter meter;
    meter.initialize(48000);

    // 997 Hz sine at -3 dBFS (≈ 0.7079). 997 is intentionally not a sample-rate
    // sub-multiple so sample peak is typically slightly below amplitude while
    // true peak should reconstruct near amplitude.
    const float amp = 0.7079457f;
    auto sine = makeStereoSine(997.0, 48000.0, 0.5, amp);
    meter.processStereo(sine.data(), sine.size() / 2);

    const float sp = meter.getMaxSamplePeak();
    const float tp = meter.getMaxTruePeak();
    std::printf("  sample peak = %.6f (%.3f dBFS)\n", sp, 20.0f * std::log10(sp));
    std::printf("  true   peak = %.6f (%.3f dBTP)\n", tp, 20.0f * std::log10(tp));

    assert(tp + 1e-6f >= sp);                                 // true peak ≥ sample peak
    assert(std::fabs(tp - amp) < 0.05f);                      // within 5% of amplitude
    assert(tp <= amp * 1.30f);                                // not absurdly high
    std::printf("  [PASS] sine: true peak in [sample peak, 1.3*amp]\n");
}

void testIntersamplePeak() {
    std::printf("Test: alternating ±A intersample worst case...\n");
    TruePeakMeter meter;
    meter.initialize(48000);

    // Alternating ±0.9 — sample peak is exactly 0.9, but the band-limited
    // reconstruction can spike well above (theoretical worst case 4/π ≈ 1.273x
    // for ideal sinc; our windowed FIR is lower but should still exceed sp).
    const float amp = 0.9f;
    auto signal = makeStereoAlternating(amp, 4096);
    meter.processStereo(signal.data(), signal.size() / 2);

    const float sp = meter.getMaxSamplePeak();
    const float tp = meter.getMaxTruePeak();
    std::printf("  sample peak = %.6f (%.3f dBFS)\n", sp, 20.0f * std::log10(sp));
    std::printf("  true   peak = %.6f (%.3f dBTP)\n", tp, 20.0f * std::log10(tp));

    assert(std::fabs(sp - amp) < 1e-4f);
    assert(tp > sp + 1e-3f);                                  // strictly higher
    assert(tp >= amp * 1.10f);                                // at least +0.83 dB lift
    assert(tp <= amp * 1.40f);                                // sane upper bound
    std::printf("  [PASS] intersample: true peak exceeds sample peak by >10%%\n");
}

void testMaxHoldAndReset() {
    std::printf("Test: max-hold across blocks, reset clears...\n");
    TruePeakMeter meter;
    meter.initialize(48000);

    // First block: loud transient.
    auto loud = makeStereoSine(440.0, 48000.0, 0.05, 0.95f);
    meter.processStereo(loud.data(), loud.size() / 2);
    const float tpLoud = meter.getMaxTruePeak();
    assert(tpLoud > 0.9f);

    // Subsequent quiet block must not lower the running peak.
    auto quiet = makeStereoSine(440.0, 48000.0, 0.05, 0.10f);
    meter.processStereo(quiet.data(), quiet.size() / 2);
    const float tpAfter = meter.getMaxTruePeak();
    assert(tpAfter >= tpLoud - 1e-6f);

    // Reset clears.
    meter.reset();
    assert(meter.getMaxTruePeak() == 0.0f);

    // After reset, the same quiet block reports a small peak.
    meter.processStereo(quiet.data(), quiet.size() / 2);
    assert(meter.getMaxTruePeak() > 0.0f);
    assert(meter.getMaxTruePeak() < 0.3f);
    std::printf("  [PASS] max-hold semantics + reset() correct\n");
}

void testDcGain() {
    std::printf("Test: DC steady-state unity gain...\n");
    TruePeakMeter meter;
    meter.initialize(48000);

    // A constant signal — but cold-start (zero history -> DC) produces a
    // small step-response overshoot from the windowed-sinc FIR (~10-13%).
    // That is the meter correctly reporting the band-limited reconstruction
    // of an instantaneous 0->A step. To validate STEADY-STATE DC gain we
    // first run enough samples to fully fill the FIR history, then call
    // clearPeaks() (which preserves history) and measure again.
    std::array<float, 2 * 256> dc{};
    constexpr float amp = 0.5f;
    for (size_t i = 0; i < 256; ++i) {
        dc[i * 2 + 0] = amp;
        dc[i * 2 + 1] = amp;
    }

    // Phase A: cold-start — observe step-response overshoot is bounded.
    meter.processStereo(dc.data(), 256);
    const float tpStep = meter.getMaxTruePeak();
    std::printf("  cold-start step  -> true peak %.6f (step-response, expected slight overshoot)\n", tpStep);
    assert(tpStep >= amp - 1e-4f);   // at least the input level
    assert(tpStep <= amp * 1.20f);   // bounded ringing (≤ 20%)

    // Phase B: history now full of `amp`. Reset peaks only and measure again.
    meter.clearPeaks();
    meter.processStereo(dc.data(), 256);
    const float tpSteady = meter.getMaxTruePeak();
    std::printf("  steady-state DC  -> true peak %.6f (must be ≈ amplitude)\n", tpSteady);
    assert(std::fabs(tpSteady - amp) < 0.005f); // within 0.5% in steady state
    std::printf("  [PASS] step response bounded; steady-state DC gain == 1.0\n");
}

} // namespace

int main() {
    std::printf("=== True Peak Meter (Phase 2) Tests ===\n\n");

    testSilenceAndReset();
    testSineDetection();
    testIntersamplePeak();
    testMaxHoldAndReset();
    testDcGain();

    std::printf("\n=== All True Peak Meter Tests Passed ===\n");
    return 0;
}
