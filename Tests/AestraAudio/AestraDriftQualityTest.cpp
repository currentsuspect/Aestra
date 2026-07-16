// © 2026 Aestra Studios — All Rights Reserved.
// Focused quality, compatibility, and click-safety contract for AestraDrift.

#include "Plugin/AestraDrift.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using Aestra::Audio::Plugins::AestraDrift;

constexpr double kPi = 3.14159265358979323846;

struct Render {
    std::vector<float> left;
    std::vector<float> right;
    uint32_t latency = 0;
};

Render render(float pitch, float grain, float fine, float spread, float motion, float motionRate, uint32_t blockSize,
              bool monoInput = false, bool impulse = false, float texture = 0.0f) {
    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t frames = sampleRate * 2;
    AestraDrift drift;
    drift.initialize(sampleRate, blockSize);
    drift.setParameter(AestraDrift::kPitch, pitch);
    drift.setParameter(AestraDrift::kGrain, grain);
    drift.setParameter(AestraDrift::kMix, 1.0f);
    drift.setParameter(AestraDrift::kFine, fine);
    drift.setParameter(AestraDrift::kSpread, spread);
    drift.setParameter(AestraDrift::kMotion, motion);
    drift.setParameter(AestraDrift::kMotionRate, motionRate);
    drift.setParameter(AestraDrift::kOutput, 0.5f);
    drift.setParameter(AestraDrift::kTexture, texture);
    drift.activate();

    std::vector<float> inputL(frames, 0.0f);
    std::vector<float> inputR(frames, 0.0f);
    if (impulse) {
        inputL[0] = 1.0f;
        inputR[0] = 1.0f;
    } else {
        for (uint32_t i = 0; i < frames; ++i) {
            inputL[i] = 0.25f * std::sin(static_cast<float>(2.0 * kPi * 440.0 * i / sampleRate));
            inputR[i] =
                monoInput ? inputL[i] : 0.22f * std::sin(static_cast<float>(2.0 * kPi * 330.0 * i / sampleRate));
        }
    }

    Render result;
    result.left.resize(frames);
    result.right.resize(frames);
    result.latency = drift.getLatencySamples();
    for (uint32_t offset = 0; offset < frames; offset += blockSize) {
        const uint32_t count = std::min(blockSize, frames - offset);
        const float* inputs[] = {inputL.data() + offset, inputR.data() + offset};
        float* outputs[] = {result.left.data() + offset, result.right.data() + offset};
        drift.process(inputs, outputs, monoInput ? 1u : 2u, 2u, count);
    }
    return result;
}

bool require(bool condition, const char* message) {
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

double rmsDifference(const std::vector<float>& a, const std::vector<float>& b, size_t start) {
    double sum = 0.0;
    for (size_t i = start; i < std::min(a.size(), b.size()); ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(std::max<size_t>(1, a.size() - start)));
}

double envelopeVariation(const std::vector<float>& signal, size_t start) {
    constexpr size_t windowSize = 480;
    std::vector<double> windows;
    for (size_t offset = start; offset + windowSize <= signal.size(); offset += windowSize) {
        double energy = 0.0;
        for (size_t i = offset; i < offset + windowSize; ++i)
            energy += static_cast<double>(signal[i]) * signal[i];
        windows.push_back(std::sqrt(energy / static_cast<double>(windowSize)));
    }
    if (windows.empty())
        return 0.0;
    double mean = 0.0;
    for (double value : windows)
        mean += value;
    mean /= static_cast<double>(windows.size());
    double variance = 0.0;
    for (double value : windows) {
        const double deviation = value - mean;
        variance += deviation * deviation;
    }
    variance /= static_cast<double>(windows.size());
    return std::sqrt(variance) / std::max(mean, std::numeric_limits<double>::min());
}

bool testUnityAndMonoParity() {
    const auto unity = render(0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.35f, 257u, true, true);
    const auto peakIt = std::max_element(unity.left.begin(), unity.left.end(),
                                         [](float a, float b) { return std::fabs(a) < std::fabs(b); });
    const size_t peakIndex = static_cast<size_t>(std::distance(unity.left.begin(), peakIt));
    bool ok = true;
    ok &= require(peakIndex == unity.latency, "unity pitch does not land at the reported latency");
    ok &= require(std::fabs(*peakIt - 1.0f) < 1.0e-5f, "unity pitch is not amplitude transparent");
    ok &= require(rmsDifference(unity.left, unity.right, 0) < 1.0e-7, "mono input does not produce dual-mono output");
    return ok;
}

bool testBlockInvarianceAndGrainTruth() {
    const auto block1 = render(0.79f, 0.15f, 0.5f, 0.0f, 0.0f, 0.35f, 1u);
    const auto block257 = render(0.79f, 0.15f, 0.5f, 0.0f, 0.0f, 0.35f, 257u);
    const auto longGrain = render(0.79f, 0.95f, 0.5f, 0.0f, 0.0f, 0.35f, 257u);
    const size_t start = 12000;
    const double blockError = rmsDifference(block1.left, block257.left, start);
    const double grainDifference = rmsDifference(block257.left, longGrain.left, start);
    std::cout << "Block RMS error: " << blockError << ", grain RMS difference: " << grainDifference << '\n';
    bool ok = true;
    ok &= require(blockError < 1.0e-7, "render changes with host block size");
    ok &= require(grainDifference > 1.0e-3, "Grain parameter does not materially change shifted audio");
    return ok;
}

bool testClickSafetyAndFiniteOutput() {
    const auto shifted = render(0.79f, 0.55f, 0.5f, 0.0f, 0.0f, 0.35f, 193u);
    const size_t start = 12000;
    double maxDelta = 0.0;
    double derivativeEnergy = 0.0;
    double signalEnergy = 0.0;
    size_t positiveCrossings = 0;
    bool finite = true;
    for (size_t i = start + 1; i < shifted.left.size(); ++i) {
        finite &= std::isfinite(shifted.left[i]) && std::isfinite(shifted.right[i]);
        const double delta = std::fabs(static_cast<double>(shifted.left[i]) - shifted.left[i - 1]);
        maxDelta = std::max(maxDelta, delta);
        derivativeEnergy += delta * delta;
        signalEnergy += static_cast<double>(shifted.left[i]) * shifted.left[i];
        if (shifted.left[i - 1] <= 0.0f && shifted.left[i] > 0.0f)
            ++positiveCrossings;
    }
    const double measuredFrames = static_cast<double>(shifted.left.size() - start - 1);
    const double derivativeRms = std::sqrt(derivativeEnergy / measuredFrames);
    const double signalRms = std::sqrt(signalEnergy / measuredFrames);
    const double measuredHz = static_cast<double>(positiveCrossings) * 48000.0 / measuredFrames;
    const double expectedHz = 440.0 * std::pow(2.0, 6.96 / 12.0);
    std::cout << "Shifted max delta: " << maxDelta << ", derivative RMS: " << derivativeRms
              << ", signal RMS: " << signalRms << ", pitch: " << measuredHz << " Hz\n";
    bool ok = true;
    ok &= require(finite, "shifted render produced NaN or Inf");
    ok &= require(signalRms > 0.05, "shifted render lost too much signal energy");
    ok &= require(std::fabs(measuredHz - expectedHz) / expectedHz < 0.05, "shifted pitch is outside 5% tolerance");
    ok &= require(maxDelta < 0.20, "shifted render contains a click-scale discontinuity");
    ok &= require(maxDelta < derivativeRms * 8.0, "shifted render has an outlier discontinuity");
    return ok;
}

bool testAutomationClickSafety() {
    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t frames = sampleRate * 2;
    constexpr uint32_t blockSize = 64;
    AestraDrift drift;
    drift.initialize(sampleRate, blockSize);
    drift.setParameter(AestraDrift::kPitch, 0.5f);
    drift.setParameter(AestraDrift::kGrain, 0.1f);
    drift.setParameter(AestraDrift::kMix, 1.0f);
    drift.setParameter(AestraDrift::kMotion, 0.0f);
    drift.activate();

    std::vector<float> input(frames);
    std::vector<float> outputL(frames);
    std::vector<float> outputR(frames);
    for (uint32_t i = 0; i < frames; ++i)
        input[i] = 0.25f * std::sin(static_cast<float>(2.0 * kPi * 440.0 * i / sampleRate));
    for (uint32_t offset = 0; offset < frames; offset += blockSize) {
        if (offset == sampleRate / 2)
            drift.setParameter(AestraDrift::kPitch, 0.8f);
        if (offset == sampleRate)
            drift.setParameter(AestraDrift::kGrain, 0.9f);
        if (offset == sampleRate + sampleRate / 2) {
            drift.setParameter(AestraDrift::kMotion, 0.8f);
            drift.setParameter(AestraDrift::kTexture, 1.0f);
        }
        const float* inputs[] = {input.data() + offset};
        float* outputs[] = {outputL.data() + offset, outputR.data() + offset};
        drift.process(inputs, outputs, 1u, 2u, blockSize);
    }

    double maxDelta = 0.0;
    for (size_t i = drift.getLatencySamples() + 1u; i < outputL.size(); ++i)
        maxDelta = std::max(maxDelta, std::fabs(static_cast<double>(outputL[i]) - outputL[i - 1]));
    std::cout << "Automation max delta: " << maxDelta << '\n';
    return require(maxDelta < 0.20, "Pitch/Grain/Motion/Texture automation produced a click-scale discontinuity");
}

bool testSampleRateExtremes() {
    bool ok = true;
    for (uint32_t sampleRate : {44100u, 96000u}) {
        AestraDrift drift;
        drift.initialize(sampleRate, 257u);
        drift.setParameter(AestraDrift::kPitch, 1.0f);
        drift.setParameter(AestraDrift::kGrain, 1.0f);
        drift.setParameter(AestraDrift::kFine, 1.0f);
        drift.setParameter(AestraDrift::kSpread, 1.0f);
        drift.setParameter(AestraDrift::kMotion, 1.0f);
        drift.setParameter(AestraDrift::kMotionRate, 1.0f);
        drift.activate();
        std::vector<float> input(4096);
        std::vector<float> outputL(4096);
        std::vector<float> outputR(4096);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = 0.2f * std::sin(static_cast<float>(2.0 * kPi * 330.0 * i / sampleRate));
        const float* inputs[] = {input.data()};
        float* outputs[] = {outputL.data(), outputR.data()};
        drift.process(inputs, outputs, 1u, 2u, static_cast<uint32_t>(input.size()));
        for (size_t i = 0; i < outputL.size(); ++i) {
            ok &= require(std::isfinite(outputL[i]) && std::isfinite(outputR[i]),
                          "sample-rate extreme produced NaN or Inf");
            if (!ok)
                return false;
        }
        ok &= require(drift.getLatencySamples() > 0u, "sample-rate-aware latency was not reported");
    }
    return ok;
}

bool testBypassParity() {
    AestraDrift drift;
    drift.initialize(48000.0, 257u);
    drift.activate();
    drift.setParameter(AestraDrift::kBypass, 1.0f);
    std::vector<float> inputL(257);
    std::vector<float> inputR(257);
    std::vector<float> outputL(257, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> outputR(257, std::numeric_limits<float>::quiet_NaN());
    for (size_t i = 0; i < inputL.size(); ++i) {
        inputL[i] = static_cast<float>(i) / static_cast<float>(inputL.size());
        inputR[i] = -inputL[i];
    }
    const float* inputs[] = {inputL.data(), inputR.data()};
    float* outputs[] = {outputL.data(), outputR.data()};
    drift.process(inputs, outputs, 2u, 2u, static_cast<uint32_t>(inputL.size()));
    return require(outputL == inputL && outputR == inputR, "bypass is not sample-exact");
}

bool testCenteredStereoSpread() {
    const auto spread = render(0.79f, 0.45f, 0.5f, 1.0f, 0.0f, 0.35f, 257u, true);
    constexpr size_t start = 12000;
    double energyL = 0.0;
    double energyR = 0.0;
    for (size_t i = start; i < spread.left.size(); ++i) {
        energyL += static_cast<double>(spread.left[i]) * spread.left[i];
        energyR += static_cast<double>(spread.right[i]) * spread.right[i];
    }
    const double levelRatio = std::sqrt(energyR / std::max(energyL, std::numeric_limits<double>::min()));
    const double stereoDifference = rmsDifference(spread.left, spread.right, start);
    std::cout << "Spread R/L ratio: " << levelRatio << ", stereo RMS difference: " << stereoDifference << '\n';
    bool ok = true;
    ok &= require(levelRatio > 0.98 && levelRatio < 1.02, "Spread shifts energy away from the stereo center");
    ok &= require(stereoDifference > 1.0e-3, "Spread does not create stereo decorrelation");
    return ok;
}

bool testTextureIsOptional() {
    constexpr size_t start = 12000;
    const auto clean = render(0.79f, 0.0f, 0.5f, 0.0f, 0.0f, 0.35f, 257u, true, false, 0.0f);
    const auto textured = render(0.79f, 0.0f, 0.5f, 0.0f, 0.0f, 0.35f, 257u, true, false, 1.0f);
    const double cleanVariation = envelopeVariation(clean.left, start);
    const double texturedVariation = envelopeVariation(textured.left, start);
    const double textureDifference = rmsDifference(clean.left, textured.left, start);
    double texturedMaxDelta = 0.0;
    for (size_t i = start + 1; i < textured.left.size(); ++i) {
        texturedMaxDelta =
            std::max(texturedMaxDelta, std::fabs(static_cast<double>(textured.left[i]) - textured.left[i - 1]));
    }
    std::cout << "Texture envelope variation clean/textured: " << cleanVariation << "/" << texturedVariation
              << ", RMS difference: " << textureDifference << ", max delta: " << texturedMaxDelta << '\n';
    bool ok = true;
    ok &= require(textureDifference > 1.0e-3, "Texture does not materially change shifted audio");
    ok &= require(cleanVariation < texturedVariation, "Texture OFF does not reduce cyclic envelope movement");
    ok &= require(texturedMaxDelta < 0.20, "Texture introduced a click-scale discontinuity");
    return ok;
}

bool testStateMigration() {
    constexpr uint32_t magic = 0x44524654;
    constexpr uint32_t version = 1;
    const float legacyValues[] = {0.75f, 0.2f, 0.8f, 0.0f};
    std::vector<uint8_t> legacy(sizeof(magic) + sizeof(version) + sizeof(legacyValues));
    std::memcpy(legacy.data(), &magic, sizeof(magic));
    std::memcpy(legacy.data() + sizeof(magic), &version, sizeof(version));
    std::memcpy(legacy.data() + sizeof(magic) + sizeof(version), legacyValues, sizeof(legacyValues));

    AestraDrift drift;
    drift.initialize(48000.0, 256);
    bool ok = require(drift.loadState(legacy), "legacy V1 state was rejected");
    for (uint32_t i : {AestraDrift::kPitch, AestraDrift::kMix, AestraDrift::kBypass})
        ok &= require(std::fabs(drift.getParameter(i) - legacyValues[i]) < 1.0e-7f,
                      "legacy parameter ID moved during migration");
    ok &= require(drift.getParameter(AestraDrift::kGrain) == 0.0f, "legacy Grain did not migrate to PURE");
    ok &= require(drift.getParameter(AestraDrift::kFine) == 0.5f, "legacy Fine default is not neutral");
    ok &= require(drift.getParameter(AestraDrift::kSpread) == 0.0f, "legacy Spread default is not pure");
    ok &= require(drift.getParameter(AestraDrift::kMotion) == 0.0f, "legacy Motion default is not pure");
    ok &= require(drift.getParameter(AestraDrift::kOutput) == 0.5f, "legacy Output default is not unity");
    ok &= require(drift.getParameter(AestraDrift::kTexture) == 0.0f, "legacy Texture default is not off");

    constexpr uint32_t version2 = 2;
    const float v2Values[] = {0.6f, 0.3f, 0.7f, 0.0f, 0.45f, 0.2f, 0.1f, 0.4f, 0.55f};
    std::vector<uint8_t> v2(sizeof(magic) + sizeof(version2) + sizeof(v2Values));
    std::memcpy(v2.data(), &magic, sizeof(magic));
    std::memcpy(v2.data() + sizeof(magic), &version2, sizeof(version2));
    std::memcpy(v2.data() + sizeof(magic) + sizeof(version2), v2Values, sizeof(v2Values));
    AestraDrift restoredV2;
    restoredV2.initialize(48000.0, 256);
    ok &= require(restoredV2.loadState(v2), "V2 state was rejected");
    ok &= require(restoredV2.getParameter(AestraDrift::kTexture) == 0.0f, "V2 Texture default is not off");

    for (uint32_t i = 0; i < AestraDrift::kParamCount; ++i)
        drift.setParameter(i, 0.1f + 0.8f * static_cast<float>(i) / static_cast<float>(AestraDrift::kParamCount - 1));
    const auto state = drift.saveState();
    AestraDrift restored;
    restored.initialize(96000.0, 511);
    ok &= require(restored.loadState(state), "current state round-trip was rejected");
    for (uint32_t i = 0; i < AestraDrift::kParamCount; ++i)
        ok &= require(std::fabs(restored.getParameter(i) - drift.getParameter(i)) < 1.0e-7f,
                      "current parameter failed state round-trip");
    return ok;
}
} // namespace

int main() {
    bool ok = true;
    ok &= testUnityAndMonoParity();
    ok &= testBlockInvarianceAndGrainTruth();
    ok &= testClickSafetyAndFiniteOutput();
    ok &= testAutomationClickSafety();
    ok &= testSampleRateExtremes();
    ok &= testBypassParity();
    ok &= testCenteredStereoSpread();
    ok &= testTextureIsOptional();
    ok &= testStateMigration();
    if (!ok)
        return 1;
    std::cout << "AestraDrift quality contract passed.\n";
    return 0;
}
