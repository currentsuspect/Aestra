// © 2026 Aestra Studios — All Rights Reserved.
// AestraFilterTest — DSP contract tests for the envelope-modulated SVF.

#include "Plugin/AestraFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraFilter;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 512;

struct StereoBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

StereoBuffer processStereo(AestraFilter& flt, const std::vector<float>& inL, const std::vector<float>& inR) {
    StereoBuffer out{std::vector<float>(inL.size(), 0.0f), std::vector<float>(inR.size(), 0.0f)};
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {out.left.data(), out.right.data()};
    flt.process(inputs, outputs, 2, 2, static_cast<uint32_t>(inL.size()));
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

// Configure, activate, feed a sine, return steady-state magnitude at its own
// frequency (i.e. one point of the transfer function).
float sineResponse(AestraFilter::Type type, float cutoffNorm, float resoNorm, float freq) {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.setParameter(AestraFilter::kType, AestraFilter::normFromType(type));
    flt.setParameter(AestraFilter::kCutoff, cutoffNorm);
    flt.setParameter(AestraFilter::kReso, resoNorm);
    flt.activate();

    const auto in = makeSine(16384, freq, 0.1f, kSampleRate);
    auto out = processStereo(flt, in, in);
    return goertzelMag(out.left, 8192, freq, kSampleRate) / 0.1f;
}

float normForHz(float hz) {
    // Inverse of cutoffHzFromNorm: 20 * 1000^v
    return std::log(hz / 20.0f) / std::log(1000.0f);
}

bool testBypassPassesAudioUnchanged() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.setParameter(AestraFilter::kBypass, 1.0f);
    flt.setParameter(AestraFilter::kCutoff, 0.1f);
    flt.activate();

    const auto inL = makeSine(1024, 1000.0f, 0.5f, kSampleRate);
    const auto inR = makeSine(1024, 500.0f, 0.3f, kSampleRate);
    auto out = processStereo(flt, inL, inR);

    for (uint32_t i = 0; i < inL.size(); ++i) {
        if (std::abs(out.left[i] - inL[i]) > 1e-6f)
            return false;
        if (std::abs(out.right[i] - inR[i]) > 1e-6f)
            return false;
    }
    return true;
}

bool testSilenceProducesSilence() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.setParameter(AestraFilter::kReso, 1.0f); // max Q must not self-oscillate
    flt.activate();

    const auto silence = makeSilence(4096);
    auto out = processStereo(flt, silence, silence);
    return peakAmplitude(out.left) < 1e-8f && peakAmplitude(out.right) < 1e-8f;
}

bool testLowPassResponse() {
    const float cutoff = normForHz(500.0f);
    const float passband = sineResponse(AestraFilter::kTypeLowPass, cutoff, 0.116f, 100.0f);
    const float stopband = sineResponse(AestraFilter::kTypeLowPass, cutoff, 0.116f, 5000.0f);
    // 100 Hz well below 500 Hz cutoff: ~unity. 5 kHz is ~3.3 octaves above:
    // 12 dB/oct SVF gives ~-40 dB.
    return passband > 0.9f && passband < 1.1f && stopband < 0.05f;
}

bool testHighPassResponse() {
    const float cutoff = normForHz(2000.0f);
    const float passband = sineResponse(AestraFilter::kTypeHighPass, cutoff, 0.116f, 10000.0f);
    const float stopband = sineResponse(AestraFilter::kTypeHighPass, cutoff, 0.116f, 100.0f);
    return passband > 0.85f && passband < 1.15f && stopband < 0.05f;
}

bool testBandPassResponse() {
    const float cutoff = normForHz(1000.0f);
    const float center = sineResponse(AestraFilter::kTypeBandPass, cutoff, 0.5f, 1000.0f);
    const float below = sineResponse(AestraFilter::kTypeBandPass, cutoff, 0.5f, 100.0f);
    const float above = sineResponse(AestraFilter::kTypeBandPass, cutoff, 0.5f, 10000.0f);
    return center > 0.7f && below < 0.2f && above < 0.2f;
}

bool testResonancePeaks() {
    const float cutoff = normForHz(1000.0f);
    const float lowQ = sineResponse(AestraFilter::kTypeLowPass, cutoff, 0.116f, 1000.0f);
    const float highQ = sineResponse(AestraFilter::kTypeLowPass, cutoff, 1.0f, 1000.0f);
    // At the cutoff, Q=0.707 sits at ~-3 dB, Q=10 peaks at ~+20 dB.
    return highQ > 4.0f * lowQ && highQ > 5.0f;
}

bool testHighResonanceStaysBounded() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.setParameter(AestraFilter::kReso, 1.0f);
    flt.setParameter(AestraFilter::kCutoff, normForHz(1000.0f));
    flt.activate();

    // Impulse into max resonance: must ring but decay, never blow up.
    std::vector<float> in(48000, 0.0f);
    in[0] = 1.0f;
    auto out = processStereo(flt, in, in);
    if (!allFinite(out.left))
        return false;
    if (peakAmplitude(out.left) > 16.0f)
        return false;
    // Ring must have decayed to noise floor within a second.
    return peakAmplitude(out.left, 40000) < 1e-4f;
}

// The envelope shaper: with positive env amount, loud input must open the
// filter (more high-frequency content passes than with env amount at zero).
bool testEnvelopeOpensFilter() {
    auto highBandLevel = [](float envAmountNorm) {
        AestraFilter flt;
        flt.initialize(kSampleRate, kBlockSize);
        flt.setParameter(AestraFilter::kType, AestraFilter::normFromType(AestraFilter::kTypeLowPass));
        flt.setParameter(AestraFilter::kCutoff, normForHz(200.0f));
        flt.setParameter(AestraFilter::kEnvAmount, envAmountNorm);
        flt.setParameter(AestraFilter::kEnvAttack, 0.0f); // fastest
        flt.activate();

        // Loud 100 Hz carrier (drives the envelope) + quiet 4 kHz probe.
        const auto lo = makeSine(24000, 100.0f, 0.8f, kSampleRate);
        const auto hi = makeSine(24000, 4000.0f, 0.05f, kSampleRate);
        std::vector<float> in(lo.size());
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = lo[i] + hi[i];
        AestraFilter& ref = flt;
        auto out = processStereo(ref, in, in);
        return goertzelMag(out.left, 12000, 4000.0f, kSampleRate);
    };

    const float closed = highBandLevel(0.5f); // env amount 0
    const float open = highBandLevel(1.0f);   // env amount +100%
    // +4 octaves on a 0.8-peak envelope moves 200 Hz cutoff well past 2 kHz:
    // the 4 kHz probe must come through much stronger.
    return open > 4.0f * closed;
}

// Negative env amount must close the filter instead.
bool testNegativeEnvelopeClosesFilter() {
    auto probeLevel = [](float envAmountNorm) {
        AestraFilter flt;
        flt.initialize(kSampleRate, kBlockSize);
        flt.setParameter(AestraFilter::kType, AestraFilter::normFromType(AestraFilter::kTypeLowPass));
        flt.setParameter(AestraFilter::kCutoff, normForHz(2000.0f));
        flt.setParameter(AestraFilter::kEnvAmount, envAmountNorm);
        flt.setParameter(AestraFilter::kEnvAttack, 0.0f);
        flt.activate();

        const auto lo = makeSine(24000, 100.0f, 0.8f, kSampleRate);
        const auto probe = makeSine(24000, 1000.0f, 0.05f, kSampleRate);
        std::vector<float> in(lo.size());
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = lo[i] + probe[i];
        auto out = processStereo(flt, in, in);
        return goertzelMag(out.left, 12000, 1000.0f, kSampleRate);
    };

    const float neutral = probeLevel(0.5f);
    const float ducked = probeLevel(0.0f); // env amount -100%
    return ducked < 0.5f * neutral;
}

bool testDriveZeroIsTransparent() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    // Cutoff fully open, drive 0, mix 100%: output ~= input minus the small
    // response of a 20 kHz LP at low frequencies.
    flt.setParameter(AestraFilter::kCutoff, 1.0f);
    flt.setParameter(AestraFilter::kDrive, 0.0f);
    flt.activate();

    const float freq = 200.0f;
    const auto in = makeSine(16384, freq, 0.5f, kSampleRate);
    auto out = processStereo(flt, in, in);
    const float mag = goertzelMag(out.left, 8192, freq, kSampleRate) / 0.5f;
    return mag > 0.98f && mag < 1.02f;
}

bool testDriveAddsHarmonics() {
    auto thirdHarmonic = [](float driveNorm) {
        AestraFilter flt;
        flt.initialize(kSampleRate, kBlockSize);
        flt.setParameter(AestraFilter::kCutoff, 1.0f);
        flt.setParameter(AestraFilter::kDrive, driveNorm);
        flt.activate();

        const float freq = 500.0f;
        const auto in = makeSine(16384, freq, 0.5f, kSampleRate);
        auto out = processStereo(flt, in, in);
        return goertzelMag(out.left, 8192, 3.0f * freq, kSampleRate);
    };

    const float clean = thirdHarmonic(0.0f);
    const float driven = thirdHarmonic(1.0f);
    return driven > 0.01f && driven > 10.0f * clean;
}

bool testMixZeroIsIdentity() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.setParameter(AestraFilter::kMix, 0.0f);
    flt.setParameter(AestraFilter::kCutoff, 0.0f); // fully closed LP
    flt.setParameter(AestraFilter::kDrive, 1.0f);
    flt.activate();

    const auto in = makeSine(4096, 1000.0f, 0.5f, kSampleRate);
    auto out = processStereo(flt, in, in);
    for (uint32_t i = 0; i < in.size(); ++i) {
        if (std::abs(out.left[i] - in[i]) > 1e-6f)
            return false;
    }
    return true;
}

bool testSurvivesHostileInput() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.setParameter(AestraFilter::kReso, 1.0f);
    flt.setParameter(AestraFilter::kDrive, 1.0f);
    flt.activate();

    std::vector<float> in(2048, 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = (i % 2 == 0) ? 100.0f : -100.0f;
    in[100] = std::numeric_limits<float>::quiet_NaN();
    in[200] = std::numeric_limits<float>::infinity();
    in[300] = -std::numeric_limits<float>::infinity();

    auto out = processStereo(flt, in, in);
    return allFinite(out.left) && allFinite(out.right);
}

bool testStateRoundTrip() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.activate();
    flt.setParameter(AestraFilter::kType, AestraFilter::normFromType(AestraFilter::kTypeBandPass));
    flt.setParameter(AestraFilter::kCutoff, 0.42f);
    flt.setParameter(AestraFilter::kReso, 0.8f);
    flt.setParameter(AestraFilter::kDrive, 0.3f);
    flt.setParameter(AestraFilter::kEnvAmount, 0.75f);
    flt.setParameter(AestraFilter::kEnvAttack, 0.2f);
    flt.setParameter(AestraFilter::kEnvRelease, 0.9f);
    flt.setParameter(AestraFilter::kMix, 0.6f);

    const auto state = flt.saveState();

    AestraFilter restored;
    restored.initialize(kSampleRate, kBlockSize);
    restored.activate();
    if (!restored.loadState(state))
        return false;

    for (uint32_t i = 0; i < AestraFilter::kParamCount; ++i) {
        if (std::abs(restored.getParameter(i) - flt.getParameter(i)) > 1e-6f)
            return false;
    }
    return true;
}

bool testLoadStateRejectsGarbage() {
    AestraFilter flt;
    flt.initialize(kSampleRate, kBlockSize);
    flt.activate();

    std::vector<uint8_t> tooShort = {0x31, 0x54};
    if (flt.loadState(tooShort))
        return false;

    std::vector<uint8_t> wrongMagic(64, 0);
    if (flt.loadState(wrongMagic))
        return false;
    return true;
}

bool testTypeMapping() {
    if (AestraFilter::typeFromNorm(0.0f) != AestraFilter::kTypeLowPass)
        return false;
    if (AestraFilter::typeFromNorm(0.5f) != AestraFilter::kTypeBandPass)
        return false;
    if (AestraFilter::typeFromNorm(1.0f) != AestraFilter::kTypeHighPass)
        return false;
    return AestraFilter::typeFromNorm(AestraFilter::normFromType(AestraFilter::kTypeBandPass)) ==
           AestraFilter::kTypeBandPass;
}

bool testStableAcrossSampleRates() {
    for (double sr : {44100.0, 96000.0, 192000.0}) {
        AestraFilter flt;
        flt.initialize(sr, kBlockSize);
        flt.setParameter(AestraFilter::kReso, 1.0f);
        flt.setParameter(AestraFilter::kCutoff, 1.0f); // 20 kHz — near Nyquist at 44.1k
        flt.activate();

        const auto in = makeSine(8192, 1000.0f, 0.5f, sr);
        auto out = processStereo(flt, in, in);
        if (!allFinite(out.left) || !allFinite(out.right))
            return false;
        if (peakAmplitude(out.left, 2048) > 16.0f)
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
        {"SilenceProducesSilence", testSilenceProducesSilence},
        {"LowPassResponse", testLowPassResponse},
        {"HighPassResponse", testHighPassResponse},
        {"BandPassResponse", testBandPassResponse},
        {"ResonancePeaks", testResonancePeaks},
        {"HighResonanceStaysBounded", testHighResonanceStaysBounded},
        {"EnvelopeOpensFilter", testEnvelopeOpensFilter},
        {"NegativeEnvelopeClosesFilter", testNegativeEnvelopeClosesFilter},
        {"DriveZeroIsTransparent", testDriveZeroIsTransparent},
        {"DriveAddsHarmonics", testDriveAddsHarmonics},
        {"MixZeroIsIdentity", testMixZeroIsIdentity},
        {"SurvivesHostileInput", testSurvivesHostileInput},
        {"StateRoundTrip", testStateRoundTrip},
        {"LoadStateRejectsGarbage", testLoadStateRejectsGarbage},
        {"TypeMapping", testTypeMapping},
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
    std::cout << "All AestraFilter tests passed\n";
    return 0;
}
