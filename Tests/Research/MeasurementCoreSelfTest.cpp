// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MeasurementCoreSelfTest — proves the Audio Research Bench measurement core against
// closed-form analytic expectations before any engine/DSP code is measured with it.
//
// Every expectation below is derived mathematically from the generator definition,
// never from a recorded buffer. If a check fails, either the generator or the
// measurement is wrong — both live in Tests/Research/.
//
// Doc: Aestra-Internals: aestra-docs/audio-research-bench.md

#include "AudioMeasure.h"
#include "SignalLab.h"

#include <cmath>
#include <cstdio>

using namespace AudioResearch;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kFrames = 48000; // 1 s => integer cycle counts for all chosen tones

void testSilence(CheckSession& t) {
    std::printf("\n=== silence ===\n");
    const Signal s = makeSilence(kRate, kFrames);
    t.expectNear("silence peak == 0", peak(s), 0.0, 0.0);
    t.expectNear("silence RMS == 0", rms(s), 0.0, 0.0);
    t.expectNear("silence DC == 0", dcOffset(s), 0.0, 0.0);
    t.expect("silence has no non-silent frame", firstNonSilentFrame(s) == -1);
}

void testDCAndStep(CheckSession& t) {
    std::printf("\n=== DC / step ===\n");
    const Signal dc = makeDC(kRate, kFrames, 0.25); // 0.25 is exactly representable in float
    t.expectNear("DC offset == 0.25", dcOffset(dc), 0.25, 0.0);
    t.expectNear("DC RMS == 0.25", rms(dc), 0.25, 0.0);
    t.expectNear("DC peak == 0.25", peak(dc), 0.25, 0.0);

    const Signal st = makeStep(kRate, kFrames, 24000, 0.75);
    t.expectNear("step: pre-step DC == 0", dcOffset(st, -1, 0, 24000), 0.0, 0.0);
    t.expectNear("step: post-step DC == level", dcOffset(st, -1, 24000, kFrames), 0.75, 0.0);
    t.expect("step: first non-silent frame is the step", firstNonSilentFrame(st) == 24000);
}

void testSine(CheckSession& t) {
    std::printf("\n=== sine (1 kHz, amp 0.5, integer cycles) ===\n");
    const Signal s = makeSine(kRate, kFrames, 1000.0, 0.5);
    // 48 samples/cycle; sample 12 sits exactly on the crest -> peak == amplitude.
    t.expectNear("sine peak == amplitude", peak(s), 0.5, 1e-7);
    t.expectNear("sine RMS == A/sqrt(2)", rms(s), 0.5 / std::sqrt(2.0), 1e-6);
    t.expectNear("sine DC == 0", dcOffset(s), 0.0, 1e-7);

    const ToneFit fit = fitTone(s, 0, 1000.0);
    t.expectNear("fitTone recovers amplitude", fit.amplitude, 0.5, 1e-7);
    t.expectNear("fitTone recovers DC", fit.dc, 0.0, 1e-7);
    // Residual should be float-quantization noise only; ~152 dB in practice.
    t.expect("fitTone residual SINAD > 120 dB (float quantization floor)", fit.sinadDb > 120.0,
             "sinadDb=" + std::to_string(fit.sinadDb));
    // Orthogonality: a tone probe at a different integer-cycle frequency sees ~nothing.
    t.expectNear("off-frequency probe (3 kHz) sees no energy", toneAmplitude(s, 0, 3000.0), 0.0, 1e-6);

    const Signal nyq = makeNearNyquistSine(kRate, kFrames, 0.5, 0.9); // 21.6 kHz, integer cycles
    t.expectNear("near-Nyquist fitTone recovers amplitude", toneAmplitude(nyq, 0, 21600.0), 0.5, 1e-6);
}

void testDualTone(CheckSession& t) {
    std::printf("\n=== dual tone (component separation) ===\n");
    const Signal s = makeDualTone(kRate, kFrames, 1000.0, 0.4, 3000.0, 0.04);
    t.expectNear("dual tone: f1 amplitude recovered", toneAmplitude(s, 0, 1000.0), 0.4, 1e-6);
    t.expectNear("dual tone: f2 amplitude recovered", toneAmplitude(s, 0, 3000.0), 0.04, 1e-6);

    // With f2 = 3*f1 the "harmonic distortion" is exactly a2/a1 = 0.1 (=-20 dB).
    const HarmonicReport h = measureHarmonics(s, 0, 1000.0, 5);
    t.expectNear("THD of synthetic 3rd harmonic == 0.1", h.thdRatio, 0.1, 1e-4);
}

void testSquareTHD(CheckSession& t) {
    std::printf("\n=== square wave THD (discrete Fourier expectation) ===\n");
    // Period 128 frames @48 kHz -> f0 = 375 Hz, 375 integer periods in the window.
    // Discrete Fourier series of a 50%-duty square with period N=128 and amplitude A:
    //   odd harmonic n has amplitude a_n = A / (32 * sin(pi*n/128))
    // (derivation: geometric-sum DFT of the two half-period blocks), which converges to
    // the continuous 4A/(pi*n) as N -> inf. Even harmonics are exactly zero.
    constexpr double kAmp = 0.5;
    constexpr uint32_t kPeriod = 128;
    constexpr uint32_t kMaxHarmonic = 15;
    const double f0 = static_cast<double>(kRate) / kPeriod; // 375 Hz
    const Signal s = makeSquare(kRate, kFrames, kPeriod, kAmp);

    auto discreteAmp = [&](uint32_t n) {
        return kAmp / (32.0 * std::sin(kTau / 2.0 * static_cast<double>(n) / kPeriod));
    };

    const double a1 = discreteAmp(1);
    t.expectNear("square fundamental amplitude matches DFT expectation", toneAmplitude(s, 0, f0), a1, 1e-6);

    double expectedSumSq = 0.0;
    for (uint32_t n = 3; n <= kMaxHarmonic; n += 2) {
        expectedSumSq += discreteAmp(n) * discreteAmp(n);
    }
    const double expectedThd = std::sqrt(expectedSumSq) / a1;

    const HarmonicReport h = measureHarmonics(s, 0, f0, kMaxHarmonic);
    t.expect("square: all requested harmonics measured", h.harmonicsMeasured == kMaxHarmonic - 1,
             "measured=" + std::to_string(h.harmonicsMeasured));
    t.expectNear("square THD (harmonics 2..15) matches DFT expectation", h.thdRatio, expectedThd, 1e-4);
    t.expectNear("square even harmonic (2nd) is zero", h.harmonicAmplitudes.empty() ? 1.0 : h.harmonicAmplitudes[0],
                 0.0, 1e-6);
}

void testImpulse(CheckSession& t) {
    std::printf("\n=== impulse ===\n");
    const Signal s = makeImpulse(kRate, kFrames, 1000, 0.7);
    const ImpulseReport r = analyzeImpulse(s, 0);
    t.expect("impulse peak frame == 1000", r.peakFrame == 1000, "peakFrame=" + std::to_string(r.peakFrame));
    t.expectNear("impulse peak value == 0.7", r.peakAbs, 0.7, 1e-7);
    t.expect("impulse span is a single frame", r.spanFrames == 1, "spanFrames=" + std::to_string(r.spanFrames));
    t.expectNear("impulse pre-span RMS == 0", r.preSpanRms, 0.0, 0.0);
    t.expectNear("impulse tail RMS == 0", r.tailRms, 0.0, 0.0);
}

void testNoiseDeterminism(CheckSession& t) {
    std::printf("\n=== fixed-seed noise ===\n");
    const Signal a = makeNoise(kRate, kFrames, 42, 0.5);
    const Signal b = makeNoise(kRate, kFrames, 42, 0.5);
    const DiffReport d = diff(a, b);
    t.expectNear("same seed -> bit-identical buffers", d.maxAbsError, 0.0, 0.0);
    t.expect("noise peak <= amplitude", peak(a) <= 0.5, "peak=" + std::to_string(peak(a)));
    // Uniform [-A, A): RMS -> A/sqrt(3); 48k samples make the estimate very tight.
    t.expectNear("uniform noise RMS ~= A/sqrt(3)", rms(a), 0.5 / std::sqrt(3.0), 0.005);
    t.expectNear("uniform noise DC ~= 0", dcOffset(a), 0.0, 0.005);

    const Signal c = makeNoise(kRate, kFrames, 43, 0.5);
    t.expect("different seed -> different buffer", diff(a, c).maxAbsError > 0.0);
}

void testStereoCorrelation(CheckSession& t) {
    std::printf("\n=== stereo correlation / polarity ===\n");
    const Signal inPhase = makeStereoCase(kRate, kFrames, StereoMode::InPhase);
    const Signal inverted = makeStereoCase(kRate, kFrames, StereoMode::Inverted);
    const Signal decorr = makeStereoCase(kRate, kFrames, StereoMode::Decorrelated);
    t.expectNear("in-phase correlation == +1", stereoCorrelation(inPhase), 1.0, 1e-9);
    t.expectNear("inverted correlation == -1", stereoCorrelation(inverted), -1.0, 1e-9);
    t.expect("decorrelated |correlation| < 0.02", std::abs(stereoCorrelation(decorr)) < 0.02,
             "corr=" + std::to_string(stereoCorrelation(decorr)));
    t.expectNear("silence correlation defined as 0", stereoCorrelation(makeSilence(kRate, 1024)), 0.0, 0.0);
}

void testDiffForensics(CheckSession& t) {
    std::printf("\n=== diff / first-mismatch forensics ===\n");
    const Signal ref = makeSine(kRate, kFrames, 1000.0, 0.5);
    Signal corrupted = ref;
    corrupted.at(123, 1) += 0.01f;

    const DiffReport d = diff(corrupted, ref, 1e-6);
    t.expect("first mismatch frame located", d.firstMismatchFrame == 123,
             "frame=" + std::to_string(d.firstMismatchFrame));
    t.expect("first mismatch channel located", d.firstMismatchChannel == 1,
             "channel=" + std::to_string(d.firstMismatchChannel));
    t.expectNear("max abs error == injected corruption", d.maxAbsError, 0.01, 1e-6);
    t.expect("no length mismatch flagged", !d.lengthMismatch);

    // Demonstrate the forensic dump once so its format is visible in test logs.
    printDiffForensics("intentional demo (not a failure)", d, corrupted, ref);

    Signal shorter = ref;
    shorter.samples.resize(shorter.samples.size() - 2);
    t.expect("length mismatch flagged", diff(shorter, ref).lengthMismatch);
}

void testSweepAndBurst(CheckSession& t) {
    std::printf("\n=== sweep / transient burst ===\n");
    const Signal sweep = makeLinearSweep(kRate, kFrames, 20.0, 20000.0, 0.5);
    // A constant-amplitude chirp has sine statistics over long windows.
    t.expectNear("sweep RMS ~= A/sqrt(2)", rms(sweep), 0.5 / std::sqrt(2.0), 0.01);
    t.expect("sweep peak <= amplitude", peak(sweep) <= 0.5 + 1e-7, "peak=" + std::to_string(peak(sweep)));

    const Signal burst = makeTransientBurst(kRate, kFrames, 8000, 4800, 1000.0, 0.5);
    t.expectNear("burst: leading silence is silent", rms(burst, -1, 0, 8000), 0.0, 0.0);
    t.expectNear("burst: trailing silence is silent", rms(burst, -1, 8000 + 4800, kFrames), 0.0, 0.0);
    const int64_t first = firstNonSilentFrame(burst);
    t.expect("burst: first energy at burst start (+1 for sin(0)=0)", first == 8001,
             "firstNonSilentFrame=" + std::to_string(first));
    t.expectNear("burst: body RMS ~= A/sqrt(2)", rms(burst, -1, 8000, 8000 + 4800), 0.5 / std::sqrt(2.0), 1e-3);
}

} // namespace

int main() {
    std::printf("============================================================\n");
    std::printf("  Aestra Audio Research Bench — Measurement Core Self-Test\n");
    std::printf("============================================================\n");

    CheckSession t;
    testSilence(t);
    testDCAndStep(t);
    testSine(t);
    testDualTone(t);
    testSquareTHD(t);
    testImpulse(t);
    testNoiseDeterminism(t);
    testStereoCorrelation(t);
    testDiffForensics(t);
    testSweepAndBurst(t);
    return t.exitCode();
}
