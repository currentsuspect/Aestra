// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraEQTest — V1 contract tests for the 6-band parametric EQ.

#include "Plugin/AestraEQ.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Aestra::Audio::Plugins;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 512;
constexpr double kTau = 6.28318530717958647692;

int g_testsPassed = 0;
int g_testsFailed = 0;

void generateTone(float* buffer, uint32_t frames, double freq, float amp) {
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        buffer[i] = static_cast<float>(std::sin(kTau * freq * t) * amp);
    }
}

void generateSilence(float* buffer, uint32_t frames) {
    std::memset(buffer, 0, frames * sizeof(float));
}

float calculateRMS(const float* buffer, uint32_t frames) {
    double sum = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return static_cast<float>(std::sqrt(sum / frames));
}

float calculateToneAmplitude(const float* buffer, uint32_t frames, double freq) {
    double sinSum = 0.0;
    double cosSum = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        const double phase = kTau * freq * static_cast<double>(i) / kSampleRate;
        sinSum += static_cast<double>(buffer[i]) * std::sin(phase);
        cosSum += static_cast<double>(buffer[i]) * std::cos(phase);
    }
    return static_cast<float>(2.0 * std::hypot(sinSum, cosSum) / static_cast<double>(frames));
}

float maxAbsDiff(const float* a, const float* b, uint32_t frames) {
    float maxDiff = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float diff = std::abs(a[i] - b[i]);
        if (diff > maxDiff) maxDiff = diff;
    }
    return maxDiff;
}

float maxAbsValue(const float* buffer, uint32_t frames) {
    float maxValue = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        maxValue = std::max(maxValue, std::abs(buffer[i]));
    }
    return maxValue;
}

float graphNormForHz(float hz) {
    return std::clamp(static_cast<float>(std::log10(std::max(hz, 20.0f) / 20.0f) / 3.0), 0.0f, 1.0f);
}

template <typename T>
std::vector<uint8_t> toBytes(const T& value) {
    const auto* ptr = reinterpret_cast<const uint8_t*>(&value);
    return std::vector<uint8_t>(ptr, ptr + sizeof(T));
}

bool hasNaNOrInf(const float* buffer, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        if (!std::isfinite(buffer[i])) return true;
    }
    return false;
}

bool report(const char* name, bool passed) {
    if (passed) {
        std::cout << "  PASS: " << name << "\n";
        ++g_testsPassed;
    } else {
        std::cout << "  FAIL: " << name << "\n";
        ++g_testsFailed;
    }
    return passed;
}

void processBlocks(AestraEQ& eq, const float* input, float* output, uint32_t totalFrames, uint32_t blockSize) {
    for (uint32_t offset = 0; offset < totalFrames; offset += blockSize) {
        uint32_t frames = std::min(blockSize, totalFrames - offset);
        const float* inPtr = input + offset;
        float* outPtr = output + offset;
        eq.process(&inPtr, &outPtr, 1, 1, frames);
    }
}

void processStereoBlock(AestraEQ& eq,
                        const float* leftIn,
                        const float* rightIn,
                        float* leftOut,
                        float* rightOut,
                        uint32_t frames) {
    const float* inputs[] = {leftIn, rightIn};
    float* outputs[] = {leftOut, rightOut};
    eq.process(inputs, outputs, 2, 2, frames);
}

void processStereoBlocks(AestraEQ& eq,
                         const float* leftIn,
                         const float* rightIn,
                         float* leftOut,
                         float* rightOut,
                         uint32_t totalFrames,
                         uint32_t blockSize) {
    for (uint32_t offset = 0; offset < totalFrames; offset += blockSize) {
        const uint32_t frames = std::min(blockSize, totalFrames - offset);
        processStereoBlock(eq, leftIn + offset, rightIn + offset, leftOut + offset, rightOut + offset, frames);
    }
}

void settleSmoothing(AestraEQ& eq, uint32_t numBlocks = 500) {
    std::vector<float> in(kBlockSize, 0.0f);
    std::vector<float> out(kBlockSize, 0.0f);
    for (uint32_t i = 0; i < numBlocks; ++i) {
        const float* ip = in.data();
        float* op = out.data();
        eq.process(&ip, &op, 1, 1, kBlockSize);
        std::copy(out.begin(), out.end(), in.begin());
    }
}

// ---- Tests ----

bool testDirectConstructionSafeDefaults() {
    AestraEQ eq;
    // Direct construction should not crash; params should be initialized
    float bypass = eq.getParameter(AestraEQ::kParamBypass);
    float bell1Gain = eq.getParameter(AestraEQ::kParamBell1Gain);
    return report("Direct construction safe defaults",
        bypass == 0.0f && bell1Gain == 0.5f);
}

bool testParameterDescriptorCount() {
    AestraEQ eq;
    auto params = eq.getParameters();
    return report("Descriptor count matches EQ parameter count",
        params.size() == AestraEQ::kParamCount);
}

bool testParameterDescriptorTypeExposedForMiddleBands() {
    AestraEQ eq;
    auto params = eq.getParameters();
    bool hasBell1Type = false;
    bool hasBell2Type = false;
    for (const auto& p : params) {
        hasBell1Type = hasBell1Type || p.id == AestraEQ::kParamBell1Type;
        hasBell2Type = hasBell2Type || p.id == AestraEQ::kParamBell2Type;
    }
    return report("Middle-band type params are exposed", hasBell1Type && hasBell2Type);
}

bool testNeutralGainDisplaysCleanZero() {
    AestraEQ eq;
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.5f);
    eq.setParameter(AestraEQ::kParamOutputGain, 0.5f);

    const bool ok = eq.getParameterDisplay(AestraEQ::kParamBell1Gain) == "0.0dB" &&
                    eq.getParameterDisplay(AestraEQ::kParamOutputGain) == "0.0dB";
    return report("Neutral gain display is clean zero", ok);
}

bool testPositiveGainDisplaysPlusPrefix() {
    AestraEQ eq;
    eq.setParameter(AestraEQ::kParamBell1Gain, (3.0f + 18.0f) / 36.0f);
    eq.setParameter(AestraEQ::kParamOutputGain, (3.0f + 18.0f) / 36.0f);

    const bool ok = eq.getParameterDisplay(AestraEQ::kParamBell1Gain) == "+3.0dB" &&
                    eq.getParameterDisplay(AestraEQ::kParamOutputGain) == "+3.0dB";
    return report("Positive gain display includes plus prefix", ok);
}

bool testHasEditorTrue() {
    AestraEQ eq;
    return report("hasEditor returns true", eq.hasEditor());
}

bool testBypassParity() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamBypass, 1.0f);

    const uint32_t frames = kBlockSize;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.5f);

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    float diff = maxAbsDiff(input.data(), output.data(), frames);
    return report("Bypass parity", diff < 1e-6f);
}

bool testActiveMultichannelPassthroughAboveStereo() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    constexpr uint32_t channels = 4;
    const uint32_t frames = kBlockSize;
    std::array<std::vector<float>, channels> inputs;
    std::array<std::vector<float>, channels> outputs;
    std::array<const float*, channels> inputPtrs{};
    std::array<float*, channels> outputPtrs{};

    for (uint32_t ch = 0; ch < channels; ++ch) {
        inputs[ch].resize(frames);
        outputs[ch].assign(frames, -9.0f);
        for (uint32_t i = 0; i < frames; ++i) {
            inputs[ch][i] = 0.001f * static_cast<float>((ch + 1) * (i + 1));
        }
        inputPtrs[ch] = inputs[ch].data();
        outputPtrs[ch] = outputs[ch].data();
    }

    eq.process(inputPtrs.data(), outputPtrs.data(), channels, channels, frames);

    const bool ok = maxAbsDiff(inputs[2].data(), outputs[2].data(), frames) < 1e-7f &&
                    maxAbsDiff(inputs[3].data(), outputs[3].data(), frames) < 1e-7f;
    return report("Active EQ passes through channels above stereo", ok);
}

bool testFlatEQEqualsInput() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    // All gain bands at 0 dB, cuts disabled
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.5f); // 0 dB
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Gain, 0.5f); // 0 dB
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    const uint32_t frames = kBlockSize;

    // Let smoothing settle
    std::vector<float> dummyIn(frames, 0.0f);
    std::vector<float> dummyOut(frames, 0.0f);
    for (int i = 0; i < 200; ++i) {
        const float* inPtr = dummyIn.data();
        float* outPtr = dummyOut.data();
        eq.process(&inPtr, &outPtr, 1, 1, frames);
    }

    // Now compare
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.5f);
    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    float rmsIn = calculateRMS(input.data(), frames);
    float rmsOut = calculateRMS(output.data(), frames);
    float ratio = rmsOut / std::max(rmsIn, 1e-12f);
    return report("Flat EQ equals input (0 dB bands)", ratio > 0.95f && ratio < 1.05f);
}

bool testHPFAttenuatesLowFreq() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, 0.392f); // 80 Hz
    eq.setParameter(AestraEQ::kParamHPFSlope, 0.333f); // 24 dB/oct
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    const uint32_t frames = kBlockSize * 8;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 40.0, 0.5f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    float rmsIn = calculateRMS(input.data(), frames);
    float rmsOut = calculateRMS(output.data(), frames);
    return report("HPF attenuates 40 Hz", rmsOut < rmsIn * 0.3f);
}

bool testLPFAttenuatesHighFreq() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLPFFreq, 0.0f); // 1000 Hz
    eq.setParameter(AestraEQ::kParamLPFSlope, 1.0f); // 96 dB/oct
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    settleSmoothing(eq);

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 10000.0, 0.5f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    float rmsIn = calculateRMS(input.data(), frames);
    float rmsOut = calculateRMS(output.data(), frames);
    return report("LPF attenuates 10 kHz", rmsOut < rmsIn * 0.3f);
}

bool testLowShelfBoostsBass() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLShFreq, 0.370f); // 200 Hz
    eq.setParameter(AestraEQ::kParamLShGain, 0.833f); // +12 dB
    eq.setParameter(AestraEQ::kParamLShQ, 0.061f); // 0.707
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    settleSmoothing(eq);

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 80.0, 0.3f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    float rmsIn = calculateRMS(input.data(), frames);
    float rmsOut = calculateRMS(output.data(), frames);
    float boostDb = 20.0f * std::log10(rmsOut / std::max(rmsIn, 1e-12f) + 1e-12f);
    return report("Low shelf boosts 80 Hz (+12 dB)", boostDb > 8.0f);
}

bool testHighShelfBoostsTreble() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHShFreq, 0.5f); // ~4.5kHz
    eq.setParameter(AestraEQ::kParamHShGain, 1.0f); // +18 dB
    eq.setParameter(AestraEQ::kParamHShQ, 0.061f); // 0.707
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    settleSmoothing(eq);

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 10000.0, 0.3f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    float rmsIn = calculateRMS(input.data(), frames);
    float rmsOut = calculateRMS(output.data(), frames);
    float boostDb = 20.0f * std::log10(rmsOut / std::max(rmsIn, 1e-12f) + 1e-12f);
    return report("High shelf boosts 10 kHz (+18 dB)", boostDb > 10.0f);
}

bool testBellBoost() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.430f); // 500 Hz
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.833f); // +12 dB
    eq.setParameter(AestraEQ::kParamBell1Q, 0.091f); // Q 1.0
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    settleSmoothing(eq);

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 500.0, 0.3f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    float rmsIn = calculateRMS(input.data(), frames);
    float rmsOut = calculateRMS(output.data(), frames);
    float boostDb = 20.0f * std::log10(rmsOut / std::max(rmsIn, 1e-12f) + 1e-12f);
    return report("Bell boost at 500 Hz (+12 dB)", boostDb > 8.0f);
}

bool testBellCut() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.430f); // 500 Hz
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.167f); // -12 dB
    eq.setParameter(AestraEQ::kParamBell1Q, 0.091f); // Q 1.0
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    settleSmoothing(eq);

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 500.0, 0.3f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    float rmsIn = calculateRMS(input.data(), frames);
    float rmsOut = calculateRMS(output.data(), frames);
    float cutDb = 20.0f * std::log10(rmsOut / std::max(rmsIn, 1e-12f) + 1e-12f);
    return report("Bell cut at 500 Hz (-12 dB)", cutDb < -8.0f);
}

bool testExtremeValuesNoNaN() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    // Set all bands to extreme values
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, 1.0f); // 500 Hz
    eq.setParameter(AestraEQ::kParamHPFSlope, 1.0f); // 96 dB/oct
    eq.setParameter(AestraEQ::kParamLShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLShFreq, 0.0f); // 40 Hz
    eq.setParameter(AestraEQ::kParamLShGain, 1.0f); // +18 dB
    eq.setParameter(AestraEQ::kParamLShQ, 1.0f); // Q 10.0
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 1.0f); // 8000 Hz
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.0f); // -18 dB
    eq.setParameter(AestraEQ::kParamBell1Q, 1.0f); // Q 10.0
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Freq, 0.0f); // 200 Hz
    eq.setParameter(AestraEQ::kParamBell2Gain, 1.0f); // +18 dB
    eq.setParameter(AestraEQ::kParamBell2Q, 0.0f); // Q 0.1
    eq.setParameter(AestraEQ::kParamHShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHShFreq, 0.0f); // 2000 Hz
    eq.setParameter(AestraEQ::kParamHShGain, 0.0f); // -18 dB
    eq.setParameter(AestraEQ::kParamHShQ, 1.0f); // Q 10.0
    eq.setParameter(AestraEQ::kParamLPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLPFFreq, 0.0f); // 1000 Hz
    eq.setParameter(AestraEQ::kParamLPFSlope, 1.0f); // 96 dB/oct
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    const uint32_t frames = kBlockSize * 16;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.5f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    return report("Extreme values produce no NaN/Inf", !hasNaNOrInf(output.data(), frames));
}

bool testSampleRateInit44100() {
    AestraEQ eq;
    bool ok = eq.initialize(44100.0, kBlockSize);
    eq.activate();

    const uint32_t frames = kBlockSize;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.5f);

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    return report("Sample rate 44.1k init + process", ok && !hasNaNOrInf(output.data(), frames));
}

bool testSampleRateInit96000() {
    AestraEQ eq;
    bool ok = eq.initialize(96000.0, kBlockSize);
    eq.activate();

    const uint32_t frames = kBlockSize;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.5f);

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    return report("Sample rate 96k init + process", ok && !hasNaNOrInf(output.data(), frames));
}

bool testStateV6Roundtrip() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    // Set non-default values
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, 0.5f);
    eq.setParameter(AestraEQ::kParamHPFSlope, 0.667f);
    eq.setParameter(AestraEQ::kParamLShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLShFreq, 0.3f);
    eq.setParameter(AestraEQ::kParamLShGain, 0.7f);
    eq.setParameter(AestraEQ::kParamLShQ, 0.2f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Freq, 0.8f);
    eq.setParameter(AestraEQ::kParamBell2Type, 1.0f / 3.0f); // Notch
    eq.setParameter(AestraEQ::kParamOutputGain, 0.75f);
    eq.setParameter(AestraEQ::kParamPolarityInvert, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2StereoMode, 0.75f); // Mid
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    auto state = eq.saveState();

    AestraEQ eq2;
    eq2.initialize(kSampleRate, kBlockSize);
    bool loaded = eq2.loadState(state);

    bool match = true;
    match &= eq2.getParameter(AestraEQ::kParamHPFEnable) == 1.0f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamHPFFreq) - 0.5f) < 0.001f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamHPFSlope) - 0.667f) < 0.001f;
    match &= eq2.getParameter(AestraEQ::kParamLShEnable) == 1.0f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamLShFreq) - 0.3f) < 0.001f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamLShGain) - 0.7f) < 0.001f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamLShQ) - 0.2f) < 0.001f;
    match &= eq2.getParameter(AestraEQ::kParamBell1Enable) == 0.0f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamBell2Freq) - 0.8f) < 0.001f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamBell2Type) - (1.0f / 3.0f)) < 0.001f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamOutputGain) - 0.75f) < 0.001f;
    match &= eq2.getParameter(AestraEQ::kParamPolarityInvert) == 1.0f;
    match &= std::abs(eq2.getParameter(AestraEQ::kParamBell2StereoMode) - 0.75f) < 0.001f;

    return report("State V6 roundtrip preserves stereo placement", loaded && match);
}

bool testLegacyV5StateLoadDefaultsStereoPlacement() {
    EQStateBlobV5 v5{};
    v5.magic = AestraEQ::kStateMagicV5;
    v5.version = 5;
    for (uint32_t i = 0; i < AestraEQ::kParamHPFStereoMode; ++i) {
        v5.params[i] = 0.5f;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&v5);
    std::vector<uint8_t> state(data, data + sizeof(v5));

    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    const bool loaded = eq.loadState(state);
    bool defaults = true;
    defaults &= eq.getParameter(AestraEQ::kParamHPFStereoMode) == 0.0f;
    defaults &= eq.getParameter(AestraEQ::kParamLShStereoMode) == 0.0f;
    defaults &= eq.getParameter(AestraEQ::kParamBell1StereoMode) == 0.0f;
    defaults &= eq.getParameter(AestraEQ::kParamBell2StereoMode) == 0.0f;
    defaults &= eq.getParameter(AestraEQ::kParamHShStereoMode) == 0.0f;
    defaults &= eq.getParameter(AestraEQ::kParamLPFStereoMode) == 0.0f;
    return report("Legacy V5 state loads with stereo placement defaults", loaded && defaults);
}

bool testLegacyV4StateLoadDefaultsPolarity() {
    EQStateBlobV4 v4{};
    v4.magic = AestraEQ::kStateMagicV4;
    v4.version = 4;
    for (uint32_t i = 0; i < AestraEQ::kParamPolarityInvert; ++i) {
        v4.params[i] = 0.5f;
    }
    v4.params[AestraEQ::kParamOutputGain] = 0.75f;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&v4);
    std::vector<uint8_t> state(data, data + sizeof(v4));

    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    const bool loaded = eq.loadState(state);
    const bool polarityDefault = eq.getParameter(AestraEQ::kParamPolarityInvert) == 0.0f;
    const bool outputPreserved = std::abs(eq.getParameter(AestraEQ::kParamOutputGain) - 0.75f) < 0.001f;
    return report("Legacy V4 state loads with normal polarity", loaded && polarityDefault && outputPreserved);
}

bool testLegacyV3StateLoadDefaultsOutputGain() {
    EQStateBlobV3 v3{};
    v3.magic = AestraEQ::kStateMagicV3;
    v3.version = 3;
    for (uint32_t i = 0; i < AestraEQ::kParamOutputGain; ++i) {
        v3.params[i] = 0.5f;
    }
    v3.params[AestraEQ::kParamBell1Type] = 1.0f;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&v3);
    std::vector<uint8_t> state(data, data + sizeof(v3));

    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    const bool loaded = eq.loadState(state);
    const bool defaults = std::abs(eq.getParameter(AestraEQ::kParamOutputGain) - 0.5f) < 0.001f;
    const bool preserved = std::abs(eq.getParameter(AestraEQ::kParamBell1Type) - 1.0f) < 0.001f;
    return report("Legacy V3 state loads with neutral output gain", loaded && defaults && preserved);
}

bool testLegacyV2StateLoadDefaultsNewTypes() {
    EQStateBlobV2 v2{};
    v2.magic = AestraEQ::kStateMagicV2;
    v2.version = 2;
    for (uint32_t i = 0; i < AestraEQ::kV1ParamCount; ++i) {
        v2.params[i] = 0.5f;
    }
    v2.params[AestraEQ::kParamBell1Enable] = 1.0f;
    v2.params[AestraEQ::kParamBell2Enable] = 1.0f;
    v2.params[AestraEQ::kParamHPFSlope] = 0.0f; // legacy 12 dB/oct
    v2.params[AestraEQ::kParamLPFSlope] = 1.0f; // legacy 48 dB/oct
    v2.params[AestraEQ::kParamBypass] = 0.0f;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&v2);
    std::vector<uint8_t> state(data, data + sizeof(v2));

    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    const bool loaded = eq.loadState(state);
    const bool migrated = std::abs(eq.getParameter(AestraEQ::kParamHPFSlope) - (1.0f / 6.0f)) < 0.001f &&
                          std::abs(eq.getParameter(AestraEQ::kParamLPFSlope) - (4.0f / 6.0f)) < 0.001f;
    const bool defaults = eq.getParameter(AestraEQ::kParamBell1Type) == 0.0f &&
                          eq.getParameter(AestraEQ::kParamBell2Type) == 0.0f &&
                          std::abs(eq.getParameter(AestraEQ::kParamOutputGain) - 0.5f) < 0.001f &&
                          eq.getParameter(AestraEQ::kParamPolarityInvert) == 0.0f;
    return report("Legacy V2 state loads with Bell type defaults and migrated slopes",
                  loaded && defaults && migrated);
}

bool testLegacyV1StateLoad() {
    // Create a V1 state blob
    EQStateBlobV1 v1{};
    v1.magic = 0x45510001;
    v1.version = 1;

    // Set band 0 as LowCut at 200Hz
    v1.enabled[0] = 1;
    v1.types[0] = static_cast<uint8_t>(FilterType::LowCut);
    v1.params[0] = 1.0f; // enable
    v1.params[1] = 1.0f / 7.0f; // type = LowCut
    v1.params[2] = 0.5f; // freq
    v1.params[3] = 0.5f; // gain
    v1.params[4] = 0.0f; // legacy 12 dB/oct slope

    // Set band 2 as Bell
    v1.enabled[2] = 1;
    v1.types[2] = static_cast<uint8_t>(FilterType::Bell);
    v1.params[10] = 1.0f;
    v1.params[11] = 0.0f;
    v1.params[12] = 0.6f;
    v1.params[13] = 0.7f;
    v1.params[14] = 0.3f;

    // Bands 6-7 should be ignored
    v1.enabled[6] = 1;
    v1.types[6] = static_cast<uint8_t>(FilterType::Bell);
    v1.enabled[7] = 1;
    v1.types[7] = static_cast<uint8_t>(FilterType::Bell);

    v1.params[40] = 0.0f; // bypass off

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&v1);
    std::vector<uint8_t> state(data, data + sizeof(v1));

    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    bool loaded = eq.loadState(state);

    bool ok = true;
    ok &= loaded;
    ok &= eq.getParameter(AestraEQ::kParamHPFEnable) == 1.0f;
    ok &= std::abs(eq.getParameter(AestraEQ::kParamHPFFreq) - 0.5f) < 0.001f;
    ok &= std::abs(eq.getParameter(AestraEQ::kParamHPFSlope) - (1.0f / 6.0f)) < 0.001f;
    ok &= eq.getParameter(AestraEQ::kParamBell1Enable) == 1.0f;
    ok &= std::abs(eq.getParameter(AestraEQ::kParamBell1Freq) - 0.6f) < 0.001f;

    return report("Legacy V1 state migration", ok);
}

bool testCorruptStateFails() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    std::vector<uint8_t> corrupt = {0xDE, 0xAD, 0xBE, 0xEF};
    bool loaded = eq.loadState(corrupt);
    return report("Corrupt state fails safely", !loaded);
}

bool testShortStateFails() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    std::vector<uint8_t> tooShort(4, 0xFF);
    bool loaded = eq.loadState(tooShort);
    return report("Short state fails safely", !loaded);
}

bool testNaNInputRecovers() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.5f);
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.7f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    const uint32_t frames = kBlockSize;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);

    // First block: valid audio
    generateTone(input.data(), frames, 1000.0, 0.5f);
    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    // Second block: NaN input
    std::vector<float> nanInput(frames, std::numeric_limits<float>::quiet_NaN());
    inPtr = nanInput.data();
    outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    // Third block: valid audio again — should recover
    std::vector<float> validInput(frames, 0.0f);
    std::vector<float> validOutput(frames, 0.0f);
    generateTone(validInput.data(), frames, 1000.0, 0.5f);
    inPtr = validInput.data();
    outPtr = validOutput.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    return report("NaN input recovers to valid output", !hasNaNOrInf(validOutput.data(), frames));
}

bool testSilenceStability() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.5f);
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.8f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    const uint32_t frames = kBlockSize;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);

    // Process many blocks of silence
    bool stable = true;
    for (int block = 0; block < 100; ++block) {
        generateSilence(input.data(), frames);
        const float* inPtr = input.data();
        float* outPtr = output.data();
        eq.process(&inPtr, &outPtr, 1, 1, frames);
        if (hasNaNOrInf(output.data(), frames)) {
            stable = false;
            break;
        }
        float rms = calculateRMS(output.data(), frames);
        if (rms > 1e-6f) {
            stable = false;
            break;
        }
    }
    return report("Silence stability (100 blocks)", stable);
}

bool testSmoothingNoDiscontinuity() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.3f);
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.5f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    const uint32_t frames = kBlockSize;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 500.0, 0.5f);

    // Process a few blocks to settle
    for (int i = 0; i < 10; ++i) {
        const float* inPtr = input.data();
        float* outPtr = output.data();
        eq.process(&inPtr, &outPtr, 1, 1, frames);
    }

    // Now snap frequency to max and process — smoothing should prevent discontinuity
    eq.setParameter(AestraEQ::kParamBell1Freq, 1.0f);

    float prevMax = 0.0f;
    bool smooth = true;
    for (int block = 0; block < 20; ++block) {
        const float* inPtr = input.data();
        float* outPtr = output.data();
        eq.process(&inPtr, &outPtr, 1, 1, frames);
        if (hasNaNOrInf(output.data(), frames)) {
            smooth = false;
            break;
        }
    }
    return report("Smoothing: no NaN/Inf on parameter change", smooth);
}

bool testAnalyzerPublishesDistinctPreAndPostWindows() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.430f); // 500 Hz
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.833f); // +12 dB
    eq.setParameter(AestraEQ::kParamBell1Q, 0.091f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    settleSmoothing(eq);

    const uint32_t frames = AestraEQ::kAnalyzerWindowSize * 2;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 500.0, 0.3f);
    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    std::array<float, AestraEQ::kAnalyzerWindowSize> pre{};
    std::array<float, AestraEQ::kAnalyzerWindowSize> post{};
    uint64_t preSerial = 0;
    uint64_t postSerial = 0;
    const bool hasPre = eq.getAnalyzerWindow(pre, &preSerial, AestraEQ::AnalyzerSource::Pre);
    const bool hasPost = eq.getAnalyzerWindow(post, &postSerial, AestraEQ::AnalyzerSource::Post);

    double diffSum = 0.0;
    for (size_t i = 0; i < pre.size(); ++i) {
        diffSum += std::abs(static_cast<double>(pre[i]) - static_cast<double>(post[i]));
    }

    return report("Analyzer publishes distinct Pre and Post windows",
                  hasPre && hasPost && preSerial > 0 && postSerial > 0 && diffSum > 0.01);
}

bool testAnalyzerPublishesStereoPlacementWindows() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    const uint32_t frames = AestraEQ::kAnalyzerWindowSize * 2;
    std::vector<float> leftIn(frames, 0.0f);
    std::vector<float> rightIn(frames, 0.0f);
    std::vector<float> leftOut(frames, 0.0f);
    std::vector<float> rightOut(frames, 0.0f);
    generateTone(leftIn.data(), frames, 500.0, 0.4f);
    processStereoBlocks(eq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames, kBlockSize);

    std::array<float, AestraEQ::kAnalyzerWindowSize> stereo{};
    std::array<float, AestraEQ::kAnalyzerWindowSize> left{};
    std::array<float, AestraEQ::kAnalyzerWindowSize> right{};
    std::array<float, AestraEQ::kAnalyzerWindowSize> mid{};
    std::array<float, AestraEQ::kAnalyzerWindowSize> side{};
    const bool hasStereo = eq.getAnalyzerWindow(stereo, nullptr, AestraEQ::AnalyzerSource::Pre, AestraEQ::StereoMode::Stereo);
    const bool hasLeft = eq.getAnalyzerWindow(left, nullptr, AestraEQ::AnalyzerSource::Pre, AestraEQ::StereoMode::Left);
    const bool hasRight = eq.getAnalyzerWindow(right, nullptr, AestraEQ::AnalyzerSource::Pre, AestraEQ::StereoMode::Right);
    const bool hasMid = eq.getAnalyzerWindow(mid, nullptr, AestraEQ::AnalyzerSource::Pre, AestraEQ::StereoMode::Mid);
    const bool hasSide = eq.getAnalyzerWindow(side, nullptr, AestraEQ::AnalyzerSource::Pre, AestraEQ::StereoMode::Side);

    const float leftRms = calculateRMS(left.data(), static_cast<uint32_t>(left.size()));
    const float rightRms = calculateRMS(right.data(), static_cast<uint32_t>(right.size()));
    const float stereoRms = calculateRMS(stereo.data(), static_cast<uint32_t>(stereo.size()));
    const float midRms = calculateRMS(mid.data(), static_cast<uint32_t>(mid.size()));
    const float sideRms = calculateRMS(side.data(), static_cast<uint32_t>(side.size()));

    const bool levels =
        leftRms > 0.20f &&
        rightRms < 0.001f &&
        std::abs(stereoRms - leftRms * 0.5f) < 0.02f &&
        std::abs(midRms - leftRms * 0.5f) < 0.02f &&
        std::abs(sideRms - leftRms * 0.5f) < 0.02f;

    return report("Analyzer publishes ST/L/R/M/S placement windows",
                  hasStereo && hasLeft && hasRight && hasMid && hasSide && levels);
}

bool testBandStereoPlacementProcessesOnlySelectedSide() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.548f); // approximately 1 kHz
    eq.setParameter(AestraEQ::kParamBell1Gain, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Q, 0.2f);
    eq.setParameter(AestraEQ::kParamBell1StereoMode, 0.25f); // Left
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq);

    constexpr uint32_t frames = 2048;
    std::vector<float> leftIn(frames), rightIn(frames), leftOut(frames), rightOut(frames);
    generateTone(leftIn.data(), frames, 1000.0, 0.18f);
    generateTone(rightIn.data(), frames, 1000.0, 0.18f);
    processStereoBlock(eq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);

    const float rightDiff = maxAbsDiff(rightIn.data(), rightOut.data(), frames);
    const float leftInRms = calculateRMS(leftIn.data(), frames);
    const float leftOutRms = calculateRMS(leftOut.data(), frames);
    const bool leftChanged = leftOutRms > leftInRms * 1.20f;
    const bool rightUnchanged = rightDiff < 1.0e-4f;
    return report("Band stereo placement processes only selected side", leftChanged && rightUnchanged);
}

bool testBandStereoPlacementProcessesRightSide() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.548f); // approximately 1 kHz
    eq.setParameter(AestraEQ::kParamBell1Gain, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Q, 0.2f);
    eq.setParameter(AestraEQ::kParamBell1StereoMode, 0.50f); // Right
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq);

    constexpr uint32_t frames = 2048;
    std::vector<float> leftIn(frames), rightIn(frames), leftOut(frames), rightOut(frames);
    generateTone(leftIn.data(), frames, 1000.0, 0.18f);
    generateTone(rightIn.data(), frames, 1000.0, 0.18f);
    processStereoBlock(eq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);

    const float leftDiff = maxAbsDiff(leftIn.data(), leftOut.data(), frames);
    const float rightInRms = calculateRMS(rightIn.data(), frames);
    const float rightOutRms = calculateRMS(rightOut.data(), frames);
    const bool rightChanged = rightOutRms > rightInRms * 1.20f;
    const bool leftUnchanged = leftDiff < 1.0e-4f;
    return report("Band stereo placement processes right side", rightChanged && leftUnchanged);
}

bool testBandStereoPlacementProcessesMidSignal() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.548f); // approximately 1 kHz
    eq.setParameter(AestraEQ::kParamBell1Gain, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Q, 0.2f);
    eq.setParameter(AestraEQ::kParamBell1StereoMode, 0.75f); // Mid
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq);

    constexpr uint32_t frames = 2048;
    std::vector<float> leftIn(frames), rightIn(frames), leftOut(frames), rightOut(frames), sideLeak(frames);
    generateTone(leftIn.data(), frames, 1000.0, 0.18f);
    rightIn = leftIn;
    processStereoBlock(eq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);

    for (uint32_t i = 0; i < frames; ++i) {
        sideLeak[i] = leftOut[i] - rightOut[i];
    }

    const float leftInRms = calculateRMS(leftIn.data(), frames);
    const bool leftChanged = calculateRMS(leftOut.data(), frames) > leftInRms * 1.20f;
    const bool rightChanged = calculateRMS(rightOut.data(), frames) > leftInRms * 1.20f;
    const bool staysMono = calculateRMS(sideLeak.data(), frames) < 1.0e-4f;
    return report("Band stereo placement processes mid signal", leftChanged && rightChanged && staysMono);
}

bool testBandStereoPlacementProcessesSideSignal() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.548f); // approximately 1 kHz
    eq.setParameter(AestraEQ::kParamBell1Gain, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Q, 0.2f);
    eq.setParameter(AestraEQ::kParamBell1StereoMode, 1.0f); // Side
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq);

    constexpr uint32_t frames = 2048;
    std::vector<float> leftIn(frames), rightIn(frames), leftOut(frames), rightOut(frames), midLeak(frames);
    generateTone(leftIn.data(), frames, 1000.0, 0.18f);
    for (uint32_t i = 0; i < frames; ++i) {
        rightIn[i] = -leftIn[i];
    }
    processStereoBlock(eq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);

    for (uint32_t i = 0; i < frames; ++i) {
        midLeak[i] = leftOut[i] + rightOut[i];
    }

    const float leftInRms = calculateRMS(leftIn.data(), frames);
    const bool leftChanged = calculateRMS(leftOut.data(), frames) > leftInRms * 1.20f;
    const bool rightChanged = calculateRMS(rightOut.data(), frames) > leftInRms * 1.20f;
    const bool staysSide = calculateRMS(midLeak.data(), frames) < 1.0e-4f;
    return report("Band stereo placement processes side signal", leftChanged && rightChanged && staysSide);
}

bool testDynamicBandStereoPlacementProcessesLeftAndRight() {
    auto configureEq = [](AestraEQ& eq, AestraEQ::StereoMode mode) {
        eq.initialize(kSampleRate, kBlockSize);
        eq.activate();
        eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
        eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
        eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
        eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
        eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
        eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
        const float freqNorm = static_cast<float>(std::log10(1000.0 / 20.0) / 3.0);
        const bool set = eq.setDynamicBandSlot(AestraEQ::kLegacyBandCount, {
            true,
            FilterType::Bell,
            mode,
            freqNorm,
            1.0f,
            0.2f,
            false,
        });
        if (!set) {
            eq.setParameter(AestraEQ::kParamBypass, 1.0f);
        }
        settleSmoothing(eq);
    };

    constexpr uint32_t frames = 2048;
    std::vector<float> leftIn(frames), rightIn(frames), leftOut(frames), rightOut(frames);
    generateTone(leftIn.data(), frames, 1000.0, 0.18f);
    generateTone(rightIn.data(), frames, 1000.0, 0.18f);

    AestraEQ leftEq;
    configureEq(leftEq, AestraEQ::StereoMode::Left);
    processStereoBlock(leftEq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);
    const bool leftModeOk = calculateRMS(leftOut.data(), frames) > calculateRMS(leftIn.data(), frames) * 1.20f &&
                            maxAbsDiff(rightIn.data(), rightOut.data(), frames) < 1.0e-4f;

    std::fill(leftOut.begin(), leftOut.end(), 0.0f);
    std::fill(rightOut.begin(), rightOut.end(), 0.0f);
    AestraEQ rightEq;
    configureEq(rightEq, AestraEQ::StereoMode::Right);
    processStereoBlock(rightEq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);
    const bool rightModeOk = calculateRMS(rightOut.data(), frames) > calculateRMS(rightIn.data(), frames) * 1.20f &&
                             maxAbsDiff(leftIn.data(), leftOut.data(), frames) < 1.0e-4f;

    return report("Dynamic band stereo placement processes left and right", leftModeOk && rightModeOk);
}

bool testDynamicBandStereoPlacementProcessesMidAndSide() {
    auto configureEq = [](AestraEQ& eq, AestraEQ::StereoMode mode) {
        eq.initialize(kSampleRate, kBlockSize);
        eq.activate();
        eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
        eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
        eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
        eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
        eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
        eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
        const float freqNorm = static_cast<float>(std::log10(1000.0 / 20.0) / 3.0);
        const bool set = eq.setDynamicBandSlot(AestraEQ::kLegacyBandCount, {
            true,
            FilterType::Bell,
            mode,
            freqNorm,
            1.0f,
            0.2f,
            false,
        });
        if (!set) {
            eq.setParameter(AestraEQ::kParamBypass, 1.0f);
        }
        settleSmoothing(eq);
    };

    constexpr uint32_t frames = 2048;
    std::vector<float> leftIn(frames), rightIn(frames), leftOut(frames), rightOut(frames), leak(frames);
    generateTone(leftIn.data(), frames, 1000.0, 0.18f);
    rightIn = leftIn;

    AestraEQ midEq;
    configureEq(midEq, AestraEQ::StereoMode::Mid);
    processStereoBlock(midEq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);
    for (uint32_t i = 0; i < frames; ++i) {
        leak[i] = leftOut[i] - rightOut[i];
    }
    const bool midOk = calculateRMS(leftOut.data(), frames) > calculateRMS(leftIn.data(), frames) * 1.20f &&
                       calculateRMS(rightOut.data(), frames) > calculateRMS(rightIn.data(), frames) * 1.20f &&
                       calculateRMS(leak.data(), frames) < 1.0e-4f;

    for (uint32_t i = 0; i < frames; ++i) {
        rightIn[i] = -leftIn[i];
        leftOut[i] = 0.0f;
        rightOut[i] = 0.0f;
        leak[i] = 0.0f;
    }

    AestraEQ sideEq;
    configureEq(sideEq, AestraEQ::StereoMode::Side);
    processStereoBlock(sideEq, leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), frames);
    for (uint32_t i = 0; i < frames; ++i) {
        leak[i] = leftOut[i] + rightOut[i];
    }
    const bool sideOk = calculateRMS(leftOut.data(), frames) > calculateRMS(leftIn.data(), frames) * 1.20f &&
                        calculateRMS(rightOut.data(), frames) > calculateRMS(leftIn.data(), frames) * 1.20f &&
                        calculateRMS(leak.data(), frames) < 1.0e-4f;

    return report("Dynamic band stereo placement processes mid and side", midOk && sideOk);
}

bool testParameterCount() {
    AestraEQ eq;
    return report("getParameterCount matches kParamCount", eq.getParameterCount() == AestraEQ::kParamCount);
}

bool testLegacyBandSlotMetadata() {
    const auto hp = AestraEQ::legacyBandSlot(0);
    const auto b1 = AestraEQ::legacyBandSlot(2);
    const auto lp = AestraEQ::legacyBandSlot(5);
    const auto clamped = AestraEQ::legacyBandSlot(999);

    const bool ok = AestraEQ::kLegacyBandCount == AestraEQ::kV1BandCount &&
                    AestraEQ::kMaxDynamicBands == 24 &&
                    std::strcmp(hp.id, "HP") == 0 &&
                    hp.enableId == AestraEQ::kParamHPFEnable &&
                    hp.freqId == AestraEQ::kParamHPFFreq &&
                    hp.gainId == 0 &&
                    hp.qId == AestraEQ::kParamHPFSlope &&
                    hp.typeId == 0 &&
                    hp.stereoModeId == AestraEQ::kParamHPFStereoMode &&
                    hp.defaultType == FilterType::LowCut &&
                    hp.usesSlope &&
                    std::strcmp(b1.id, "B1") == 0 &&
                    b1.typeId == AestraEQ::kParamBell1Type &&
                    b1.gainId == AestraEQ::kParamBell1Gain &&
                    b1.defaultType == FilterType::Bell &&
                    !b1.usesSlope &&
                    std::strcmp(lp.id, "LP") == 0 &&
                    lp.defaultType == FilterType::HighCut &&
                    lp.usesSlope &&
                    std::strcmp(clamped.id, "LP") == 0;

    return report("Legacy band slot metadata maps the six V1 bands", ok);
}

bool testDynamicBandSlotsDefaultInactive() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    bool ok = eq.getDynamicBandSlotCount() == AestraEQ::kMaxDynamicBands;
    for (uint32_t slot = 0; slot < AestraEQ::kMaxDynamicBands; ++slot) {
        ok &= AestraEQ::isLegacyDynamicBandSlot(slot) == (slot < AestraEQ::kLegacyBandCount);
        if (slot >= AestraEQ::kLegacyBandCount) {
            ok &= !eq.isDynamicBandSlotEnabled(slot);
            ok &= eq.getDynamicBandSlotStageCount(slot) == 0u;
        }
    }

    return report("Dynamic band slots 6-23 default inactive", ok);
}

bool testDynamicBandSlotAllocatorSkipsLegacySlots() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    const bool ok = !eq.isDynamicBandSlotAllocatable(0) &&
                    !eq.isDynamicBandSlotAllocatable(AestraEQ::kLegacyBandCount - 1) &&
                    eq.isDynamicBandSlotAllocatable(AestraEQ::kLegacyBandCount) &&
                    eq.findNextAvailableDynamicBandSlot(0) == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    eq.findNextAvailableDynamicBandSlot(AestraEQ::kLegacyBandCount) ==
                        static_cast<int32_t>(AestraEQ::kLegacyBandCount + 1) &&
                    eq.findNextAvailableDynamicBandSlot(AestraEQ::kMaxDynamicBands - 1) ==
                        static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    eq.findNextAvailableDynamicBandSlot(999) == static_cast<int32_t>(AestraEQ::kLegacyBandCount);

    return report("Dynamic band allocator skips legacy slots and wraps", ok);
}

bool testDynamicBandSlotDefaultsAreNeutralForFutureSlots() {
    const auto hp = AestraEQ::dynamicBandSlotDefaults(0);
    const auto b1 = AestraEQ::dynamicBandSlotDefaults(2);
    const auto dynamic = AestraEQ::dynamicBandSlotDefaults(AestraEQ::kLegacyBandCount);
    const auto last = AestraEQ::dynamicBandSlotDefaults(AestraEQ::kMaxDynamicBands - 1);

    const bool legacyOk = !hp.enabled &&
                          hp.type == FilterType::LowCut &&
                          hp.stereoMode == AestraEQ::StereoMode::Stereo &&
                          hp.usesSlope &&
                          !b1.enabled &&
                          b1.type == FilterType::Bell &&
                          std::abs(b1.gainNorm - 0.5f) < 0.001f &&
                          !b1.usesSlope;

    const bool dynamicOk = !dynamic.enabled &&
                           dynamic.type == FilterType::Bell &&
                           dynamic.stereoMode == AestraEQ::StereoMode::Stereo &&
                           std::abs(dynamic.frequencyNorm - 0.5f) < 0.001f &&
                           std::abs(dynamic.gainNorm - 0.5f) < 0.001f &&
                           std::abs(dynamic.qOrSlopeNorm - AestraEQ::defaultParameterValue(AestraEQ::kParamBell1Q)) <
                               0.001f &&
                           !dynamic.usesSlope &&
                           !last.enabled &&
                           last.type == FilterType::Bell &&
                           last.stereoMode == AestraEQ::StereoMode::Stereo &&
                           !last.usesSlope;

    return report("Dynamic band defaults keep future slots neutral", legacyOk && dynamicOk);
}

bool testDefaultStateStartsWithNoBandsEnabled() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    bool allOff = true;
    for (uint32_t i = 0; i < AestraEQ::kMaxDynamicBands; ++i) {
        allOff &= !eq.isDynamicBandSlotEnabled(i);
    }

    return report("Default state starts with no bands enabled", allOff);
}

bool testDynamicBandSlotSnapshotReportsLegacyAndFutureSlots() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, 0.75f);
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.25f);
    eq.setParameter(AestraEQ::kParamBell1Q, 0.35f);
    eq.setParameter(AestraEQ::kParamBell1Type, 1.0f / 3.0f); // Notch
    eq.setParameter(AestraEQ::kParamBell1StereoMode, 1.0f);  // Side

    const auto legacy = eq.getDynamicBandSlotSnapshot(2);
    const auto dynamic = eq.getDynamicBandSlotSnapshot(AestraEQ::kLegacyBandCount);
    const auto clamped = eq.getDynamicBandSlotSnapshot(999);

    const bool legacyOk = legacy.slotIndex == 2 &&
                          legacy.legacySlot &&
                          !legacy.enabled &&
                          legacy.type == FilterType::Notch &&
                          legacy.stereoMode == AestraEQ::StereoMode::Side &&
                          std::abs(legacy.frequencyNorm - 0.75f) < 0.001f &&
                          std::abs(legacy.gainNorm - 0.25f) < 0.001f &&
                          std::abs(legacy.qOrSlopeNorm - 0.35f) < 0.001f &&
                          !legacy.usesSlope;

    const bool dynamicOk = dynamic.slotIndex == AestraEQ::kLegacyBandCount &&
                           !dynamic.legacySlot &&
                           !dynamic.enabled &&
                           dynamic.type == FilterType::Bell &&
                           dynamic.stereoMode == AestraEQ::StereoMode::Stereo &&
                           std::abs(dynamic.frequencyNorm - 0.5f) < 0.001f &&
                           std::abs(dynamic.gainNorm - 0.5f) < 0.001f &&
                           !dynamic.usesSlope &&
                           clamped.slotIndex == AestraEQ::kMaxDynamicBands - 1 &&
                           !clamped.legacySlot &&
                           clamped.type == FilterType::Bell;

    return report("Dynamic band snapshots expose legacy live values and future defaults", legacyOk && dynamicOk);
}

bool testDynamicBandGraphCreationDefaultsClampClickedPoint() {
    const auto created = AestraEQ::dynamicBandGraphCreationDefaults(0.25f, 0.75f);
    const auto lowCut = AestraEQ::dynamicBandGraphCreationDefaults(0.0f, 0.25f);
    const auto lowShelf = AestraEQ::dynamicBandGraphCreationDefaults(graphNormForHz(35.0f), 0.70f);
    const auto lowMidNotch = AestraEQ::dynamicBandGraphCreationDefaults(graphNormForHz(500.0f), 0.05f);
    const auto highShelf = AestraEQ::dynamicBandGraphCreationDefaults(graphNormForHz(10000.0f), 0.30f);
    const auto highCut = AestraEQ::dynamicBandGraphCreationDefaults(graphNormForHz(18000.0f), 0.85f);
    const auto clampedLow = AestraEQ::dynamicBandGraphCreationDefaults(-1.0f, -0.25f);
    const auto clampedHigh = AestraEQ::dynamicBandGraphCreationDefaults(2.0f, 4.0f);

    const bool createdOk = created.enabled &&
                           created.type == FilterType::Bell &&
                           created.stereoMode == AestraEQ::StereoMode::Stereo &&
                           std::abs(created.frequencyNorm - 0.25f) < 0.001f &&
                           std::abs(created.gainNorm - 0.75f) < 0.001f &&
                           std::abs(created.qOrSlopeNorm - AestraEQ::defaultParameterValue(AestraEQ::kParamBell1Q)) <
                               0.001f &&
                           !created.usesSlope;
    const bool frequencyAwareOk =
        lowCut.type == FilterType::LowCut &&
        lowCut.usesSlope &&
        std::abs(lowCut.gainNorm - 0.5f) < 0.001f &&
        std::abs(lowCut.qOrSlopeNorm - AestraEQ::defaultParameterValue(AestraEQ::kParamHPFSlope)) < 0.001f &&
        lowShelf.type == FilterType::LowShelf &&
        !lowShelf.usesSlope &&
        std::abs(lowShelf.gainNorm - 0.70f) < 0.001f &&
        lowMidNotch.type == FilterType::Notch &&
        !lowMidNotch.usesSlope &&
        std::abs(lowMidNotch.gainNorm - 0.5f) < 0.001f &&
        highShelf.type == FilterType::HighShelf &&
        !highShelf.usesSlope &&
        std::abs(highShelf.gainNorm - 0.30f) < 0.001f &&
        highCut.type == FilterType::HighCut &&
        highCut.usesSlope &&
        std::abs(highCut.gainNorm - 0.5f) < 0.001f &&
        std::abs(highCut.qOrSlopeNorm - AestraEQ::defaultParameterValue(AestraEQ::kParamLPFSlope)) < 0.001f;
    const bool clampedOk = std::abs(clampedLow.frequencyNorm - 0.0f) < 0.001f &&
                           std::abs(clampedLow.gainNorm - 0.5f) < 0.001f &&
                           std::abs(clampedHigh.frequencyNorm - 1.0f) < 0.001f &&
                           std::abs(clampedHigh.gainNorm - 0.5f) < 0.001f;

    return report("Dynamic band graph creation defaults clamp clicked point", createdOk && frequencyAwareOk && clampedOk);
}

bool testResetToEmptyStateClearsAllBands() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 1.0f);
    const int32_t first = eq.createDynamicBandAtGraphPoint(graphNormForHz(500.0f), 0.75f);
    const int32_t second = eq.createDynamicBandAtGraphPoint(graphNormForHz(8000.0f), 0.25f);

    eq.resetToEmptyState();

    bool legacyOff = true;
    for (uint32_t i = 0; i < AestraEQ::kLegacyBandCount; ++i) {
        legacyOff &= !eq.isDynamicBandSlotEnabled(i);
    }
    bool dynamicOff = first >= 0 && second >= 0;
    for (uint32_t i = AestraEQ::kLegacyBandCount; i < AestraEQ::kMaxDynamicBands; ++i) {
        dynamicOff &= !eq.isDynamicBandSlotEnabled(i);
    }

    return report("Reset to empty state clears all bands", legacyOff && dynamicOff);
}

bool testDynamicBandGraphClickAllocatesNextAvailableSlot() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    const int32_t first = eq.createDynamicBandAtGraphPoint(0.25f, 0.75f);
    const int32_t second = eq.createDynamicBandAtGraphPoint(0.40f, 0.60f, static_cast<uint32_t>(first));

    const auto firstSnapshot = eq.getDynamicBandSlotSnapshot(static_cast<uint32_t>(first));
    const auto secondSnapshot = eq.getDynamicBandSlotSnapshot(static_cast<uint32_t>(second));

    const bool firstOk = first == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                         firstSnapshot.slotIndex == AestraEQ::kLegacyBandCount &&
                         !firstSnapshot.legacySlot &&
                         firstSnapshot.enabled &&
                         firstSnapshot.type == FilterType::Bell &&
                         firstSnapshot.stereoMode == AestraEQ::StereoMode::Stereo &&
                         std::abs(firstSnapshot.frequencyNorm - 0.25f) < 0.001f &&
                         std::abs(firstSnapshot.gainNorm - 0.75f) < 0.001f &&
                         std::abs(firstSnapshot.qOrSlopeNorm -
                                  AestraEQ::defaultParameterValue(AestraEQ::kParamBell1Q)) < 0.001f &&
                         !firstSnapshot.usesSlope;

    const bool secondOk = second == static_cast<int32_t>(AestraEQ::kLegacyBandCount + 1) &&
                          secondSnapshot.enabled &&
                          std::abs(secondSnapshot.frequencyNorm - 0.40f) < 0.001f &&
                          std::abs(secondSnapshot.gainNorm - 0.60f) < 0.001f;

    return report("Dynamic band graph click allocates next available slot", firstOk && secondOk);
}

bool testDynamicBandsAllowMultipleSameFilterType() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    const int32_t first = eq.createDynamicBandSlot({
        true,
        FilterType::Bell,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(700.0f),
        0.72f,
        AestraEQ::defaultParameterValue(AestraEQ::kParamBell1Q),
        false,
    });
    const int32_t second = eq.createDynamicBandSlot({
        true,
        FilterType::Bell,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(1400.0f),
        0.30f,
        AestraEQ::defaultParameterValue(AestraEQ::kParamBell1Q),
        false,
    }, static_cast<uint32_t>(first));

    const auto a = eq.getDynamicBandSlotSnapshot(static_cast<uint32_t>(first));
    const auto b = eq.getDynamicBandSlotSnapshot(static_cast<uint32_t>(second));
    const bool ok = first == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    second == static_cast<int32_t>(AestraEQ::kLegacyBandCount + 1) &&
                    a.enabled &&
                    b.enabled &&
                    !a.legacySlot &&
                    !b.legacySlot &&
                    a.type == FilterType::Bell &&
                    b.type == FilterType::Bell;

    return report("Dynamic bands allow multiple same filter type", ok);
}

bool testDynamicBandSlotClearReopensSlotForGraphClick() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    const int32_t first = eq.createDynamicBandAtGraphPoint(0.25f, 0.75f);
    const bool cleared = eq.clearDynamicBandSlot(static_cast<uint32_t>(first));
    const int32_t reused = eq.createDynamicBandAtGraphPoint(0.55f, 0.45f);
    const auto snapshot = eq.getDynamicBandSlotSnapshot(static_cast<uint32_t>(reused));

    const bool ok = first == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    cleared &&
                    reused == first &&
                    snapshot.enabled &&
                    std::abs(snapshot.frequencyNorm - 0.55f) < 0.001f &&
                    std::abs(snapshot.gainNorm - 0.45f) < 0.001f &&
                    !eq.clearDynamicBandSlot(0);

    return report("Dynamic band clear reopens slot for graph click", ok);
}

bool testDynamicBandGraphClickReportsFullCapacity() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    bool ok = true;
    for (uint32_t i = AestraEQ::kLegacyBandCount; i < AestraEQ::kMaxDynamicBands; ++i) {
        const int32_t slot = eq.createDynamicBandAtGraphPoint(0.5f, 0.5f, i - 1u);
        ok &= slot == static_cast<int32_t>(i);
    }
    ok &= eq.createDynamicBandAtGraphPoint(0.2f, 0.8f) == -1;

    return report("Dynamic band graph click reports full capacity", ok);
}

bool testDynamicBandContributesToMagnitudeResponse() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    const float freqNorm = static_cast<float>(std::log10(1000.0 / 20.0) / 3.0);
    const int32_t slot = eq.createDynamicBandAtGraphPoint(freqNorm, 0.833f); // approximately +12 dB

    const double bandDb = slot >= 0 ? eq.getBandMagnitudeResponseDb(static_cast<uint32_t>(slot), 1000.0) : 0.0;
    const double totalDb = eq.getMagnitudeResponseDb(1000.0);
    const bool ok = slot == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    bandDb > 8.0 &&
                    totalDb > 8.0;

    return report("Dynamic band contributes to magnitude response", ok);
}

bool testDynamicBandProcessesAudio() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);

    const float freqNorm = static_cast<float>(std::log10(1000.0 / 20.0) / 3.0);
    const int32_t slot = eq.createDynamicBandAtGraphPoint(freqNorm, 0.833f); // approximately +12 dB
    settleSmoothing(eq);

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.3f);

    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    const float rmsIn = calculateRMS(input.data(), frames);
    const float rmsOut = calculateRMS(output.data(), frames);
    const float boostDb = 20.0f * std::log10(rmsOut / std::max(rmsIn, 1e-12f) + 1e-12f);
    const bool ok = slot == static_cast<int32_t>(AestraEQ::kLegacyBandCount) && boostDb > 8.0f;

    return report("Dynamic band processes audio", ok);
}

bool testDynamicBandDetectorDrivesTargetGain() {
    AestraEQ staticEq;
    staticEq.initialize(kSampleRate, kBlockSize);
    staticEq.activate();
    AestraEQ dynamicEq;
    dynamicEq.initialize(kSampleRate, kBlockSize);
    dynamicEq.activate();

    for (AestraEQ* eq : {&staticEq, &dynamicEq}) {
        eq->setParameter(AestraEQ::kParamHPFEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamLShEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamBell1Enable, 0.0f);
        eq->setParameter(AestraEQ::kParamBell2Enable, 0.0f);
        eq->setParameter(AestraEQ::kParamHShEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamLPFEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamBypass, 0.0f);
    }

    auto defaults = AestraEQ::DynamicBandSlotDefaults{};
    defaults.enabled = true;
    defaults.type = FilterType::Bell;
    defaults.frequencyNorm = graphNormForHz(1000.0f);
    defaults.gainNorm = 0.5f;
    defaults.qOrSlopeNorm = 0.35f;
    defaults.dynamicEnabled = true;
    defaults.targetGainNorm = 0.95f;
    defaults.thresholdNorm = 0.0f;
    defaults.kneeNorm = 0.02f;
    defaults.attackNorm = 0.0f;
    defaults.releaseNorm = 0.0f;

    const int32_t dynamicSlot = dynamicEq.createDynamicBandSlot(defaults);
    defaults.dynamicEnabled = false;
    const int32_t staticSlot = staticEq.createDynamicBandSlot(defaults);
    settleSmoothing(staticEq);
    settleSmoothing(dynamicEq);

    const uint32_t frames = kBlockSize * 32;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> staticOut(frames, 0.0f);
    std::vector<float> dynamicOut(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.35f);

    processBlocks(staticEq, input.data(), staticOut.data(), frames, kBlockSize);
    processBlocks(dynamicEq, input.data(), dynamicOut.data(), frames, kBlockSize);

    const uint32_t tailOffset = frames / 2;
    const float staticRms = calculateRMS(staticOut.data() + tailOffset, frames - tailOffset);
    const float dynamicRms = calculateRMS(dynamicOut.data() + tailOffset, frames - tailOffset);
    const float envelopeAmount = dynamicEq.getDynamicBandEnvelopeAmount(static_cast<uint32_t>(dynamicSlot));
    const bool ok = staticSlot == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    dynamicSlot == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    envelopeAmount > 0.40f &&
                    dynamicRms > staticRms * 1.8f;

    return report("Dynamic band detector drives target gain", ok);
}

bool testDynamicBandUnlinkedDetectorCanTriggerDifferentRange() {
    AestraEQ linkedEq;
    linkedEq.initialize(kSampleRate, kBlockSize);
    linkedEq.activate();
    AestraEQ unlinkedEq;
    unlinkedEq.initialize(kSampleRate, kBlockSize);
    unlinkedEq.activate();

    for (AestraEQ* eq : {&linkedEq, &unlinkedEq}) {
        eq->setParameter(AestraEQ::kParamHPFEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamLShEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamBell1Enable, 0.0f);
        eq->setParameter(AestraEQ::kParamBell2Enable, 0.0f);
        eq->setParameter(AestraEQ::kParamHShEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamLPFEnable, 0.0f);
        eq->setParameter(AestraEQ::kParamBypass, 0.0f);
    }

    auto defaults = AestraEQ::DynamicBandSlotDefaults{};
    defaults.enabled = true;
    defaults.type = FilterType::Bell;
    defaults.frequencyNorm = graphNormForHz(1000.0f);
    defaults.gainNorm = 0.5f;
    defaults.qOrSlopeNorm = 0.40f;
    defaults.dynamicEnabled = true;
    defaults.targetGainNorm = 0.95f;
    defaults.thresholdNorm = 0.60f;
    defaults.kneeNorm = 0.02f;
    defaults.attackNorm = 0.0f;
    defaults.releaseNorm = 0.0f;
    defaults.sidechainLinked = true;
    const int32_t linkedSlot = linkedEq.createDynamicBandSlot(defaults);

    defaults.sidechainLinked = false;
    defaults.sidechainType = FilterType::BandPass;
    defaults.sidechainFrequencyNorm = graphNormForHz(120.0f);
    defaults.sidechainQNorm = 0.45f;
    const int32_t unlinkedSlot = unlinkedEq.createDynamicBandSlot(defaults);

    settleSmoothing(linkedEq);
    settleSmoothing(unlinkedEq);

    const uint32_t frames = kBlockSize * 48;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> linkedOut(frames, 0.0f);
    std::vector<float> unlinkedOut(frames, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        input[i] = static_cast<float>(0.45 * std::sin(kTau * 120.0 * t) +
                                      0.02 * std::sin(kTau * 1000.0 * t));
    }

    processBlocks(linkedEq, input.data(), linkedOut.data(), frames, kBlockSize);
    processBlocks(unlinkedEq, input.data(), unlinkedOut.data(), frames, kBlockSize);

    const uint32_t tailOffset = frames / 2;
    const uint32_t tailFrames = frames - tailOffset;
    const float linkedTone = calculateToneAmplitude(linkedOut.data() + tailOffset, tailFrames, 1000.0);
    const float unlinkedTone = calculateToneAmplitude(unlinkedOut.data() + tailOffset, tailFrames, 1000.0);
    const bool ok = linkedSlot == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    unlinkedSlot == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    unlinkedTone > linkedTone * 2.0f;

    return report("Dynamic band unlinked detector can trigger different range", ok);
}

bool testDisabledDynamicBandsDoNotAlterOutput() {
    AestraEQ baseline;
    baseline.initialize(kSampleRate, kBlockSize);
    baseline.activate();
    baseline.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    baseline.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    baseline.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    baseline.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    baseline.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    baseline.setParameter(AestraEQ::kParamLPFEnable, 0.0f);

    AestraEQ withDisabled;
    withDisabled.initialize(kSampleRate, kBlockSize);
    withDisabled.activate();
    withDisabled.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    withDisabled.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    withDisabled.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    withDisabled.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    withDisabled.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    withDisabled.setParameter(AestraEQ::kParamLPFEnable, 0.0f);

    for (uint32_t slot = AestraEQ::kLegacyBandCount; slot < AestraEQ::kMaxDynamicBands; ++slot) {
        const bool set = withDisabled.setDynamicBandSlot(slot, {
            false,
            slot % 3u == 0u ? FilterType::Tilt : (slot % 3u == 1u ? FilterType::Notch : FilterType::Bell),
            slot % 2u == 0u ? AestraEQ::StereoMode::Mid : AestraEQ::StereoMode::Side,
            static_cast<float>(slot - AestraEQ::kLegacyBandCount) /
                static_cast<float>(AestraEQ::kMaxDynamicBands - AestraEQ::kLegacyBandCount - 1u),
            slot % 2u == 0u ? 1.0f : 0.0f,
            slot % 2u == 0u ? 1.0f : 0.0f,
            false,
        });
        if (!set) {
            return report("Disabled dynamic bands do not alter output", false);
        }
    }
    settleSmoothing(baseline, 50);
    settleSmoothing(withDisabled, 50);

    const uint32_t frames = kBlockSize * 3;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> baselineOut(frames, 0.0f);
    std::vector<float> disabledOut(frames, 0.0f);
    generateTone(input.data(), frames, 440.0, 0.18f);
    for (uint32_t i = 0; i < frames; ++i) {
        input[i] += static_cast<float>(std::sin(kTau * 2700.0 * static_cast<double>(i) / kSampleRate) * 0.08);
    }

    processBlocks(baseline, input.data(), baselineOut.data(), frames, kBlockSize);
    processBlocks(withDisabled, input.data(), disabledOut.data(), frames, kBlockSize);

    const bool ok = !hasNaNOrInf(disabledOut.data(), frames) &&
                    maxAbsDiff(baselineOut.data(), disabledOut.data(), frames) < 1.0e-5f;
    return report("Disabled dynamic bands do not alter output", ok);
}

bool testAllTwentyFourBandsActiveRemainFinite() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, 0.15f);
    eq.setParameter(AestraEQ::kParamHPFSlope, 2.0f / 6.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLShGain, 0.92f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.96f);
    eq.setParameter(AestraEQ::kParamBell1Q, 0.72f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Gain, 0.04f);
    eq.setParameter(AestraEQ::kParamBell2Q, 0.86f);
    eq.setParameter(AestraEQ::kParamHShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHShGain, 0.88f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLPFFreq, 0.98f);
    eq.setParameter(AestraEQ::kParamLPFSlope, 2.0f / 6.0f);

    for (uint32_t slot = AestraEQ::kLegacyBandCount; slot < AestraEQ::kMaxDynamicBands; ++slot) {
        const float t = static_cast<float>(slot - AestraEQ::kLegacyBandCount) /
                        static_cast<float>(AestraEQ::kMaxDynamicBands - AestraEQ::kLegacyBandCount - 1u);
        const bool set = eq.setDynamicBandSlot(slot, {
            true,
            slot % 5u == 0u ? FilterType::Tilt : (slot % 5u == 1u ? FilterType::Notch : FilterType::Bell),
            static_cast<AestraEQ::StereoMode>(slot % 5u),
            t,
            slot % 2u == 0u ? 0.92f : 0.08f,
            0.84f,
            false,
        });
        if (!set) {
            return report("All 24 active bands remain finite", false);
        }
    }
    settleSmoothing(eq, 80);

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> silence(frames, 0.0f);
    std::vector<float> silenceOut(frames, 1.0f);
    processBlocks(eq, silence.data(), silenceOut.data(), frames, kBlockSize);

    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        input[i] = static_cast<float>(0.08 * std::sin(kTau * 110.0 * t) +
                                      0.05 * std::sin(kTau * 1000.0 * t) +
                                      0.03 * std::sin(kTau * 6700.0 * t));
    }
    processBlocks(eq, input.data(), output.data(), frames, kBlockSize);

    const bool ok = !hasNaNOrInf(silenceOut.data(), frames) &&
                    maxAbsValue(silenceOut.data(), frames) < 1.0e-6f &&
                    !hasNaNOrInf(output.data(), frames) &&
                    maxAbsValue(output.data(), frames) < 1.0e5f;
    return report("All 24 active bands remain finite", ok);
}

bool testDynamicBandStateRoundTrip() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.setParameter(AestraEQ::kParamOutputGain, 0.75f);

    const float freqNorm = static_cast<float>(std::log10(1000.0 / 20.0) / 3.0);
    const int32_t slotA = eq.createDynamicBandAtGraphPoint(freqNorm, 0.833f);
    auto dynamicDefaults = AestraEQ::DynamicBandSlotDefaults{
        true,
        FilterType::Notch,
        AestraEQ::StereoMode::Mid,
        0.62f,
        0.40f,
        0.35f,
        false,
    };
    dynamicDefaults.dynamicEnabled = true;
    dynamicDefaults.targetGainNorm = 0.78f;
    dynamicDefaults.thresholdNorm = 0.42f;
    dynamicDefaults.kneeNorm = 0.24f;
    dynamicDefaults.attackNorm = 0.16f;
    dynamicDefaults.releaseNorm = 0.58f;
    dynamicDefaults.sidechainLinked = false;
    dynamicDefaults.sidechainType = FilterType::BandPass;
    dynamicDefaults.sidechainFrequencyNorm = 0.31f;
    dynamicDefaults.sidechainQNorm = 0.44f;
    const int32_t slotB = eq.createDynamicBandSlot(dynamicDefaults, static_cast<uint32_t>(slotA));

    const auto state = eq.saveState();
    AestraEQ restored;
    restored.initialize(kSampleRate, kBlockSize);
    const bool loaded = restored.loadState(state);

    const auto a = restored.getDynamicBandSlotSnapshot(static_cast<uint32_t>(slotA));
    const auto b = restored.getDynamicBandSlotSnapshot(static_cast<uint32_t>(slotB));
    const bool ok = loaded &&
                    state.size() == sizeof(EQStateBlobV8) &&
                    std::abs(restored.getParameter(AestraEQ::kParamOutputGain) - 0.75f) < 0.001f &&
                    slotA == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    slotB == static_cast<int32_t>(AestraEQ::kLegacyBandCount + 1) &&
                    a.enabled &&
                    a.type == FilterType::Bell &&
                    std::abs(a.frequencyNorm - freqNorm) < 0.001f &&
                    std::abs(a.gainNorm - 0.833f) < 0.001f &&
                    b.enabled &&
                    b.type == FilterType::Notch &&
                    b.stereoMode == AestraEQ::StereoMode::Mid &&
                    std::abs(b.frequencyNorm - 0.62f) < 0.001f &&
                    std::abs(b.qOrSlopeNorm - 0.35f) < 0.001f &&
                    b.dynamicEnabled &&
                    std::abs(b.targetGainNorm - 0.78f) < 0.001f &&
                    std::abs(b.thresholdNorm - 0.42f) < 0.001f &&
                    std::abs(b.kneeNorm - 0.24f) < 0.001f &&
                    std::abs(b.attackNorm - 0.16f) < 0.001f &&
                    std::abs(b.releaseNorm - 0.58f) < 0.001f &&
                    !b.sidechainLinked &&
                    b.sidechainType == FilterType::BandPass &&
                    std::abs(b.sidechainFrequencyNorm - 0.31f) < 0.001f &&
                    std::abs(b.sidechainQNorm - 0.44f) < 0.001f &&
                    restored.getMagnitudeResponseDb(1000.0) > 8.0;

    return report("Dynamic band state round-trips in V8", ok);
}

bool testLegacyV7DynamicBandStateLoadsWithDynamicDefaults() {
    EQStateBlobV7 blob{};
    blob.magic = AestraEQ::kStateMagicV7;
    blob.version = 7;
    for (uint32_t i = 0; i < AestraEQ::kParamCount; ++i) {
        blob.params[i] = AestraEQ::defaultParameterValue(i);
    }
    auto& saved = blob.dynamicBands[AestraEQ::kLegacyBandCount];
    saved.enabled = 1u;
    saved.typeNorm = 0.25f;
    saved.stereoNorm = 0.75f;
    saved.frequencyNorm = graphNormForHz(900.0f);
    saved.gainNorm = 0.70f;
    saved.qOrSlopeNorm = 0.33f;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
    const std::vector<uint8_t> state(bytes, bytes + sizeof(blob));

    AestraEQ restored;
    restored.initialize(kSampleRate, kBlockSize);
    const bool loaded = restored.loadState(state);
    const auto snapshot = restored.getDynamicBandSlotSnapshot(AestraEQ::kLegacyBandCount);
    const bool ok = loaded &&
                    snapshot.enabled &&
                    !snapshot.dynamicEnabled &&
                    snapshot.sidechainLinked &&
                    std::abs(snapshot.sidechainFrequencyNorm - snapshot.frequencyNorm) < 0.001f &&
                    std::abs(snapshot.sidechainQNorm - snapshot.qOrSlopeNorm) < 0.001f;

    return report("Legacy V7 dynamic band state loads with V8 dynamic defaults", ok);
}

bool testBandSoloIsRuntimeOnlyAndLimitsResponse() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    const int32_t slotA = eq.createDynamicBandSlot({
        true,
        FilterType::Bell,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(1000.0f),
        0.833f,
        0.20f,
        false,
    });
    const int32_t slotB = eq.createDynamicBandSlot({
        true,
        FilterType::HighShelf,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(1800.0f),
        0.15f,
        0.12f,
        false,
    });

    const double bandA = eq.getBandMagnitudeResponseDb(static_cast<uint32_t>(slotA), 1000.0);
    eq.setSoloBandSlot(slotA);
    const double soloA = eq.getMagnitudeResponseDb(1000.0);
    const auto state = eq.saveState();

    AestraEQ restored;
    restored.initialize(kSampleRate, kBlockSize);
    const bool loaded = restored.loadState(state);
    const bool soloLimitsResponse = slotA >= 0 && slotB >= 0 && std::abs(soloA - bandA) < 0.02;
    const bool soloNotSerialized = loaded && restored.getSoloBandSlot() == -1;

    return report("Band solo is runtime-only and limits response", soloLimitsResponse && soloNotSerialized);
}

bool testBandSoloAuditionsAffectedFrequencyRegion() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    const int32_t slot = eq.createDynamicBandSlot({
        true,
        FilterType::Bell,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(1000.0f),
        0.5f, // 0 dB, so normal EQ processing would be transparent.
        0.55f,
        false,
    });
    if (slot < 0) {
        return report("Band solo auditions affected frequency region", false);
    }
    eq.setSoloBandSlot(slot);

    const uint32_t frames = AestraEQ::kAnalyzerWindowSize * 2;
    std::vector<float> passInput(frames, 0.0f);
    std::vector<float> rejectInput(frames, 0.0f);
    std::vector<float> passOutput(frames, 0.0f);
    std::vector<float> rejectOutput(frames, 0.0f);
    generateTone(passInput.data(), frames, 1000.0, 0.4f);
    generateTone(rejectInput.data(), frames, 120.0, 0.4f);
    processBlocks(eq, passInput.data(), passOutput.data(), frames, kBlockSize);
    processBlocks(eq, rejectInput.data(), rejectOutput.data(), frames, kBlockSize);

    const float passRms = calculateRMS(passOutput.data(), frames);
    const float rejectRms = calculateRMS(rejectOutput.data(), frames);
    const bool isolated = passRms > 0.08f && rejectRms < passRms * 0.45f;
    return report("Band solo auditions affected frequency region", isolated);
}

bool testBandSoloUnsoloRebuildsWithoutParameterNudge() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    const int32_t slot = eq.createDynamicBandSlot({
        true,
        FilterType::Bell,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(1000.0f),
        0.5f, // 0 dB normal EQ should pass through after unsolo.
        0.55f,
        false,
    });
    if (slot < 0) {
        return report("Band solo/unsolo rebuilds without parameter nudge", false);
    }

    const uint32_t frames = AestraEQ::kAnalyzerWindowSize * 2;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> soloOut(frames, 0.0f);
    std::vector<float> unsoloOut(frames, 0.0f);
    generateTone(input.data(), frames, 120.0, 0.4f);

    eq.setSoloBandSlot(slot);
    processBlocks(eq, input.data(), soloOut.data(), frames, kBlockSize);
    eq.setSoloBandSlot(-1);
    processBlocks(eq, input.data(), unsoloOut.data(), frames, kBlockSize);

    const float inputRms = calculateRMS(input.data(), frames);
    const float soloRms = calculateRMS(soloOut.data(), frames);
    const float unsoloRms = calculateRMS(unsoloOut.data(), frames);
    const bool rebuilt = soloRms < inputRms * 0.45f && unsoloRms > inputRms * 0.92f;
    return report("Band solo/unsolo rebuilds without parameter nudge", rebuilt);
}

bool testClearingSoloedDynamicBandClearsSoloState() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    const int32_t slot = eq.createDynamicBandAtGraphPoint(graphNormForHz(700.0f), 0.75f);
    eq.setSoloBandSlot(slot);
    const bool cleared = eq.clearDynamicBandSlot(static_cast<uint32_t>(slot));

    return report("Clearing soloed dynamic band clears solo state",
                  slot >= 0 && cleared && eq.getSoloBandSlot() == -1);
}

bool testDisablingSoloedDynamicBandClearsSoloState() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    const int32_t slot = eq.createDynamicBandAtGraphPoint(graphNormForHz(900.0f), 0.80f);
    const bool disabled = eq.setDynamicBandSlot(static_cast<uint32_t>(slot), {
        false,
        FilterType::Bell,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(900.0f),
        0.80f,
        0.20f,
        false,
    });
    eq.setSoloBandSlot(slot);
    const bool disabledAgain = eq.setDynamicBandSlot(static_cast<uint32_t>(slot), {
        false,
        FilterType::Bell,
        AestraEQ::StereoMode::Stereo,
        graphNormForHz(900.0f),
        0.80f,
        0.20f,
        false,
    });

    return report("Disabling soloed dynamic band clears solo state",
                  slot >= 0 && disabled && disabledAgain && eq.getSoloBandSlot() == -1);
}

bool testAllDynamicBandStateRoundTrip() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);

    bool created = true;
    static constexpr FilterType kDynamicRoundTripTypes[] = {
        FilterType::LowCut,
        FilterType::LowShelf,
        FilterType::Bell,
        FilterType::Notch,
        FilterType::BandPass,
        FilterType::Tilt,
        FilterType::HighShelf,
        FilterType::HighCut,
    };
    for (uint32_t slot = AestraEQ::kLegacyBandCount; slot < AestraEQ::kMaxDynamicBands; ++slot) {
        const float t = static_cast<float>(slot - AestraEQ::kLegacyBandCount) /
                        static_cast<float>(AestraEQ::kMaxDynamicBands - AestraEQ::kLegacyBandCount - 1u);
        const auto type =
            kDynamicRoundTripTypes[slot % (sizeof(kDynamicRoundTripTypes) / sizeof(kDynamicRoundTripTypes[0]))];
        const auto mode = static_cast<AestraEQ::StereoMode>(slot % 5u);
        created &= eq.setDynamicBandSlot(slot, {
            true,
            type,
            mode,
            std::clamp(0.06f + t * 0.88f, 0.0f, 1.0f),
            std::clamp(0.15f + t * 0.70f, 0.0f, 1.0f),
            std::clamp(0.20f + t * 0.55f, 0.0f, 1.0f),
            false,
        });
    }

    const auto state = eq.saveState();
    AestraEQ restored;
    restored.initialize(kSampleRate, kBlockSize);
    const bool loaded = restored.loadState(state);

    bool slotsOk = created && loaded && state.size() == sizeof(EQStateBlobV8);
    for (uint32_t slot = AestraEQ::kLegacyBandCount; slot < AestraEQ::kMaxDynamicBands; ++slot) {
        const auto expected = eq.getDynamicBandSlotSnapshot(slot);
        const auto actual = restored.getDynamicBandSlotSnapshot(slot);
        slotsOk &= actual.slotIndex == slot;
        slotsOk &= !actual.legacySlot;
        slotsOk &= actual.enabled == expected.enabled;
        slotsOk &= actual.type == expected.type;
        slotsOk &= actual.stereoMode == expected.stereoMode;
        slotsOk &= std::abs(actual.frequencyNorm - expected.frequencyNorm) < 0.001f;
        slotsOk &= std::abs(actual.gainNorm - expected.gainNorm) < 0.001f;
        slotsOk &= std::abs(actual.qOrSlopeNorm - expected.qOrSlopeNorm) < 0.001f;
        slotsOk &= actual.usesSlope == expected.usesSlope;
        slotsOk &= actual.dynamicEnabled == expected.dynamicEnabled;
        slotsOk &= std::abs(actual.targetGainNorm - expected.targetGainNorm) < 0.001f;
        slotsOk &= actual.sidechainLinked == expected.sidechainLinked;
    }

    return report("All dynamic band state round-trips in V8", slotsOk);
}

bool testLegacyV6StateClearsDynamicSlots() {
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    const int32_t slot = eq.createDynamicBandAtGraphPoint(0.4f, 0.8f);

    EQStateBlobV6 legacy{};
    legacy.magic = AestraEQ::kStateMagicV6;
    legacy.version = 6;
    for (uint32_t i = 0; i < AestraEQ::kParamCount; ++i) {
        legacy.params[i] = AestraEQ::defaultParameterValue(i);
    }

    const bool loaded = eq.loadState(toBytes(legacy));
    const auto snapshot = eq.getDynamicBandSlotSnapshot(static_cast<uint32_t>(slot));
    const bool ok = slot == static_cast<int32_t>(AestraEQ::kLegacyBandCount) &&
                    loaded &&
                    !snapshot.enabled &&
                    eq.findNextAvailableDynamicBandSlot() == slot;

    return report("Legacy V6 state clears dynamic slots", ok);
}

bool testLatencyZero() {
    AestraEQ eq;
    return report("Latency is 0 samples", eq.getLatencySamples() == 0);
}

} // namespace

int main() {
    std::cout << "=== Aestra EQ V1 Tests ===\n\n";

    testDirectConstructionSafeDefaults();
    testParameterDescriptorCount();
    testParameterDescriptorTypeExposedForMiddleBands();
    testNeutralGainDisplaysCleanZero();
    testPositiveGainDisplaysPlusPrefix();
    testHasEditorTrue();
    testParameterCount();
    testLegacyBandSlotMetadata();
    testDynamicBandSlotsDefaultInactive();
    testDynamicBandSlotAllocatorSkipsLegacySlots();
    testDynamicBandSlotDefaultsAreNeutralForFutureSlots();
    testDefaultStateStartsWithNoBandsEnabled();
    testDynamicBandSlotSnapshotReportsLegacyAndFutureSlots();
    testDynamicBandGraphCreationDefaultsClampClickedPoint();
    testResetToEmptyStateClearsAllBands();
    testDynamicBandGraphClickAllocatesNextAvailableSlot();
    testDynamicBandsAllowMultipleSameFilterType();
    testDynamicBandSlotClearReopensSlotForGraphClick();
    testDynamicBandGraphClickReportsFullCapacity();
    testDynamicBandContributesToMagnitudeResponse();
    testDynamicBandProcessesAudio();
    testDynamicBandDetectorDrivesTargetGain();
    testDynamicBandUnlinkedDetectorCanTriggerDifferentRange();
    testDisabledDynamicBandsDoNotAlterOutput();
    testAllTwentyFourBandsActiveRemainFinite();
    testDynamicBandStateRoundTrip();
    testLegacyV7DynamicBandStateLoadsWithDynamicDefaults();
    testBandSoloIsRuntimeOnlyAndLimitsResponse();
    testBandSoloAuditionsAffectedFrequencyRegion();
    testBandSoloUnsoloRebuildsWithoutParameterNudge();
    testClearingSoloedDynamicBandClearsSoloState();
    testDisablingSoloedDynamicBandClearsSoloState();
    testAllDynamicBandStateRoundTrip();
    testLegacyV6StateClearsDynamicSlots();
    testLatencyZero();
    testBypassParity();
    testActiveMultichannelPassthroughAboveStereo();
    testFlatEQEqualsInput();
    testHPFAttenuatesLowFreq();
    testLPFAttenuatesHighFreq();
    testLowShelfBoostsBass();
    testHighShelfBoostsTreble();
    testBellBoost();
    testBellCut();
    testExtremeValuesNoNaN();
    testSampleRateInit44100();
    testSampleRateInit96000();
    testStateV6Roundtrip();
    testLegacyV5StateLoadDefaultsStereoPlacement();
    testLegacyV4StateLoadDefaultsPolarity();
    testLegacyV3StateLoadDefaultsOutputGain();
    testLegacyV2StateLoadDefaultsNewTypes();
    testLegacyV1StateLoad();
    testCorruptStateFails();
    testShortStateFails();
    testNaNInputRecovers();
    testSilenceStability();
    testSmoothingNoDiscontinuity();
    testAnalyzerPublishesDistinctPreAndPostWindows();
    testAnalyzerPublishesStereoPlacementWindows();
    testBandStereoPlacementProcessesOnlySelectedSide();
    testBandStereoPlacementProcessesRightSide();
    testBandStereoPlacementProcessesMidSignal();
    testBandStereoPlacementProcessesSideSignal();
    testDynamicBandStereoPlacementProcessesLeftAndRight();
    testDynamicBandStereoPlacementProcessesMidAndSide();

    std::cout << "\n";
    std::cout << g_testsPassed << " passed, " << g_testsFailed << " failed.\n";
    return g_testsFailed > 0 ? 1 : 0;
}
