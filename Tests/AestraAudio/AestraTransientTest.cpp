// © 2026 Aestra Studios — All Rights Reserved.
// AestraTransientTest — material tests for the zero-latency transient shaper.
//
// Covers the acceptance surface from the v0.7.1 sprint scope (P-1): bypass
// parity, silence handling, zero-latency impulse alignment, neutral unity,
// onset emphasis / tail shaping in the eq/comp material style, hostile input,
// state roundtrip, block-size and sample-rate stability.

#include "Plugin/AestraTransient.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraTransient;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 256;
constexpr float kPi = 3.14159265358979323846f;

void configure(AestraTransient& plugin, double sampleRate, float attack, float sustain, float output = 0.5f,
               float mix = 1.0f) {
    plugin.initialize(sampleRate, kBlockSize);
    plugin.setParameter(AestraTransient::kAttack, attack);
    plugin.setParameter(AestraTransient::kSustain, sustain);
    plugin.setParameter(AestraTransient::kOutput, output);
    plugin.setParameter(AestraTransient::kMix, mix);
    plugin.setParameter(AestraTransient::kBypass, 0.0f);
    plugin.activate();
}

// Drum-workflow reference signal: 60 Hz kick-like sine burst with exponential
// decay over 400 ms.
std::vector<float> kickBurst(double sampleRate, float amplitude = 0.8f) {
    const double seconds = 0.4;
    const size_t frames = static_cast<size_t>(seconds * sampleRate);
    std::vector<float> data(frames, 0.0f);
    const double tau = 0.12; // 120 ms decay
    for (size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        data[i] = amplitude * std::sin(2.0 * kPi * 60.0 * t) * std::exp(-t / tau);
    }
    return data;
}

std::vector<float> process(AestraTransient& plugin, const std::vector<float>& input, uint32_t blockSize = kBlockSize) {
    std::vector<float> output(input.size(), 0.0f);
    for (size_t offset = 0; offset < input.size(); offset += blockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(blockSize, input.size() - offset));
        const float* inputs[] = {input.data() + offset};
        float* outputs[] = {output.data() + offset};
        plugin.process(inputs, outputs, 1, 1, frames);
    }
    return output;
}

double windowEnergy(const std::vector<float>& data, size_t begin, size_t end) {
    double sum = 0.0;
    for (size_t i = begin; i < end && i < data.size(); ++i) {
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }
    return sum;
}

size_t windowBegin(const std::vector<float>& data, double seconds, double sampleRate) {
    return std::min(data.size(), static_cast<size_t>(seconds * sampleRate));
}

bool allFinite(const std::vector<float>& data) {
    for (float sample : data) {
        if (!std::isfinite(sample))
            return false;
    }
    return true;
}

bool testBypassParity() {
    const auto input = kickBurst(kSampleRate);
    AestraTransient plugin;
    configure(plugin, kSampleRate, 0.9f, 0.1f);
    plugin.setParameter(AestraTransient::kBypass, 1.0f);
    const auto output = process(plugin, input);
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != output[i])
            return false;
    }
    return true;
}

bool testSilenceInSilenceOut() {
    const std::vector<float> silence(48000, 0.0f);
    for (const auto amount : {0.0f, 0.5f, 1.0f}) {
        AestraTransient plugin;
        configure(plugin, kSampleRate, amount, amount);
        const auto output = process(plugin, silence);
        for (float sample : output) {
            if (sample != 0.0f)
                return false;
        }
        if (!allFinite(output))
            return false;
    }
    return true;
}

bool testZeroLatencyImpulseAlignment() {
    std::vector<float> impulse(4800, 0.0f);
    impulse[1000] = 1.0f;
    for (const auto amount : {0.0f, 1.0f}) {
        AestraTransient plugin;
        configure(plugin, kSampleRate, amount, amount);
        const auto output = process(plugin, impulse);
        for (size_t i = 0; i < 1000; ++i) {
            if (output[i] != 0.0f)
                return false; // no energy before the impulse: zero latency
        }
        size_t peakIndex = 0;
        for (size_t i = 0; i < output.size(); ++i) {
            if (std::abs(output[i]) > std::abs(output[peakIndex]))
                peakIndex = i;
        }
        if (peakIndex != 1000)
            return false; // peak lands exactly on the impulse
    }
    return true;
}

bool testNeutralIsUnity() {
    const auto input = kickBurst(kSampleRate);
    AestraTransient plugin;
    configure(plugin, kSampleRate, 0.5f, 0.5f);
    const auto output = process(plugin, input);
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != output[i])
            return false; // neutral amounts and 100% mix are an exact identity
    }
    return true;
}

bool testAttackShapesOnset() {
    const auto input = kickBurst(kSampleRate);
    const size_t onsetEnd = windowBegin(input, 0.015, kSampleRate); // first 15 ms

    AestraTransient neutral;
    configure(neutral, kSampleRate, 0.5f, 0.5f);
    const auto outNeutral = process(neutral, input);
    const double energyNeutral = windowEnergy(outNeutral, 0, onsetEnd);

    AestraTransient boosted;
    configure(boosted, kSampleRate, 1.0f, 0.5f);
    const auto outBoosted = process(boosted, input);
    const double energyBoosted = windowEnergy(outBoosted, 0, onsetEnd);

    AestraTransient cut;
    configure(cut, kSampleRate, 0.0f, 0.5f);
    const auto outCut = process(cut, input);
    const double energyCut = windowEnergy(outCut, 0, onsetEnd);

    if (energyNeutral <= 0.0)
        return false;
    if (energyBoosted <= energyNeutral * 1.5)
        return false; // attack boost must audibly emphasize the onset
    if (energyCut >= energyNeutral * 0.8)
        return false; // attack cut must attenuate the onset
    return allFinite(outBoosted) && allFinite(outCut);
}

bool testSustainShapesTail() {
    const auto input = kickBurst(kSampleRate);
    const size_t tailBegin = windowBegin(input, 0.15, kSampleRate);
    const size_t tailEnd = windowBegin(input, 0.35, kSampleRate);

    AestraTransient neutral;
    configure(neutral, kSampleRate, 0.5f, 0.5f);
    const auto outNeutral = process(neutral, input);
    const double energyNeutral = windowEnergy(outNeutral, tailBegin, tailEnd);

    AestraTransient boosted;
    configure(boosted, kSampleRate, 0.5f, 1.0f);
    const auto outBoosted = process(boosted, input);
    const double energyBoosted = windowEnergy(outBoosted, tailBegin, tailEnd);

    AestraTransient cut;
    configure(cut, kSampleRate, 0.5f, 0.0f);
    const auto outCut = process(cut, input);
    const double energyCut = windowEnergy(outCut, tailBegin, tailEnd);

    if (energyNeutral <= 0.0)
        return false;
    if (energyBoosted <= energyNeutral * 1.3)
        return false; // sustain boost must lengthen the tail
    if (energyCut >= energyNeutral * 0.7)
        return false; // sustain cut must tighten the tail
    return allFinite(outBoosted) && allFinite(outCut);
}

bool testSurvivesHostileInput() {
    std::vector<float> input(48000, 0.0f);
    input[100] = std::nanf("");
    input[200] = std::numeric_limits<float>::infinity();
    input[300] = -std::numeric_limits<float>::infinity();
    input[400] = 1.0e30f;
    for (size_t i = 500; i < 47000; ++i) {
        input[i] = 0.5f * std::sin(2.0 * kPi * 220.0 * static_cast<double>(i) / kSampleRate);
    }
    AestraTransient plugin;
    configure(plugin, kSampleRate, 1.0f, 0.0f);
    const auto output = process(plugin, input);
    if (!allFinite(output))
        return false;
    // After the hostile samples, output returns to sane levels.
    double tailPeak = 0.0;
    for (size_t i = 1000; i < output.size(); ++i) {
        tailPeak = std::max(tailPeak, std::abs(static_cast<double>(output[i])));
    }
    return tailPeak < 2.0;
}

bool testStereoLinkPreservesImage() {
    // Mono-correlated source: R is -6 dB of L. The linked detector applies one
    // gain to both channels, so the ratio must survive sample-exactly.
    const auto mono = kickBurst(kSampleRate);
    std::vector<float> left(mono.size());
    std::vector<float> right(mono.size());
    for (size_t i = 0; i < mono.size(); ++i) {
        left[i] = mono[i];
        right[i] = mono[i] * 0.5f;
    }
    AestraTransient plugin;
    configure(plugin, kSampleRate, 1.0f, 0.0f);
    std::vector<float> outL(left.size(), 0.0f);
    std::vector<float> outR(right.size(), 0.0f);
    for (size_t offset = 0; offset < left.size(); offset += kBlockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kBlockSize, left.size() - offset));
        const float* inputs[] = {left.data() + offset, right.data() + offset};
        float* outputs[] = {outL.data() + offset, outR.data() + offset};
        plugin.process(inputs, outputs, 2, 2, frames);
    }
    for (size_t i = 0; i < outL.size(); ++i) {
        if (std::abs(outL[i] - 2.0f * outR[i]) > 1.0e-4f)
            return false;
    }
    return true;
}

bool testChannelsAboveStereoPassThrough() {
    // Channels above stereo must behave identically active and bypassed
    // (pass-through), so the bypass switch never audibly changes them.
    const auto mono = kickBurst(kSampleRate);
    std::vector<float> c2(mono.size(), 0.0f);
    for (size_t i = 0; i < c2.size(); ++i) {
        c2[i] = 0.3f * std::sin(2.0f * kPi * 880.0f * static_cast<double>(i) / kSampleRate);
    }

    std::vector<float> outA0(mono.size(), 0.0f), outA1(mono.size(), 0.0f), outA2(mono.size(), 0.0f);
    AestraTransient active;
    configure(active, kSampleRate, 0.5f, 0.5f);
    for (size_t offset = 0; offset < mono.size(); offset += kBlockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kBlockSize, mono.size() - offset));
        const float* inputs[] = {mono.data() + offset, mono.data() + offset, c2.data() + offset};
        float* outputs[] = {outA0.data() + offset, outA1.data() + offset, outA2.data() + offset};
        active.process(inputs, outputs, 3, 3, frames);
    }

    std::vector<float> outB2(mono.size(), 0.0f);
    AestraTransient bypassed;
    configure(bypassed, kSampleRate, 0.5f, 0.5f);
    bypassed.setParameter(AestraTransient::kBypass, 1.0f);
    std::vector<float> outB0(mono.size(), 0.0f), outB1(mono.size(), 0.0f);
    for (size_t offset = 0; offset < mono.size(); offset += kBlockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kBlockSize, mono.size() - offset));
        const float* inputs[] = {mono.data() + offset, mono.data() + offset, c2.data() + offset};
        float* outputs[] = {outB0.data() + offset, outB1.data() + offset, outB2.data() + offset};
        bypassed.process(inputs, outputs, 3, 3, frames);
    }

    for (size_t i = 0; i < mono.size(); ++i) {
        if (outA2[i] != outB2[i])
            return false; // above-stereo channel identical active vs bypass
        if (outA0[i] != outB0[i] || outA1[i] != outB1[i])
            return false; // neutral params are unity, so stereo pair matches too
    }
    return true;
}

bool testStateRoundTrip() {
    AestraTransient original;
    configure(original, kSampleRate, 0.8f, 0.2f, 0.75f, 0.4f);
    const auto state = original.saveState();

    AestraTransient restored;
    configure(restored, kSampleRate, 0.5f, 0.5f);
    if (!restored.loadState(state))
        return false;
    for (uint32_t id = 0; id < AestraTransient::kParamCount; ++id) {
        if (restored.getParameter(id) != original.getParameter(id))
            return false;
    }
    return true;
}

bool testLoadStateRejectsGarbage() {
    AestraTransient plugin;
    configure(plugin, kSampleRate, 0.5f, 0.5f);
    if (plugin.loadState({}))
        return false;
    if (plugin.loadState({0x01, 0x02}))
        return false;
    std::vector<uint8_t> wrongMagic(sizeof(uint32_t) * 2 + sizeof(float) * AestraTransient::kParamCount, 0xAB);
    if (plugin.loadState(wrongMagic))
        return false;
    // A corrupt (non-finite) parameter value must reject the whole blob
    // instead of leaving the instance partially updated.
    std::vector<uint8_t> corrupt(sizeof(uint32_t) * 2 + sizeof(float) * AestraTransient::kParamCount, 0);
    const uint32_t magic = AestraTransient::kStateMagic;
    const uint32_t version = 1;
    std::memcpy(corrupt.data(), &magic, sizeof(magic));
    std::memcpy(corrupt.data() + sizeof(magic), &version, sizeof(version));
    const float nan = std::nanf("");
    std::memcpy(corrupt.data() + sizeof(magic) + sizeof(version) + sizeof(float), &nan, sizeof(nan));
    if (plugin.loadState(corrupt))
        return false;
    if (plugin.getParameter(AestraTransient::kAttack) != 0.5f)
        return false; // pre-existing values untouched by the rejected blob
    return true;
}

bool testParameterMapping() {
    if (AestraTransient::bipolarFromNorm(0.0f) != -1.0f)
        return false;
    if (AestraTransient::bipolarFromNorm(0.5f) != 0.0f)
        return false;
    if (AestraTransient::bipolarFromNorm(1.0f) != 1.0f)
        return false;
    if (std::abs(AestraTransient::outputDbFromNorm(0.5f)) > 1.0e-6f)
        return false;
    if (std::abs(AestraTransient::outputDbFromNorm(0.0f) + 12.0f) > 1.0e-6f)
        return false;
    if (std::abs(AestraTransient::outputDbFromNorm(1.0f) - 12.0f) > 1.0e-6f)
        return false;
    return true;
}

bool testSetParameterRejectsNonFinite() {
    AestraTransient plugin;
    configure(plugin, kSampleRate, 0.5f, 0.5f);
    plugin.setParameter(AestraTransient::kAttack, std::nanf(""));
    if (plugin.getParameter(AestraTransient::kAttack) != 0.5f)
        return false;
    plugin.setParameter(AestraTransient::kAttack, 5.0f);
    if (plugin.getParameter(AestraTransient::kAttack) != 1.0f)
        return false;
    plugin.setParameter(AestraTransient::kAttack, -3.0f);
    if (plugin.getParameter(AestraTransient::kAttack) != 0.0f)
        return false;
    return true;
}

bool testBlockSizeInvariance() {
    const auto input = kickBurst(kSampleRate);
    AestraTransient a;
    configure(a, kSampleRate, 0.9f, 0.1f);
    const auto out256 = process(a, input, 256);

    AestraTransient b;
    configure(b, kSampleRate, 0.9f, 0.1f);
    const auto outOdd = process(b, input, 137);

    // With static parameters the smoothed values converge, so the steady tail
    // must match regardless of how frames are partitioned.
    const size_t settle = windowBegin(input, 0.2, kSampleRate);
    for (size_t i = settle; i < input.size(); ++i) {
        if (std::abs(out256[i] - outOdd[i]) > 1.0e-5f)
            return false;
    }
    return true;
}

bool testStableAcrossSampleRates() {
    // Onset emphasis measured at 44.1 kHz must track 48 kHz within 2 dB.
    auto onsetRatioDb = [&](double sampleRate) {
        const auto input = kickBurst(sampleRate);
        const size_t onsetEnd = windowBegin(input, 0.015, sampleRate);

        AestraTransient neutral;
        configure(neutral, sampleRate, 0.5f, 0.5f);
        const auto outNeutral = process(neutral, input);

        AestraTransient boosted;
        configure(boosted, sampleRate, 1.0f, 0.5f);
        const auto outBoosted = process(boosted, input);

        const double eN = windowEnergy(outNeutral, 0, onsetEnd);
        const double eB = windowEnergy(outBoosted, 0, onsetEnd);
        return 10.0 * std::log10(eB / eN);
    };
    const double at441 = onsetRatioDb(44100.0);
    const double at48 = onsetRatioDb(48000.0);
    return std::abs(at441 - at48) < 2.0;
}

struct TestCase {
    const char* name;
    bool (*fn)();
};

} // namespace

int main() {
    const TestCase tests[] = {
        {"BypassParity", testBypassParity},
        {"SilenceInSilenceOut", testSilenceInSilenceOut},
        {"ZeroLatencyImpulseAlignment", testZeroLatencyImpulseAlignment},
        {"NeutralIsUnity", testNeutralIsUnity},
        {"AttackShapesOnset", testAttackShapesOnset},
        {"SustainShapesTail", testSustainShapesTail},
        {"SurvivesHostileInput", testSurvivesHostileInput},
        {"StereoLinkPreservesImage", testStereoLinkPreservesImage},
        {"ChannelsAboveStereoPassThrough", testChannelsAboveStereoPassThrough},
        {"StateRoundTrip", testStateRoundTrip},
        {"LoadStateRejectsGarbage", testLoadStateRejectsGarbage},
        {"ParameterMapping", testParameterMapping},
        {"SetParameterRejectsNonFinite", testSetParameterRejectsNonFinite},
        {"BlockSizeInvariance", testBlockSizeInvariance},
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
    std::cout << "All AestraTransient tests passed\n";
    return 0;
}
