// © 2026 Aestra Studios — All Rights Reserved.
// AestraSatTest — DSP contract tests for the oversampled saturator.

#include "Plugin/AestraSat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraSat;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 512;

struct StereoBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

void initAndActivate(AestraSat& sat, double sampleRate = kSampleRate) {
    sat.initialize(sampleRate, kBlockSize);
    sat.activate();
}

StereoBuffer processStereo(AestraSat& sat, const std::vector<float>& inL, const std::vector<float>& inR) {
    StereoBuffer out{std::vector<float>(inL.size(), 0.0f), std::vector<float>(inR.size(), 0.0f)};
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {out.left.data(), out.right.data()};
    sat.process(inputs, outputs, 2, 2, static_cast<uint32_t>(inL.size()));
    return out;
}

std::vector<float> makeSine(uint32_t numSamples, float freq, float amp, double sampleRate) {
    // Phase accumulated in double: at high freq * long buffers the float
    // sin() argument grows past the point where its phase noise (~-40 dB)
    // would pollute spectral measurements.
    std::vector<float> buf(numSamples);
    const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
    for (uint32_t i = 0; i < numSamples; ++i)
        buf[i] = static_cast<float>(amp * std::sin(w * static_cast<double>(i)));
    return buf;
}

std::vector<float> makeSilence(uint32_t numSamples) {
    return std::vector<float>(numSamples, 0.0f);
}

float peakAmplitude(const std::vector<float>& buf, size_t start = 0) {
    float peak = 0.0f;
    for (size_t i = start; i < buf.size(); ++i)
        peak = std::max(peak, std::abs(buf[i]));
    return peak;
}

float rmsAmplitude(const std::vector<float>& buf, size_t start = 0) {
    if (start >= buf.size())
        return 0.0f;
    double acc = 0.0;
    for (size_t i = start; i < buf.size(); ++i)
        acc += static_cast<double>(buf[i]) * buf[i];
    return static_cast<float>(std::sqrt(acc / static_cast<double>(buf.size() - start)));
}

bool allFinite(const std::vector<float>& buf) {
    for (float s : buf) {
        if (!std::isfinite(s))
            return false;
    }
    return true;
}

/// Goertzel magnitude of one bin, normalized so a unit sine reads 1.0.
float goertzelMag(const std::vector<float>& buf, size_t start, float freq, double sampleRate) {
    const size_t n = buf.size() - start;
    const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t i = start; i < buf.size(); ++i) {
        s0 = buf[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return static_cast<float>(2.0 * std::sqrt(std::max(0.0, power)) / static_cast<double>(n));
}

// The plugin reports a constant oversampler latency, so internal bypass must
// pass audio through the same delay — an undelayed copy would land this slot
// ~30 samples early relative to PDC-compensated tracks.
bool testBypassMatchesReportedLatency() {
    AestraSat sat;
    initAndActivate(sat);
    sat.setParameter(AestraSat::kBypass, 1.0f);
    sat.setParameter(AestraSat::kDrive, 1.0f);
    const uint32_t latency = sat.getLatencySamples();

    const auto inL = makeSine(1024, 1000.0f, 0.5f, kSampleRate);
    const auto inR = makeSine(1024, 500.0f, 0.3f, kSampleRate);
    auto out = processStereo(sat, inL, inR);

    for (uint32_t i = 0; i < inL.size(); ++i) {
        const float expectL = (i >= latency) ? inL[i - latency] : 0.0f;
        const float expectR = (i >= latency) ? inR[i - latency] : 0.0f;
        if (std::abs(out.left[i] - expectL) > 1e-6f)
            return false;
        if (std::abs(out.right[i] - expectR) > 1e-6f)
            return false;
    }
    return true;
}

// Toggling bypass mid-stream must keep the delay ring advancing (constant
// alignment) and must not replay stale wet state on reactivation.
bool testBypassToggleKeepsAlignmentAndState() {
    AestraSat sat;
    initAndActivate(sat);
    sat.setParameter(AestraSat::kDrive, 1.0f); // hot wet path before bypass
    sat.setParameter(AestraSat::kMix, 1.0f);
    const uint32_t latency = sat.getLatencySamples();

    const auto loud = makeSine(4096, 1000.0f, 0.9f, kSampleRate);
    processStereo(sat, loud, loud); // build up wet/oversampler state

    // Bypass: output must be the input delayed by the reported latency,
    // seamlessly continuing the ring (first `latency` samples come from the
    // tail of the previous active block).
    sat.setParameter(AestraSat::kBypass, 1.0f);
    const auto quiet = makeSine(4096, 500.0f, 0.1f, kSampleRate);
    auto bypassed = processStereo(sat, quiet, quiet);
    for (uint32_t i = latency; i < quiet.size(); ++i) {
        if (std::abs(bypassed.left[i] - quiet[i - latency]) > 1e-6f)
            return false;
    }

    // Reactivate at zero drive: output must stay finite and near the new
    // quiet input level — any burst of stale pre-bypass oversampler/filter
    // state (built from the loud driven signal) would exceed this.
    sat.setParameter(AestraSat::kBypass, 0.0f);
    sat.setParameter(AestraSat::kDrive, 0.0f);
    auto resumed = processStereo(sat, quiet, quiet);
    if (!allFinite(resumed.left) || !allFinite(resumed.right))
        return false;
    return peakAmplitude(resumed.left) < 0.3f;
}

// NaN/Inf must not pass the parameter ingress: the previous value survives.
bool testSetParameterRejectsNonFinite() {
    AestraSat sat;
    initAndActivate(sat);
    sat.setParameter(AestraSat::kDrive, 0.42f);

    sat.setParameter(AestraSat::kDrive, std::numeric_limits<float>::quiet_NaN());
    if (std::abs(sat.getParameter(AestraSat::kDrive) - 0.42f) > 1e-6f)
        return false;
    sat.setParameter(AestraSat::kDrive, std::numeric_limits<float>::infinity());
    if (std::abs(sat.getParameter(AestraSat::kDrive) - 0.42f) > 1e-6f)
        return false;
    sat.setParameter(AestraSat::kDrive, -std::numeric_limits<float>::infinity());
    if (std::abs(sat.getParameter(AestraSat::kDrive) - 0.42f) > 1e-6f)
        return false;

    // A corrupted state blob full of NaNs must not poison the params either.
    std::vector<uint8_t> state = sat.saveState();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t off = 8; off + sizeof(float) <= state.size(); off += sizeof(float)) {
        std::memcpy(state.data() + off, &nan, sizeof(float));
    }
    sat.loadState(state); // magic/version intact, params all NaN
    for (uint32_t i = 0; i < AestraSat::kParamCount; ++i) {
        if (!std::isfinite(sat.getParameter(i)))
            return false;
    }

    const auto in = makeSine(2048, 1000.0f, 0.5f, kSampleRate);
    auto out = processStereo(sat, in, in);
    return allFinite(out.left) && allFinite(out.right);
}

bool testSilenceProducesSilence() {
    AestraSat sat;
    initAndActivate(sat);
    sat.setParameter(AestraSat::kDrive, 1.0f);

    const auto silence = makeSilence(2048);
    auto out = processStereo(sat, silence, silence);

    return peakAmplitude(out.left) < 1e-8f && peakAmplitude(out.right) < 1e-8f;
}

// Mix = 0 must be a pure delay of exactly getLatencySamples().
bool testDryPathIsExactDelay() {
    AestraSat sat;
    sat.initialize(kSampleRate, kBlockSize);
    sat.setParameter(AestraSat::kMix, 0.0f);
    sat.setParameter(AestraSat::kDrive, 1.0f);
    sat.activate();

    const uint32_t latency = sat.getLatencySamples();
    std::vector<float> in(1024, 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = std::sin(0.031f * static_cast<float>(i)) * 0.7f + std::sin(0.31f * static_cast<float>(i)) * 0.2f;
    auto out = processStereo(sat, in, in);

    for (uint32_t i = latency; i < in.size(); ++i) {
        if (std::abs(out.left[i] - in[i - latency]) > 1e-6f)
            return false;
    }
    for (uint32_t i = 0; i < latency && i < out.left.size(); ++i) {
        if (std::abs(out.left[i]) > 1e-6f)
            return false;
    }
    return true;
}

// At low drive and full mix the wet path must be time-aligned with the
// reported latency (otherwise Mix would comb-filter).
bool testWetPathLatencyAligned() {
    AestraSat sat;
    sat.initialize(kSampleRate, kBlockSize);
    sat.setParameter(AestraSat::kDrive, 0.0f); // 0 dB — tanh(x) ~= x at low level
    sat.setParameter(AestraSat::kMix, 1.0f);
    sat.setParameter(AestraSat::kTone, 1.0f);
    sat.activate();

    const uint32_t latency = sat.getLatencySamples();
    // Broadband probe: at a single low frequency the DC blocker's phase lead
    // (~2 samples at 200 Hz) would bias the correlation peak. Deterministic
    // noise spreads the energy so the peak sits on the true bulk delay.
    std::vector<float> in(8192);
    uint32_t lcg = 0x12345678u;
    for (auto& s : in) {
        lcg = lcg * 1664525u + 1013904223u;
        s = (static_cast<float>(lcg >> 8) / 8388608.0f - 1.0f) * 0.1f;
    }
    auto out = processStereo(sat, in, in);

    // Find the lag with the highest normalized cross-correlation: the bulk
    // delay must equal the reported latency, and at that lag the low-drive
    // wet path must track the input closely.
    double bestCorr = -2.0;
    uint32_t bestLag = 0;
    for (uint32_t lag = 0; lag <= 2 * latency; ++lag) {
        double dot = 0.0, inEnergy = 0.0, outEnergy = 0.0;
        for (uint32_t i = 2048; i < in.size(); ++i) {
            const double a = out.left[i];
            const double b = in[i - lag];
            dot += a * b;
            inEnergy += b * b;
            outEnergy += a * a;
        }
        const double denom = std::sqrt(inEnergy * outEnergy);
        const double corr = denom > 0.0 ? dot / denom : 0.0;
        if (corr > bestCorr) {
            bestCorr = corr;
            bestLag = lag;
        }
    }
    // The tone pole and halfband transition band shave the top octave off the
    // noise, so correlation won't be 1.0 — the lag is the real assertion.
    return bestLag == latency && bestCorr > 0.98;
}

bool testHighDriveSaturates() {
    AestraSat sat;
    sat.initialize(kSampleRate, kBlockSize);
    sat.setParameter(AestraSat::kDrive, 1.0f); // +36 dB
    sat.setParameter(AestraSat::kMode, AestraSat::normFromMode(AestraSat::kModeHard));
    sat.activate();

    const auto in = makeSine(8192, 997.0f, 0.5f, kSampleRate);
    auto out = processStereo(sat, in, in);

    // A hard-clipped sine at 36 dB overdrive is nearly a square wave:
    // peak ~1, crest factor (rms/peak) approaching 1.
    const float peak = peakAmplitude(out.left, 2048);
    const float rms = rmsAmplitude(out.left, 2048);
    if (peak < 0.8f || peak > 1.2f)
        return false;
    if (rms / peak < 0.85f)
        return false;
    return allFinite(out.left) && allFinite(out.right);
}

// Tube asymmetry shows up as even harmonics; symmetric tape/hard curves
// produce only odd ones. (At extreme drive the DC blocker recentres the
// clipped square and peak asymmetry vanishes, so measure spectrum at
// moderate drive instead of comparing lobe peaks.)
bool testTubeModeAddsEvenHarmonics() {
    auto secondHarmonicRatio = [](AestraSat::Mode mode) {
        AestraSat sat;
        sat.initialize(kSampleRate, kBlockSize);
        sat.setParameter(AestraSat::kDrive, 0.5f); // +18 dB — rich but not square
        sat.setParameter(AestraSat::kMode, AestraSat::normFromMode(mode));
        sat.activate();

        const float freq = 250.0f;
        const auto in = makeSine(16384, freq, 0.25f, kSampleRate);
        auto out = processStereo(sat, in, in);

        const float fund = goertzelMag(out.left, 4096, freq, kSampleRate);
        const float second = goertzelMag(out.left, 4096, 2.0f * freq, kSampleRate);
        return fund > 1e-6f ? second / fund : 0.0f;
    };

    const float tube = secondHarmonicRatio(AestraSat::kModeTube);
    const float tape = secondHarmonicRatio(AestraSat::kModeTape);
    // Tube: clearly audible 2nd harmonic (> -30 dB); tape: essentially none.
    return tube > 0.03f && tube > 10.0f * tape;
}

// The DC blocker must keep the long-run mean near zero even in the
// asymmetric tube mode.
bool testTubeModeHasNoDcOffset() {
    AestraSat sat;
    sat.initialize(kSampleRate, kBlockSize);
    sat.setParameter(AestraSat::kDrive, 1.0f);
    sat.setParameter(AestraSat::kMode, AestraSat::normFromMode(AestraSat::kModeTube));
    sat.activate();

    const auto in = makeSine(48000, 250.0f, 0.5f, kSampleRate);
    auto out = processStereo(sat, in, in);

    double mean = 0.0;
    for (size_t i = 24000; i < out.left.size(); ++i)
        mean += out.left[i];
    mean /= static_cast<double>(out.left.size() - 24000);
    return std::abs(mean) < 0.02;
}

// 4x oversampling must suppress foldback aliasing. A 15 kHz sine driven
// +18 dB into tanh puts its 3rd harmonic at 45 kHz (~-12 dB); without
// oversampling that folds straight down to 3 kHz. With the 4x halfband
// cascade it must be buried. (A hard clip at max drive is deliberately not
// used here: a near-square wave keeps meaningful energy in harmonics past
// the 4x Nyquist, so some fold-back is inherent to any finite oversampling
// factor and the measurement would test the torture signal, not the filter.)
bool testOversamplingSuppressesAliasing() {
    AestraSat sat;
    sat.initialize(kSampleRate, kBlockSize);
    sat.setParameter(AestraSat::kDrive, 0.5f); // +18 dB
    sat.setParameter(AestraSat::kMode, AestraSat::normFromMode(AestraSat::kModeTape));
    sat.activate();

    const float fundamental = 15000.0f;
    const float aliasFreq = 3.0f * fundamental - static_cast<float>(kSampleRate); // 3 kHz
    const auto in = makeSine(32768, fundamental, 0.25f, kSampleRate);
    auto out = processStereo(sat, in, in);

    const float fundMag = goertzelMag(out.left, 8192, fundamental, kSampleRate);
    const float aliasMag = goertzelMag(out.left, 8192, aliasFreq, kSampleRate);
    if (fundMag < 1e-4f)
        return false;

    const float ratioDb = 20.0f * std::log10(aliasMag / fundMag);
    return ratioDb < -50.0f;
}

bool testToneDarkensOutput() {
    auto renderWithTone = [](float tone) {
        AestraSat sat;
        sat.initialize(kSampleRate, kBlockSize);
        sat.setParameter(AestraSat::kDrive, 0.5f);
        sat.setParameter(AestraSat::kTone, tone);
        sat.activate();
        const auto in = makeSine(16384, 8000.0f, 0.25f, kSampleRate);
        auto out = processStereo(sat, in, in);
        return goertzelMag(out.left, 4096, 8000.0f, kSampleRate);
    };

    const float open = renderWithTone(1.0f); // 20 kHz
    const float dark = renderWithTone(0.0f); // 1 kHz
    // 8 kHz content through a 1 kHz one-pole should drop by roughly 18 dB.
    return dark < open * 0.25f;
}

bool testSurvivesHostileInput() {
    AestraSat sat;
    sat.initialize(kSampleRate, kBlockSize);
    sat.setParameter(AestraSat::kDrive, 1.0f);
    sat.activate();

    std::vector<float> in(2048, 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = (i % 2 == 0) ? 100.0f : -100.0f;
    in[100] = std::numeric_limits<float>::quiet_NaN();
    in[200] = std::numeric_limits<float>::infinity();
    in[300] = -std::numeric_limits<float>::infinity();

    auto out = processStereo(sat, in, in);
    return allFinite(out.left) && allFinite(out.right);
}

bool testStateRoundTrip() {
    AestraSat sat;
    initAndActivate(sat);
    sat.setParameter(AestraSat::kDrive, 0.7f);
    sat.setParameter(AestraSat::kMode, AestraSat::normFromMode(AestraSat::kModeTube));
    sat.setParameter(AestraSat::kTone, 0.3f);
    sat.setParameter(AestraSat::kOutput, 0.6f);
    sat.setParameter(AestraSat::kMix, 0.8f);

    const auto state = sat.saveState();

    AestraSat restored;
    initAndActivate(restored);
    if (!restored.loadState(state))
        return false;

    for (uint32_t i = 0; i < AestraSat::kParamCount; ++i) {
        if (std::abs(restored.getParameter(i) - sat.getParameter(i)) > 1e-6f)
            return false;
    }
    return true;
}

bool testLoadStateRejectsGarbage() {
    AestraSat sat;
    initAndActivate(sat);

    std::vector<uint8_t> tooShort = {0x31, 0x54};
    if (sat.loadState(tooShort))
        return false;

    std::vector<uint8_t> wrongMagic(64, 0);
    if (sat.loadState(wrongMagic))
        return false;
    return true;
}

bool testModeMapping() {
    if (AestraSat::modeFromNorm(0.0f) != AestraSat::kModeTape)
        return false;
    if (AestraSat::modeFromNorm(0.5f) != AestraSat::kModeTube)
        return false;
    if (AestraSat::modeFromNorm(1.0f) != AestraSat::kModeHard)
        return false;
    if (AestraSat::modeFromNorm(AestraSat::normFromMode(AestraSat::kModeTube)) != AestraSat::kModeTube)
        return false;
    return true;
}

bool testStableAcrossSampleRates() {
    for (double sr : {44100.0, 96000.0, 192000.0}) {
        AestraSat sat;
        initAndActivate(sat, sr);
        sat.setParameter(AestraSat::kDrive, 1.0f);

        const auto in = makeSine(8192, 1000.0f, 0.5f, sr);
        auto out = processStereo(sat, in, in);
        if (!allFinite(out.left) || !allFinite(out.right))
            return false;
        if (peakAmplitude(out.left, 2048) > 4.5f)
            return false; // +12 dB trim max headroom

        const auto silence = makeSilence(8192);
        AestraSat quiet;
        initAndActivate(quiet, sr);
        auto quietOut = processStereo(quiet, silence, silence);
        if (peakAmplitude(quietOut.left) > 1e-8f)
            return false;
    }
    return true;
}

struct TestCase {
    const char* name;
    bool (*fn)();
};

} // namespace

int main() {
    const TestCase tests[] = {
        {"BypassMatchesReportedLatency", testBypassMatchesReportedLatency},
        {"BypassToggleKeepsAlignmentAndState", testBypassToggleKeepsAlignmentAndState},
        {"SetParameterRejectsNonFinite", testSetParameterRejectsNonFinite},
        {"SilenceProducesSilence", testSilenceProducesSilence},
        {"DryPathIsExactDelay", testDryPathIsExactDelay},
        {"WetPathLatencyAligned", testWetPathLatencyAligned},
        {"HighDriveSaturates", testHighDriveSaturates},
        {"TubeModeAddsEvenHarmonics", testTubeModeAddsEvenHarmonics},
        {"TubeModeHasNoDcOffset", testTubeModeHasNoDcOffset},
        {"OversamplingSuppressesAliasing", testOversamplingSuppressesAliasing},
        {"ToneDarkensOutput", testToneDarkensOutput},
        {"SurvivesHostileInput", testSurvivesHostileInput},
        {"StateRoundTrip", testStateRoundTrip},
        {"LoadStateRejectsGarbage", testLoadStateRejectsGarbage},
        {"ModeMapping", testModeMapping},
        {"StableAcrossSampleRates", testStableAcrossSampleRates},
    };

    int failures = 0;
    for (const auto& test : tests) {
        const bool passed = test.fn();
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << test.name << "\n";
        if (!passed)
            ++failures;
    }

    if (failures > 0) {
        std::cout << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All AestraSat tests passed\n";
    return 0;
}
