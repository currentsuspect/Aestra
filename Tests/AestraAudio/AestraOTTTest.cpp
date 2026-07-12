// © 2026 Aestra Studios — All Rights Reserved.
// AestraOTTTest — DSP contract tests for the 3-band upward/downward compressor.

#include "Plugin/AestraOTT.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraOTT;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 512;

struct StereoBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

StereoBuffer processStereo(AestraOTT& ott, const std::vector<float>& inL, const std::vector<float>& inR) {
    StereoBuffer out{std::vector<float>(inL.size(), 0.0f), std::vector<float>(inR.size(), 0.0f)};
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {out.left.data(), out.right.data()};
    ott.process(inputs, outputs, 2, 2, static_cast<uint32_t>(inL.size()));
    return out;
}

std::vector<float> makeSine(uint32_t numSamples, float freq, float amp, double sampleRate) {
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

/// Feed a sine through an OTT at the given depth, return steady-state
/// output/input magnitude ratio at that frequency (last half of 1 second).
float sineGain(float freq, float amp, float depth) {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.setParameter(AestraOTT::kDepth, depth);
    ott.activate();

    const auto in = makeSine(48000, freq, amp, kSampleRate);
    auto out = processStereo(ott, in, in);
    return goertzelMag(out.left, 24000, freq, kSampleRate) / amp;
}

bool testBypassPassesAudioUnchanged() {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.setParameter(AestraOTT::kBypass, 1.0f);
    ott.setParameter(AestraOTT::kInGain, 1.0f);
    ott.activate();

    const auto inL = makeSine(1024, 1000.0f, 0.5f, kSampleRate);
    const auto inR = makeSine(1024, 500.0f, 0.3f, kSampleRate);
    auto out = processStereo(ott, inL, inR);

    for (uint32_t i = 0; i < inL.size(); ++i) {
        if (std::abs(out.left[i] - inL[i]) > 1e-6f)
            return false;
        if (std::abs(out.right[i] - inR[i]) > 1e-6f)
            return false;
    }
    return true;
}

// The upward gain gate: silence must never be dragged up.
bool testSilenceStaysSilent() {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.setParameter(AestraOTT::kDepth, 1.0f);
    ott.activate();

    const auto silence = makeSilence(8192);
    auto out = processStereo(ott, silence, silence);
    return peakAmplitude(out.left) < 1e-8f && peakAmplitude(out.right) < 1e-8f;
}

// With depth 0 and trims centered, the band split must reconstruct flat:
// LR4 low/high sum to an allpass, so magnitude ~1 at any frequency —
// including right at the crossover points.
bool testDepthZeroIsSpectrallyFlat() {
    for (float freq : {80.0f, 102.0f, 1000.0f, 2500.0f, 6000.0f}) {
        const float mag = sineGain(freq, 0.1f, 0.0f);
        if (mag < 0.9f || mag > 1.1f)
            return false;
    }
    return true;
}

// A 0 dBFS band sits 18 dB above the target: expect ~-10.8 dB of downward
// gain (0.6 of the distance).
bool testDownwardCompressesLoudSignal() {
    const float gain = sineGain(1000.0f, 1.0f, 1.0f);
    return gain < 0.5f && gain > 0.1f;
}

// A -30 dB band sits 12 dB below the target: expect ~+7.2 dB upward gain.
bool testUpwardBoostsQuietSignal() {
    const float gain = sineGain(1000.0f, 0.0316f, 1.0f);
    return gain > 1.5f && gain < 4.0f;
}

// At -70 dB the band is under the upward gate: gain must stay ~unity, not
// the +24 dB the raw computer would ask for.
bool testUpwardGateSparesNoiseFloor() {
    const float gain = sineGain(1000.0f, 0.0003f, 1.0f);
    return gain > 0.7f && gain < 1.4f;
}

// Depth is the wet control: half depth must give a noticeably smaller boost.
bool testDepthScalesEffect() {
    const float full = sineGain(1000.0f, 0.0316f, 1.0f);
    const float half = sineGain(1000.0f, 0.0316f, 0.5f);
    return half > 1.1f && full > 1.3f * half;
}

// The bands must be independent: a loud low band gets compressed while a
// quiet high band gets boosted, in the same pass.
bool testMultibandIndependence() {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.activate();

    const auto lo = makeSine(48000, 50.0f, 0.5f, kSampleRate);    // -6 dB, low band
    const auto hi = makeSine(48000, 5000.0f, 0.02f, kSampleRate); // -34 dB, high band
    std::vector<float> in(lo.size());
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = lo[i] + hi[i];

    auto out = processStereo(ott, in, in);
    const float loGain = goertzelMag(out.left, 24000, 50.0f, kSampleRate) / 0.5f;
    const float hiGain = goertzelMag(out.left, 24000, 5000.0f, kSampleRate) / 0.02f;
    return loGain < 0.7f && hiGain > 1.5f;
}

// Band trims: pulling the high trim to -12 dB must duck a high probe by
// ~12 dB while leaving a low probe alone (depth 0 isolates the trims).
bool testBandTrimsShapeOutput() {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.setParameter(AestraOTT::kDepth, 0.0f);
    ott.setParameter(AestraOTT::kHighGain, 0.0f); // -12 dB
    ott.activate();

    const auto lo = makeSine(48000, 100.0f, 0.1f, kSampleRate);
    const auto hi = makeSine(48000, 6000.0f, 0.1f, kSampleRate);
    std::vector<float> in(lo.size());
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = lo[i] + hi[i];

    auto out = processStereo(ott, in, in);
    const float loMag = goertzelMag(out.left, 24000, 100.0f, kSampleRate) / 0.1f;
    const float hiMag = goertzelMag(out.left, 24000, 6000.0f, kSampleRate) / 0.1f;
    return loMag > 0.85f && hiMag < 0.35f;
}

bool testSurvivesHostileInput() {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.setParameter(AestraOTT::kDepth, 1.0f);
    ott.setParameter(AestraOTT::kInGain, 1.0f);
    ott.activate();

    std::vector<float> in(2048, 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = (i % 2 == 0) ? 100.0f : -100.0f;
    in[100] = std::numeric_limits<float>::quiet_NaN();
    in[200] = std::numeric_limits<float>::infinity();
    in[300] = -std::numeric_limits<float>::infinity();

    auto out = processStereo(ott, in, in);
    return allFinite(out.left) && allFinite(out.right);
}

// Every parameter at both extremes with hot input: output must stay finite.
bool testStableAtParameterExtremes() {
    for (float extreme : {0.0f, 1.0f}) {
        AestraOTT ott;
        ott.initialize(kSampleRate, kBlockSize);
        for (uint32_t p = 0; p < AestraOTT::kParamCount; ++p) {
            if (p != AestraOTT::kBypass)
                ott.setParameter(p, extreme);
        }
        ott.activate();

        const auto in = makeSine(8192, 700.0f, 1.0f, kSampleRate);
        auto out = processStereo(ott, in, in);
        if (!allFinite(out.left) || !allFinite(out.right))
            return false;
    }
    return true;
}

bool testStateRoundTrip() {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.activate();
    ott.setParameter(AestraOTT::kDepth, 0.7f);
    ott.setParameter(AestraOTT::kTime, 0.3f);
    ott.setParameter(AestraOTT::kInGain, 0.6f);
    ott.setParameter(AestraOTT::kOutGain, 0.4f);
    ott.setParameter(AestraOTT::kLowGain, 0.55f);
    ott.setParameter(AestraOTT::kMidGain, 0.45f);
    ott.setParameter(AestraOTT::kHighGain, 0.65f);
    ott.setParameter(AestraOTT::kXoverLow, 0.35f);
    ott.setParameter(AestraOTT::kXoverHigh, 0.75f);

    const auto state = ott.saveState();

    AestraOTT restored;
    restored.initialize(kSampleRate, kBlockSize);
    restored.activate();
    if (!restored.loadState(state))
        return false;

    for (uint32_t i = 0; i < AestraOTT::kParamCount; ++i) {
        if (std::abs(restored.getParameter(i) - ott.getParameter(i)) > 1e-6f)
            return false;
    }
    return true;
}

bool testLoadStateRejectsGarbage() {
    AestraOTT ott;
    ott.initialize(kSampleRate, kBlockSize);
    ott.activate();

    std::vector<uint8_t> tooShort = {0x31, 0x54};
    if (ott.loadState(tooShort))
        return false;

    std::vector<uint8_t> wrongMagic(64, 0);
    if (ott.loadState(wrongMagic))
        return false;
    return true;
}

bool testParameterMapping() {
    // Time center detent must be exactly 1x, gain centers exactly 0 dB.
    if (std::abs(AestraOTT::timeMultFromNorm(0.5f) - 1.0f) > 0.01f)
        return false;
    if (std::abs(AestraOTT::bipolarDbFromNorm(0.5f, 24.0f)) > 0.001f)
        return false;
    if (std::abs(AestraOTT::xoverLowHzFromNorm(0.0f) - 60.0f) > 0.1f)
        return false;
    if (std::abs(AestraOTT::xoverHighHzFromNorm(1.0f) - 8000.0f) > 1.0f)
        return false;
    return true;
}

bool testStableAcrossSampleRates() {
    for (double sr : {44100.0, 96000.0, 192000.0}) {
        AestraOTT ott;
        ott.initialize(sr, kBlockSize);
        ott.setParameter(AestraOTT::kDepth, 1.0f);
        ott.setParameter(AestraOTT::kXoverHigh, 1.0f); // 8 kHz
        ott.activate();

        const auto in = makeSine(static_cast<uint32_t>(sr), 1000.0f, 1.0f, sr);
        auto out = processStereo(ott, in, in);
        if (!allFinite(out.left) || !allFinite(out.right))
            return false;
        if (peakAmplitude(out.left, static_cast<size_t>(sr) / 2) > 16.0f)
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
        {"BypassPassesAudioUnchanged", testBypassPassesAudioUnchanged},
        {"SilenceStaysSilent", testSilenceStaysSilent},
        {"DepthZeroIsSpectrallyFlat", testDepthZeroIsSpectrallyFlat},
        {"DownwardCompressesLoudSignal", testDownwardCompressesLoudSignal},
        {"UpwardBoostsQuietSignal", testUpwardBoostsQuietSignal},
        {"UpwardGateSparesNoiseFloor", testUpwardGateSparesNoiseFloor},
        {"DepthScalesEffect", testDepthScalesEffect},
        {"MultibandIndependence", testMultibandIndependence},
        {"BandTrimsShapeOutput", testBandTrimsShapeOutput},
        {"SurvivesHostileInput", testSurvivesHostileInput},
        {"StableAtParameterExtremes", testStableAtParameterExtremes},
        {"StateRoundTrip", testStateRoundTrip},
        {"LoadStateRejectsGarbage", testLoadStateRejectsGarbage},
        {"ParameterMapping", testParameterMapping},
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
    std::cout << "All AestraOTT tests passed\n";
    return 0;
}
