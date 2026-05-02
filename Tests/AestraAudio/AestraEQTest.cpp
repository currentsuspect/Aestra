// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraEQTest — V1 contract tests for the 6-band parametric EQ.

#include "Plugin/AestraEQ.h"
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

float maxAbsDiff(const float* a, const float* b, uint32_t frames) {
    float maxDiff = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float diff = std::abs(a[i] - b[i]);
        if (diff > maxDiff) maxDiff = diff;
    }
    return maxDiff;
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
    return report("V1 descriptor count is 23",
        params.size() == AestraEQ::kV1ParamCount);
}

bool testParameterDescriptorNoTypeExposed() {
    AestraEQ eq;
    auto params = eq.getParameters();
    for (const auto& p : params) {
        if (p.name.find("Type") != std::string::npos) {
            return report("No type params in V1 descriptors", false);
        }
    }
    return report("No type params in V1 descriptors", true);
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
    std::vector<float> dummy(frames, 0.0f);
    for (int i = 0; i < 200; ++i) {
        const float* inPtr = dummy.data();
        float* outPtr = dummy.data();
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
    eq.setParameter(AestraEQ::kParamLPFSlope, 1.0f); // 48 dB/oct
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
    eq.setParameter(AestraEQ::kParamHPFSlope, 1.0f); // 48 dB/oct
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
    eq.setParameter(AestraEQ::kParamLPFSlope, 1.0f); // 48 dB/oct
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

bool testStateV2Roundtrip() {
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

    return report("State V2 roundtrip", loaded && match);
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
    v1.params[4] = 0.5f; // q

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

bool testParameterCount() {
    AestraEQ eq;
    return report("getParameterCount returns 23", eq.getParameterCount() == AestraEQ::kV1ParamCount);
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
    testParameterDescriptorNoTypeExposed();
    testHasEditorTrue();
    testParameterCount();
    testLatencyZero();
    testBypassParity();
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
    testStateV2Roundtrip();
    testLegacyV1StateLoad();
    testCorruptStateFails();
    testShortStateFails();
    testNaNInputRecovers();
    testSilenceStability();
    testSmoothingNoDiscontinuity();

    std::cout << "\n";
    std::cout << g_testsPassed << " passed, " << g_testsFailed << " failed.\n";
    return g_testsFailed > 0 ? 1 : 0;
}
