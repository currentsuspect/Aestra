// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraEQMeasurementTest — black-box frequency response measurements for Aestra EQ.

#include "Plugin/AestraEQ.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace Aestra::Audio::Plugins;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 512;
constexpr double kTau = 6.28318530717958647692;

int g_testsPassed = 0;
int g_testsFailed = 0;

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

double rmsWindow(const std::vector<float>& buffer, size_t start, size_t end) {
    double sum = 0.0;
    const size_t clampedEnd = std::min(end, buffer.size());
    if (start >= clampedEnd) {
        return 0.0;
    }

    for (size_t i = start; i < clampedEnd; ++i) {
        sum += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return std::sqrt(sum / static_cast<double>(clampedEnd - start));
}

bool hasNaNOrInf(const std::vector<float>& buffer) {
    return std::any_of(buffer.begin(), buffer.end(), [](float v) { return !std::isfinite(v); });
}

struct Measurement {
    double gainDb{0.0};
    double rmsIn{0.0};
    double rmsOut{0.0};
    bool finite{true};
};

Measurement measureSineGainDb(
    AestraEQ& eq,
    double frequencyHz,
    double sampleRate = kSampleRate,
    uint32_t totalFrames = 131072
) {
    std::vector<float> input(totalFrames, 0.0f);
    std::vector<float> output(totalFrames, 0.0f);

    for (uint32_t i = 0; i < totalFrames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        input[i] = static_cast<float>(std::sin(kTau * frequencyHz * t) * 0.125);
    }

    for (uint32_t offset = 0; offset < totalFrames; offset += kBlockSize) {
        const uint32_t frames = std::min<uint32_t>(kBlockSize, totalFrames - offset);
        const float* inPtr = input.data() + offset;
        float* outPtr = output.data() + offset;
        eq.process(&inPtr, &outPtr, 1, 1, frames);
    }

    const size_t analysisStart = totalFrames / 2;
    const double rmsIn = rmsWindow(input, analysisStart, totalFrames);
    const double rmsOut = rmsWindow(output, analysisStart, totalFrames);
    const double ratio = rmsOut / std::max(rmsIn, 1.0e-12);
    const double gainDb = 20.0 * std::log10(std::max(ratio, 1.0e-12));

    return {gainDb, rmsIn, rmsOut, !hasNaNOrInf(output)};
}

void processSine(AestraEQ& eq, double frequencyHz, uint32_t totalFrames = 32768) {
    std::vector<float> input(kBlockSize, 0.0f);
    std::vector<float> output(kBlockSize, 0.0f);
    uint32_t processed = 0;
    while (processed < totalFrames) {
        const uint32_t frames = std::min<uint32_t>(kBlockSize, totalFrames - processed);
        for (uint32_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(processed + i) / kSampleRate;
            input[i] = static_cast<float>(std::sin(kTau * frequencyHz * t) * 0.25);
        }
        const float* inPtr = input.data();
        float* outPtr = output.data();
        eq.process(&inPtr, &outPtr, 1, 1, frames);
        processed += frames;
    }
}

double processSilencePeak(AestraEQ& eq) {
    std::vector<float> input(kBlockSize, 0.0f);
    std::vector<float> output(kBlockSize, 0.0f);
    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, kBlockSize);

    double peak = 0.0;
    for (float sample : output) {
        peak = std::max(peak, std::abs(static_cast<double>(sample)));
    }
    return peak;
}

void settleSmoothing(AestraEQ& eq, uint32_t numBlocks = 500) {
    std::vector<float> input(kBlockSize, 0.0f);
    std::vector<float> output(kBlockSize, 0.0f);
    for (uint32_t i = 0; i < numBlocks; ++i) {
        const float* inPtr = input.data();
        float* outPtr = output.data();
        eq.process(&inPtr, &outPtr, 1, 1, kBlockSize);
    }
}

void initializeActiveEQ(AestraEQ& eq) {
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
}

bool nearDb(double measured, double expected, double tolerance) {
    return std::abs(measured - expected) <= tolerance;
}

float normalizedLogFrequency(double hz, double minHz, double maxHz) {
    const double clamped = std::clamp(hz, minHz, maxHz);
    return static_cast<float>(std::log(clamped / minHz) / std::log(maxHz / minHz));
}

float normalizedGainDb(double db) {
    return static_cast<float>(std::clamp((db + 18.0) / 36.0, 0.0, 1.0));
}

float normalizedQ(double q) {
    return static_cast<float>(std::clamp((q - 0.1) / 9.9, 0.0, 1.0));
}

bool testBypassIsUnityAcrossSpectrum() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamOutputGain, normalizedGainDb(12.0));
    eq.setParameter(AestraEQ::kParamPolarityInvert, 1.0f);
    eq.setParameter(AestraEQ::kParamBypass, 1.0f);

    bool ok = true;
    for (double freq : {31.25, 1000.0, 10000.0, 19000.0}) {
        const auto m = measureSineGainDb(eq, freq);
        ok = ok && m.finite && nearDb(m.gainDb, 0.0, 0.02);
    }

    return report("Bypass measures unity and ignores output stage", ok);
}

bool testBypassSanitizesUnsafeInputOnly() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamOutputGain, normalizedGainDb(12.0));
    eq.setParameter(AestraEQ::kParamPolarityInvert, 1.0f);
    eq.setParameter(AestraEQ::kParamBypass, 1.0f);

    std::vector<float> input(kBlockSize, 0.0f);
    std::vector<float> output(kBlockSize, 99.0f);
    input[0] = 24.0f;
    input[1] = -24.0f;
    input[2] = std::numeric_limits<float>::quiet_NaN();
    input[3] = std::numeric_limits<float>::infinity();
    input[4] = -std::numeric_limits<float>::infinity();
    input[5] = std::numeric_limits<float>::denorm_min();
    input[6] = -0.125f;

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, kBlockSize);

    const bool ok = output[0] == 24.0f &&
                    output[1] == -24.0f &&
                    output[2] == 0.0f &&
                    output[3] == 0.0f &&
                    output[4] == 0.0f &&
                    output[5] == 0.0f &&
                    output[6] == -0.125f;
    return report("Bypass pass-through sanitizes unsafe input only", ok);
}

bool testOutputGainScalesActiveSignal() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamOutputGain, normalizedGainDb(6.0));
    settleSmoothing(eq);

    const auto boosted = measureSineGainDb(eq, 1000.0);

    AestraEQ cutEq;
    initializeActiveEQ(cutEq);
    cutEq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamOutputGain, normalizedGainDb(-6.0));
    settleSmoothing(cutEq);

    const auto cut = measureSineGainDb(cutEq, 1000.0);

    const bool ok = boosted.finite &&
                    cut.finite &&
                    nearDb(boosted.gainDb, 6.0, 0.15) &&
                    nearDb(cut.gainDb, -6.0, 0.15) &&
                    eq.getParameterDisplay(AestraEQ::kParamOutputGain) == "+6.0dB" &&
                    cutEq.getParameterDisplay(AestraEQ::kParamOutputGain) == "-6.0dB";
    return report("Output gain scales active signal", ok);
}

bool testPolarityInvertFlipsActiveSignal() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamOutputGain, 0.5f);
    eq.setParameter(AestraEQ::kParamPolarityInvert, 1.0f);
    settleSmoothing(eq);

    std::vector<float> input(kBlockSize, 0.0f);
    std::vector<float> output(kBlockSize, 0.0f);
    for (uint32_t i = 0; i < kBlockSize; ++i) {
        input[i] = (i % 2 == 0) ? 0.125f : -0.25f;
    }

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, kBlockSize);

    bool ok = eq.getParameterDisplay(AestraEQ::kParamPolarityInvert) == "INV";
    for (uint32_t i = 0; i < kBlockSize; ++i) {
        ok = ok && std::abs(output[i] + input[i]) < 1.0e-6f;
    }
    return report("Polarity invert flips active signal", ok);
}

bool testBellCenterGainAccuracy() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(500.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.833333f); // +12 dB
    eq.setParameter(AestraEQ::kParamBell1Q, 0.091f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq);

    const auto boost = measureSineGainDb(eq, 500.0);

    AestraEQ cutEq;
    initializeActiveEQ(cutEq);
    cutEq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    cutEq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(500.0, 80.0, 8000.0));
    cutEq.setParameter(AestraEQ::kParamBell1Gain, 0.166667f); // -12 dB
    cutEq.setParameter(AestraEQ::kParamBell1Q, 0.091f);
    cutEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    cutEq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(cutEq);

    const auto cut = measureSineGainDb(cutEq, 500.0);

    const bool ok = boost.finite && cut.finite && nearDb(boost.gainDb, 12.0, 0.75) && nearDb(cut.gainDb, -12.0, 0.75);
    return report("Bell center gain measures near requested dB", ok);
}

bool testResponseModelTracksRenderedGain() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(2500.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.777778f); // +10 dB
    eq.setParameter(AestraEQ::kParamBell1Q, 0.242424f); // ~2.5
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq, 2000);

    const auto rendered = measureSineGainDb(eq, 2500.0);
    const double modeledDb = eq.getMagnitudeResponseDb(2500.0);
    const bool ok = rendered.finite && nearDb(rendered.gainDb, modeledDb, 0.30);
    return report("Response model tracks rendered bell gain", ok);
}

bool testResponseModelTracksRenderedMultiBandCurve() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, normalizedLogFrequency(70.0, 20.0, 500.0));
    eq.setParameter(AestraEQ::kParamHPFSlope, 1.0f / 3.0f); // 24 dB/oct
    eq.setParameter(AestraEQ::kParamLShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLShFreq, normalizedLogFrequency(180.0, 40.0, 1000.0));
    eq.setParameter(AestraEQ::kParamLShGain, normalizedGainDb(4.5));
    eq.setParameter(AestraEQ::kParamLShQ, normalizedQ(0.8));
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(750.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, normalizedGainDb(-5.5));
    eq.setParameter(AestraEQ::kParamBell1Q, normalizedQ(1.4));
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Freq, normalizedLogFrequency(3400.0, 200.0, 16000.0));
    eq.setParameter(AestraEQ::kParamBell2Gain, normalizedGainDb(3.0));
    eq.setParameter(AestraEQ::kParamBell2Q, normalizedQ(2.2));
    eq.setParameter(AestraEQ::kParamHShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHShFreq, normalizedLogFrequency(9000.0, 2000.0, 20000.0));
    eq.setParameter(AestraEQ::kParamHShGain, normalizedGainDb(2.5));
    eq.setParameter(AestraEQ::kParamHShQ, normalizedQ(0.7));
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq, 2000);

    bool ok = true;
    for (double freq : {45.0, 120.0, 750.0, 3400.0, 9000.0, 14000.0}) {
        const auto rendered = measureSineGainDb(eq, freq);
        const double modeledDb = eq.getMagnitudeResponseDb(freq);
        const bool freqOk = rendered.finite && nearDb(rendered.gainDb, modeledDb, 0.85);
        if (!freqOk) {
            std::cerr << "    multi-band mismatch freq=" << freq
                      << " rendered=" << rendered.gainDb
                      << " model=" << modeledDb << "\n";
        }
        ok = ok && freqOk;
    }

    return report("Response model tracks rendered multi-band curve", ok);
}

bool testBandResponseCurvesSumToTotalResponse() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, normalizedLogFrequency(70.0, 20.0, 500.0));
    eq.setParameter(AestraEQ::kParamHPFSlope, 1.0f / 3.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLShFreq, normalizedLogFrequency(180.0, 40.0, 1000.0));
    eq.setParameter(AestraEQ::kParamLShGain, normalizedGainDb(-3.0));
    eq.setParameter(AestraEQ::kParamLShQ, normalizedQ(0.75));
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(760.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, normalizedGainDb(4.0));
    eq.setParameter(AestraEQ::kParamBell1Q, normalizedQ(1.35));
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Type, 1.0f / 3.0f);
    eq.setParameter(AestraEQ::kParamBell2Freq, normalizedLogFrequency(3100.0, 200.0, 16000.0));
    eq.setParameter(AestraEQ::kParamBell2Q, normalizedQ(6.0));
    eq.setParameter(AestraEQ::kParamHShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHShFreq, normalizedLogFrequency(9000.0, 2000.0, 20000.0));
    eq.setParameter(AestraEQ::kParamHShGain, normalizedGainDb(2.0));
    eq.setParameter(AestraEQ::kParamHShQ, normalizedQ(0.7));
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);

    bool ok = true;
    for (double freq : {35.0, 100.0, 760.0, 3100.0, 9000.0, 16000.0}) {
        double sum = 0.0;
        for (uint32_t band = 0; band < AestraEQ::kV1BandCount; ++band) {
            sum += eq.getBandMagnitudeResponseDb(band, freq);
        }
        const double total = eq.getMagnitudeResponseDb(freq);
        const bool freqOk = nearDb(sum, total, 1.0e-6);
        if (!freqOk) {
            std::cerr << "    band response sum mismatch freq=" << freq
                      << " sum=" << sum
                      << " total=" << total << "\n";
        }
        ok = ok && freqOk;
    }

    return report("Per-band response curves sum to total response", ok);
}

bool testMiddleBandFilterTypesRender() {
    AestraEQ notchEq;
    initializeActiveEQ(notchEq);
    notchEq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    notchEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    notchEq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    notchEq.setParameter(AestraEQ::kParamBell1Type, 1.0f / 3.0f); // Notch
    notchEq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(1000.0, 80.0, 8000.0));
    notchEq.setParameter(AestraEQ::kParamBell1Q, 0.2f);
    notchEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    notchEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    notchEq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(notchEq);

    const auto notchCenter = measureSineGainDb(notchEq, 1000.0);

    AestraEQ bandPassEq;
    initializeActiveEQ(bandPassEq);
    bandPassEq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    bandPassEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    bandPassEq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    bandPassEq.setParameter(AestraEQ::kParamBell1Type, 2.0f / 3.0f); // Band Pass
    bandPassEq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(1000.0, 80.0, 8000.0));
    bandPassEq.setParameter(AestraEQ::kParamBell1Q, 0.2f);
    bandPassEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    bandPassEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    bandPassEq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(bandPassEq);

    const auto bandPassCenter = measureSineGainDb(bandPassEq, 1000.0);
    const auto bandPassLow = measureSineGainDb(bandPassEq, 100.0);

    const bool ok = notchCenter.finite &&
                    bandPassCenter.finite &&
                    bandPassLow.finite &&
                    notchCenter.gainDb < -24.0 &&
                    nearDb(bandPassCenter.gainDb, 0.0, 0.75) &&
                    bandPassLow.gainDb < -12.0;
    return report("Middle-band Notch and Band Pass modes render correctly", ok);
}

bool testTiltModeRendersAsTwoSidedTilt() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Type, 1.0f); // Tilt
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(1000.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, normalizedGainDb(12.0));
    eq.setParameter(AestraEQ::kParamBell1Q, normalizedQ(0.7));
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(eq);

    const auto low = measureSineGainDb(eq, 100.0);
    const auto pivot = measureSineGainDb(eq, 1000.0);
    const auto high = measureSineGainDb(eq, 10000.0);
    const double lowModel = eq.getMagnitudeResponseDb(100.0);
    const double pivotModel = eq.getMagnitudeResponseDb(1000.0);
    const double highModel = eq.getMagnitudeResponseDb(10000.0);

    const bool ok = low.finite &&
                    pivot.finite &&
                    high.finite &&
                    low.gainDb < -4.0 &&
                    std::abs(pivot.gainDb) < 1.0 &&
                    high.gainDb > 4.0 &&
                    (high.gainDb - low.gainDb) > 9.0 &&
                    nearDb(low.gainDb, lowModel, 0.45) &&
                    nearDb(pivot.gainDb, pivotModel, 0.45) &&
                    nearDb(high.gainDb, highModel, 0.45);
    return report("Tilt mode renders as two-sided low/high tilt", ok);
}

bool testCutSlopesAreMeaningful() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, 0.392f); // 80 Hz
    eq.setParameter(AestraEQ::kParamHPFSlope, 0.333f); // 24 dB/oct
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(eq);

    const auto hpf40 = measureSineGainDb(eq, 40.0);

    AestraEQ lpfEq;
    initializeActiveEQ(lpfEq);
    lpfEq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamLPFEnable, 1.0f);
    lpfEq.setParameter(AestraEQ::kParamLPFFreq, 0.0f); // 1000 Hz
    lpfEq.setParameter(AestraEQ::kParamLPFSlope, 1.0f); // 96 dB/oct
    lpfEq.setParameter(AestraEQ::kParamBypass, 0.0f);
    settleSmoothing(lpfEq);

    const auto lpf10000 = measureSineGainDb(lpfEq, 10000.0);

    const bool ok = hpf40.finite && lpf10000.finite && hpf40.gainDb < -9.0 && lpf10000.gainDb < -45.0;
    return report("Cut filters measure strong out-of-band rejection", ok);
}

bool testCutSlopesKeepButterworthCutoffLevel() {
    static constexpr float slopeNorms[] = {
        0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f, 1.0f
    };

    bool ok = true;
    for (float slopeNorm : slopeNorms) {
        AestraEQ hpfEq;
        initializeActiveEQ(hpfEq);
        hpfEq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
        hpfEq.setParameter(AestraEQ::kParamHPFFreq, normalizedLogFrequency(80.0, 20.0, 500.0));
        hpfEq.setParameter(AestraEQ::kParamHPFSlope, slopeNorm);
        hpfEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
        hpfEq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
        hpfEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
        hpfEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
        hpfEq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
        settleSmoothing(hpfEq, 2500);

        const auto hpfCutoff = measureSineGainDb(hpfEq, 80.0);
        const double hpfModel = hpfEq.getMagnitudeResponseDb(80.0);
        const bool hpfOk = hpfCutoff.finite &&
                           hpfCutoff.gainDb < -2.0 && hpfCutoff.gainDb > -4.2 &&
                           hpfModel < -2.0 && hpfModel > -4.2 &&
                           nearDb(hpfCutoff.gainDb, hpfModel, 0.35);
        if (!hpfOk) {
            std::cerr << "    HPF cutoff mismatch slopeNorm=" << slopeNorm
                      << " rendered=" << hpfCutoff.gainDb
                      << " model=" << hpfModel << "\n";
        }
        ok = ok && hpfOk;

        AestraEQ lpfEq;
        initializeActiveEQ(lpfEq);
        lpfEq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
        lpfEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
        lpfEq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
        lpfEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
        lpfEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
        lpfEq.setParameter(AestraEQ::kParamLPFEnable, 1.0f);
        lpfEq.setParameter(AestraEQ::kParamLPFFreq, normalizedLogFrequency(1000.0, 1000.0, 20000.0));
        lpfEq.setParameter(AestraEQ::kParamLPFSlope, slopeNorm);
        settleSmoothing(lpfEq, 2500);

        const auto lpfCutoff = measureSineGainDb(lpfEq, 1000.0);
        const double lpfModel = lpfEq.getMagnitudeResponseDb(1000.0);
        const bool lpfOk = lpfCutoff.finite &&
                           lpfCutoff.gainDb < -2.0 && lpfCutoff.gainDb > -4.2 &&
                           lpfModel < -2.0 && lpfModel > -4.2 &&
                           nearDb(lpfCutoff.gainDb, lpfModel, 0.35);
        if (!lpfOk) {
            std::cerr << "    LPF cutoff mismatch slopeNorm=" << slopeNorm
                      << " rendered=" << lpfCutoff.gainDb
                      << " model=" << lpfModel << "\n";
        }
        ok = ok && lpfOk;
    }

    return report("Cut slopes keep Butterworth -3 dB cutoff behavior", ok);
}

bool testExtendedCutSlopeRangeRendersAndModels() {
    AestraEQ hpfEq;
    initializeActiveEQ(hpfEq);
    hpfEq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    hpfEq.setParameter(AestraEQ::kParamHPFFreq, 0.392f); // 80 Hz
    hpfEq.setParameter(AestraEQ::kParamHPFSlope, 0.0f); // 6 dB/oct
    hpfEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    hpfEq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    hpfEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    hpfEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    hpfEq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(hpfEq);

    const auto hpf40 = measureSineGainDb(hpfEq, 40.0);
    const double hpfModelDb = hpfEq.getMagnitudeResponseDb(40.0);

    AestraEQ lpfEq;
    initializeActiveEQ(lpfEq);
    lpfEq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    lpfEq.setParameter(AestraEQ::kParamLPFEnable, 1.0f);
    lpfEq.setParameter(AestraEQ::kParamLPFFreq, 0.0f); // 1000 Hz
    lpfEq.setParameter(AestraEQ::kParamLPFSlope, 1.0f); // 96 dB/oct
    settleSmoothing(lpfEq);

    const auto lpf10000 = measureSineGainDb(lpfEq, 10000.0);
    const double lpfModelDb = lpfEq.getMagnitudeResponseDb(10000.0);

    const bool ok = hpf40.finite &&
                    lpf10000.finite &&
                    hpf40.gainDb < -4.0 &&
                    hpf40.gainDb > -12.0 &&
                    lpf10000.gainDb < -80.0 &&
                    nearDb(hpf40.gainDb, hpfModelDb, 0.50) &&
                    nearDb(lpf10000.gainDb, lpfModelDb, 2.0) &&
                    hpfEq.getParameterDisplay(AestraEQ::kParamHPFSlope) == "6 dB/oct" &&
                    lpfEq.getParameterDisplay(AestraEQ::kParamLPFSlope) == "96 dB/oct";
    return report("Extended 6-96 dB/oct cut slope range renders and models correctly", ok);
}

bool testHighFrequencyStressStaysFinite() {
    bool ok = true;
    for (double sampleRate : {44100.0, 48000.0, 96000.0}) {
        AestraEQ eq;
        eq.initialize(sampleRate, kBlockSize);
        eq.activate();
        eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
        eq.setParameter(AestraEQ::kParamBell1Freq, 1.0f);
        eq.setParameter(AestraEQ::kParamBell1Gain, 1.0f);
        eq.setParameter(AestraEQ::kParamBell1Q, 1.0f);
        settleSmoothing(eq);

        const double toneHz = std::min(19000.0, sampleRate * 0.45);
        const auto m = measureSineGainDb(eq, toneHz, sampleRate, 65536);
        ok = ok && m.finite && std::isfinite(m.gainDb);
    }

    return report("High-frequency EQ stress remains finite", ok);
}

bool testBandDisableClearsFilterMemory() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(1000.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Q, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(eq);
    processSine(eq, 1000.0);

    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    const double disabledPeak = processSilencePeak(eq);

    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    const double reenabledPeak = processSilencePeak(eq);

    const bool ok = disabledPeak < 1.0e-7 && reenabledPeak < 1.0e-7;
    return report("Band disable clears stale filter memory before re-enable", ok);
}

bool testBandTypeSwitchClearsFilterMemory() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Type, 2.0f / 3.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(1000.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Q, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(eq);
    processSine(eq, 1000.0);

    eq.setParameter(AestraEQ::kParamBell1Type, 1.0f / 3.0f);
    const double switchedPeak = processSilencePeak(eq);

    const bool ok = switchedPeak < 1.0e-7;
    return report("Band type switches clear stale filter memory", ok);
}

bool testActiveProcessingIsStereoBounded() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(1000.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, 0.75f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(eq);

    std::vector<std::vector<float>> inputs(4, std::vector<float>(kBlockSize, 0.0f));
    std::vector<std::vector<float>> outputs(4, std::vector<float>(kBlockSize, 99.0f));
    for (uint32_t i = 0; i < kBlockSize; ++i) {
        inputs[0][i] = 0.125f;
        inputs[1][i] = -0.125f;
        inputs[2][i] = 0.5f;
        inputs[3][i] = -0.5f;
    }

    const float* inPtrs[] = {inputs[0].data(), inputs[1].data(), inputs[2].data(), inputs[3].data()};
    float* outPtrs[] = {outputs[0].data(), outputs[1].data(), outputs[2].data(), outputs[3].data()};
    eq.process(inPtrs, outPtrs, 4, 4, kBlockSize);

    bool ok = true;
    for (uint32_t i = 0; i < kBlockSize; ++i) {
        ok = ok && std::isfinite(outputs[0][i]) && std::isfinite(outputs[1][i]);
        ok = ok && outputs[2][i] == inputs[2][i] && outputs[3][i] == inputs[3][i];
    }
    return report("Active EQ processes stereo and passes through higher channels", ok);
}

bool testActiveProcessingSanitizesNonFiniteInputWithoutEnabledFilters() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(eq);

    std::vector<float> input(kBlockSize, 0.25f);
    std::vector<float> output(kBlockSize, 99.0f);
    input[3] = std::numeric_limits<float>::quiet_NaN();
    input[7] = std::numeric_limits<float>::infinity();
    input[11] = -std::numeric_limits<float>::infinity();
    input[15] = 24.0f;

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, kBlockSize);

    bool ok = true;
    for (uint32_t i = 0; i < kBlockSize; ++i) {
        ok = ok && std::isfinite(output[i]);
        ok = ok && output[i] <= 16.0f && output[i] >= -16.0f;
    }
    ok = ok && output[3] == 0.0f && output[7] == 0.0f && output[11] == 0.0f;
    ok = ok && output[15] == 16.0f;
    return report("Active EQ sanitizes non-finite input without enabled filters", ok);
}

bool testActiveProcessingFlushesDenormalInput() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamHPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamHShEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    settleSmoothing(eq);

    std::vector<float> input(kBlockSize, std::numeric_limits<float>::denorm_min());
    std::vector<float> output(kBlockSize, 99.0f);
    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, kBlockSize);

    bool ok = true;
    for (float sample : output) {
        ok = ok && sample == 0.0f;
    }
    return report("Active EQ flushes denormal input to silence", ok);
}

bool testActiveProcessingTreatsMissingInputAsSilence() {
    AestraEQ eq;
    initializeActiveEQ(eq);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Gain, 1.0f);
    settleSmoothing(eq);
    processSine(eq, 1000.0);

    std::vector<float> output(kBlockSize, 99.0f);
    const float* inPtr = nullptr;
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, kBlockSize);

    bool ok = true;
    for (float sample : output) {
        ok = ok && sample == 0.0f;
    }

    return report("Active EQ treats missing input buffers as silence", ok);
}

} // namespace

int main() {
    std::cout << "=== Aestra EQ Measurement Tests ===\n\n";

    testBypassIsUnityAcrossSpectrum();
    testBypassSanitizesUnsafeInputOnly();
    testOutputGainScalesActiveSignal();
    testPolarityInvertFlipsActiveSignal();
    testBellCenterGainAccuracy();
    testResponseModelTracksRenderedGain();
    testResponseModelTracksRenderedMultiBandCurve();
    testBandResponseCurvesSumToTotalResponse();
    testMiddleBandFilterTypesRender();
    testTiltModeRendersAsTwoSidedTilt();
    testCutSlopesAreMeaningful();
    testCutSlopesKeepButterworthCutoffLevel();
    testExtendedCutSlopeRangeRendersAndModels();
    testHighFrequencyStressStaysFinite();
    testBandDisableClearsFilterMemory();
    testBandTypeSwitchClearsFilterMemory();
    testActiveProcessingIsStereoBounded();
    testActiveProcessingSanitizesNonFiniteInputWithoutEnabledFilters();
    testActiveProcessingFlushesDenormalInput();
    testActiveProcessingTreatsMissingInputAsSilence();

    std::cout << "\n";
    std::cout << g_testsPassed << " passed, " << g_testsFailed << " failed.\n";
    return g_testsFailed > 0 ? 1 : 0;
}
