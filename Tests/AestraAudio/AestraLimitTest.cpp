// © 2026 Aestra Studios — All Rights Reserved.
// AestraLimitTest — DSP contract tests for the brickwall limiter.

#include "Plugin/AestraLimit.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using Aestra::Audio::Plugins::AestraLimit;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 512;

struct StereoBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

void initAndActivate(AestraLimit& lim) {
    lim.initialize(kSampleRate, kBlockSize);
    lim.activate();
}

StereoBuffer processStereo(AestraLimit& lim, const std::vector<float>& inL, const std::vector<float>& inR) {
    StereoBuffer out{std::vector<float>(inL.size(), 0.0f), std::vector<float>(inR.size(), 0.0f)};
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {out.left.data(), out.right.data()};
    lim.process(inputs, outputs, 2, 2, static_cast<uint32_t>(inL.size()));
    return out;
}

std::vector<float> makeSine(uint32_t numSamples, float freq, float amp, double sampleRate) {
    std::vector<float> buf(numSamples);
    for (uint32_t i = 0; i < numSamples; ++i)
        buf[i] = amp * std::sin(2.0f * 3.14159265f * freq * static_cast<float>(i) / static_cast<float>(sampleRate));
    return buf;
}

std::vector<float> makeSilence(uint32_t numSamples) {
    return std::vector<float>(numSamples, 0.0f);
}

float peakAmplitude(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    return peak;
}

float oversampledPeak(const std::vector<float>& buf, uint32_t osRatio) {
    float peak = 0.0f;
    for (size_t i = 0; i + 1 < buf.size(); ++i) {
        for (uint32_t t = 0; t < osRatio; ++t) {
            const float frac = static_cast<float>(t) / static_cast<float>(osRatio);
            const float interp = buf[i] + frac * (buf[i + 1] - buf[i]);
            peak = std::max(peak, std::abs(interp));
        }
    }
    return peak;
}

bool testBypassPassesAudioUnchanged() {
    AestraLimit lim;
    initAndActivate(lim);
    lim.setParameter(AestraLimit::kBypass, 1.0f);

    const auto inL = makeSine(1024, 1000.0f, 0.5f, kSampleRate);
    const auto inR = makeSine(1024, 1000.0f, 0.5f, kSampleRate);
    auto out = processStereo(lim, inL, inR);

    for (uint32_t i = 0; i < inL.size(); ++i) {
        if (std::abs(out.left[i] - inL[i]) > 1e-6f) return false;
        if (std::abs(out.right[i] - inR[i]) > 1e-6f) return false;
    }
    return true;
}

bool testSilenceProducesSilence() {
    AestraLimit lim;
    initAndActivate(lim);

    const auto inL = makeSilence(1024);
    const auto inR = makeSilence(1024);
    auto out = processStereo(lim, inL, inR);

    for (uint32_t i = 0; i < inL.size(); ++i) {
        if (std::abs(out.left[i]) > 1e-10f) return false;
        if (std::abs(out.right[i]) > 1e-10f) return false;
    }
    return true;
}

bool testTruePeakDoesNotExceedCeiling() {
    AestraLimit lim;
    initAndActivate(lim);
    lim.setParameter(AestraLimit::kCeiling, 0.5f);

    const float ceilingDb = -24.0f + 0.5f * 24.0f;
    const float internalCeilingLinear = std::pow(10.0f, (ceilingDb - 0.1f) / 20.0f);

    const auto silence = makeSilence(kBlockSize);
    processStereo(lim, silence, silence);

    const auto inL = makeSine(kBlockSize, 1000.0f, 1.0f, kSampleRate);
    const auto inR = makeSine(kBlockSize, 1000.0f, 1.0f, kSampleRate);
    auto out = processStereo(lim, inL, inR);

    const float peakL = oversampledPeak(out.left, AestraLimit::kOsRatio);
    const float peakR = oversampledPeak(out.right, AestraLimit::kOsRatio);
    float outPeak = std::max(peakL, peakR);
    return outPeak <= internalCeilingLinear + 0.01f;
}

bool testGainReductionApplied() {
    AestraLimit lim;
    initAndActivate(lim);
    lim.setParameter(AestraLimit::kCeiling, 0.25f);

    const auto silence = makeSilence(kBlockSize);
    processStereo(lim, silence, silence);

    const auto inL = makeSine(kBlockSize, 1000.0f, 1.0f, kSampleRate);
    const auto inR = makeSine(kBlockSize, 1000.0f, 1.0f, kSampleRate);
    auto out = processStereo(lim, inL, inR);

    const float ceilingDb = -24.0f + 0.25f * 24.0f;
    const float expectedMax = std::pow(10.0f, ceilingDb / 20.0f) + 0.05f;
    const float outPeak = peakAmplitude(out.left);
    return outPeak < expectedMax;
}

bool testStateSaveLoadRoundtrip() {
    AestraLimit lim;
    lim.initialize(kSampleRate, kBlockSize);
    lim.setParameter(AestraLimit::kCeiling, 0.3f);
    lim.setParameter(AestraLimit::kReleaseMode, 1.0f);
    lim.setParameter(AestraLimit::kRelease, 0.7f);

    const auto state = lim.saveState();
    assert(state.size() > 8);

    AestraLimit lim2;
    lim2.initialize(kSampleRate, kBlockSize);
    lim2.activate();
    assert(lim2.loadState(state));

    const float epsilon = 1e-5f;
    if (std::abs(lim2.getParameter(AestraLimit::kCeiling) - 0.3f) > epsilon) return false;
    if (std::abs(lim2.getParameter(AestraLimit::kReleaseMode) - 1.0f) > epsilon) return false;
    if (std::abs(lim2.getParameter(AestraLimit::kRelease) - 0.7f) > epsilon) return false;
    return true;
}

bool testStateRejectsBadMagic() {
    AestraLimit lim;
    lim.initialize(kSampleRate, kBlockSize);

    std::vector<uint8_t> badState(64, 0xFF);
    return !lim.loadState(badState);
}

bool testStateRejectsTooShort() {
    AestraLimit lim;
    lim.initialize(kSampleRate, kBlockSize);

    std::vector<uint8_t> shortState(4, 0x00);
    return !lim.loadState(shortState);
}

bool testParameterCount() {
    AestraLimit lim;
    return lim.getParameterCount() == AestraLimit::kParamCount;
}

bool testParameterClamping() {
    AestraLimit lim;
    lim.initialize(kSampleRate, kBlockSize);
    lim.setParameter(AestraLimit::kCeiling, -0.5f);
    if (lim.getParameter(AestraLimit::kCeiling) != 0.0f) return false;
    lim.setParameter(AestraLimit::kCeiling, 2.0f);
    if (lim.getParameter(AestraLimit::kCeiling) != 1.0f) return false;
    return true;
}

bool testSanitizeNanInput() {
    AestraLimit lim;
    initAndActivate(lim);

    float nanVal = std::nanf("");
    const float* inputs[] = {&nanVal, &nanVal};
    float outL = 0.0f, outR = 0.0f;
    float* outputs[] = {&outL, &outR};
    lim.process(inputs, outputs, 2, 2, 1);

    return std::isfinite(outL) && std::isfinite(outR);
}

bool testLatencyMatchesLookahead() {
    AestraLimit lim;
    lim.initialize(kSampleRate, kBlockSize);

    const uint32_t expected = static_cast<uint32_t>(
        std::ceil(static_cast<double>(AestraLimit::kLookaheadMs) * 0.001 * static_cast<double>(kSampleRate)));
    return lim.getLatencySamples() == expected;
}

bool testMeteringUpdatesAfterProcess() {
    AestraLimit lim;
    initAndActivate(lim);

    const auto silence = makeSilence(kBlockSize);
    processStereo(lim, silence, silence);

    const auto inL = makeSine(kBlockSize, 1000.0f, 0.8f, kSampleRate);
    const auto inR = makeSine(kBlockSize, 1000.0f, 0.8f, kSampleRate);
    processStereo(lim, inL, inR);

    return lim.getInputLevel() > 0.0f;
}

bool testOutputClipperBoundsOutput() {
    AestraLimit lim;
    initAndActivate(lim);
    lim.setParameter(AestraLimit::kCeiling, 0.9f);

    const auto silence = makeSilence(kBlockSize);
    processStereo(lim, silence, silence);

    const auto inL = makeSine(kBlockSize, 1000.0f, 2.0f, kSampleRate);
    const auto inR = makeSine(kBlockSize, 1000.0f, 2.0f, kSampleRate);
    auto out = processStereo(lim, inL, inR);

    const float outPeak = std::max(peakAmplitude(out.left), peakAmplitude(out.right));
    return outPeak <= 1.01f;
}

bool testZeroInputLatencyRamp() {
    AestraLimit lim;
    initAndActivate(lim);
    lim.setParameter(AestraLimit::kCeiling, 0.5f);

    const uint32_t latency = lim.getLatencySamples();
    const auto silence = makeSilence(latency + kBlockSize);
    auto out = processStereo(lim, silence, silence);

    for (uint32_t i = 0; i < out.left.size(); ++i) {
        if (std::abs(out.left[i]) > 1e-6f) return false;
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraLimit Tests\n";
    if (!testBypassPassesAudioUnchanged()) { std::cout << "FAIL: bypass\n"; return 1; }
    if (!testSilenceProducesSilence()) { std::cout << "FAIL: silence\n"; return 1; }
    if (!testTruePeakDoesNotExceedCeiling()) { std::cout << "FAIL: true peak ceiling\n"; return 1; }
    if (!testGainReductionApplied()) { std::cout << "FAIL: gain reduction\n"; return 1; }
    if (!testStateSaveLoadRoundtrip()) { std::cout << "FAIL: state roundtrip\n"; return 1; }
    if (!testStateRejectsBadMagic()) { std::cout << "FAIL: bad magic\n"; return 1; }
    if (!testStateRejectsTooShort()) { std::cout << "FAIL: short state\n"; return 1; }
    if (!testParameterCount()) { std::cout << "FAIL: param count\n"; return 1; }
    if (!testParameterClamping()) { std::cout << "FAIL: param clamping\n"; return 1; }
    if (!testSanitizeNanInput()) { std::cout << "FAIL: NaN sanitize\n"; return 1; }
    if (!testLatencyMatchesLookahead()) { std::cout << "FAIL: latency\n"; return 1; }
    if (!testMeteringUpdatesAfterProcess()) { std::cout << "FAIL: metering\n"; return 1; }
    if (!testOutputClipperBoundsOutput()) { std::cout << "FAIL: output clipper\n"; return 1; }
    if (!testZeroInputLatencyRamp()) { std::cout << "FAIL: zero input latency\n"; return 1; }
    std::cout << "All AestraLimit tests passed.\n";
    return 0;
}
