// © 2026 Aestra Studios — All Rights Reserved.
// AestraLFOTest — DSP contract tests for the tempo-syncable audio-path LFO.

#include "Plugin/AestraLFO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraLFO;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 512;

struct StereoBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

StereoBuffer processStereo(AestraLFO& lfo, const std::vector<float>& inL, const std::vector<float>& inR) {
    StereoBuffer out{std::vector<float>(inL.size(), 0.0f), std::vector<float>(inR.size(), 0.0f)};
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {out.left.data(), out.right.data()};
    lfo.process(inputs, outputs, 2, 2, static_cast<uint32_t>(inL.size()));
    return out;
}

std::vector<float> makeSine(uint32_t numSamples, float freq, float amp, double sampleRate) {
    std::vector<float> buf(numSamples);
    const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
    for (uint32_t i = 0; i < numSamples; ++i)
        buf[i] = static_cast<float>(amp * std::sin(w * static_cast<double>(i)));
    return buf;
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

/// Peak amplitude per fixed-size window — the modulation envelope.
std::vector<float> windowPeaks(const std::vector<float>& buf, size_t window, size_t start = 0) {
    std::vector<float> peaks;
    for (size_t w = start; w + window <= buf.size(); w += window) {
        float p = 0.0f;
        for (size_t i = w; i < w + window; ++i)
            p = std::max(p, std::abs(buf[i]));
        peaks.push_back(p);
    }
    return peaks;
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

constexpr float kRate2HzNorm = 2.0f / 3.0f; // 0.02 * 1000^(2/3) = 2 Hz

void setupFreeRunning(AestraLFO& lfo, AestraLFO::Target target, AestraLFO::Wave wave, float depth) {
    lfo.initialize(kSampleRate, kBlockSize);
    lfo.setParameter(AestraLFO::kTarget, AestraLFO::normFromTarget(target));
    lfo.setParameter(AestraLFO::kWave, AestraLFO::normFromWave(wave));
    lfo.setParameter(AestraLFO::kSyncMode, 0.0f);
    lfo.setParameter(AestraLFO::kRateHz, kRate2HzNorm);
    lfo.setParameter(AestraLFO::kDepth, depth);
    lfo.setParameter(AestraLFO::kSmooth, 0.0f);
}

bool testBypassPassesAudioUnchanged() {
    AestraLFO lfo;
    lfo.initialize(kSampleRate, kBlockSize);
    lfo.setParameter(AestraLFO::kBypass, 1.0f);
    lfo.setParameter(AestraLFO::kDepth, 1.0f);
    lfo.activate();

    const auto inL = makeSine(1024, 1000.0f, 0.5f, kSampleRate);
    const auto inR = makeSine(1024, 500.0f, 0.3f, kSampleRate);
    auto out = processStereo(lfo, inL, inR);

    for (uint32_t i = 0; i < inL.size(); ++i) {
        if (std::abs(out.left[i] - inL[i]) > 1e-6f)
            return false;
        if (std::abs(out.right[i] - inR[i]) > 1e-6f)
            return false;
    }
    return true;
}

bool testSilenceStaysSilent() {
    AestraLFO lfo;
    setupFreeRunning(lfo, AestraLFO::kTargetVolume, AestraLFO::kWaveSine, 1.0f);
    lfo.activate();
    const std::vector<float> silence(8192, 0.0f);
    auto out = processStereo(lfo, silence, silence);
    return peakAmplitude(out.left) < 1e-8f && peakAmplitude(out.right) < 1e-8f;
}

// Depth 0 must be transparent on every target.
bool testDepthZeroIsTransparent() {
    for (auto target : {AestraLFO::kTargetVolume, AestraLFO::kTargetPan, AestraLFO::kTargetCutoff}) {
        AestraLFO lfo;
        setupFreeRunning(lfo, target, AestraLFO::kWaveSine, 0.0f);
        lfo.activate();
        const float freq = 1000.0f;
        const auto in = makeSine(16384, freq, 0.5f, kSampleRate);
        auto out = processStereo(lfo, in, in);
        const float mag = goertzelMag(out.left, 8192, freq, kSampleRate) / 0.5f;
        if (mag < 0.98f || mag > 1.02f)
            return false;
    }
    return true;
}

// Volume target at full depth: the envelope must swing from ~full to ~zero.
bool testVolumeTargetTremolo() {
    AestraLFO lfo;
    setupFreeRunning(lfo, AestraLFO::kTargetVolume, AestraLFO::kWaveSine, 1.0f);
    lfo.activate();

    const auto in = makeSine(48000, 1000.0f, 0.5f, kSampleRate); // 2 LFO periods
    auto out = processStereo(lfo, in, in);
    const auto peaks = windowPeaks(out.left, 2400); // 50 ms windows
    const float maxP = *std::max_element(peaks.begin(), peaks.end());
    const float minP = *std::min_element(peaks.begin(), peaks.end());
    return maxP > 0.4f && minP < 0.06f;
}

// Pan target at full depth: energy must alternate between channels.
bool testPanTargetAlternatesChannels() {
    AestraLFO lfo;
    setupFreeRunning(lfo, AestraLFO::kTargetPan, AestraLFO::kWaveSine, 1.0f);
    lfo.activate();

    const auto in = makeSine(48000, 1000.0f, 0.5f, kSampleRate);
    auto out = processStereo(lfo, in, in);
    const auto peaksL = windowPeaks(out.left, 2400);
    const auto peaksR = windowPeaks(out.right, 2400);
    bool leftDominates = false;
    bool rightDominates = false;
    for (size_t i = 0; i < peaksL.size(); ++i) {
        if (peaksL[i] > 3.0f * std::max(1e-6f, peaksR[i]))
            leftDominates = true;
        if (peaksR[i] > 3.0f * std::max(1e-6f, peaksL[i]))
            rightDominates = true;
    }
    return leftDominates && rightDominates;
}

// Cutoff target at full depth: a high probe must fade as the filter sweeps
// down (6 octaves from 20 kHz reaches ~312 Hz) and return as it reopens.
bool testCutoffTargetSweeps() {
    AestraLFO lfo;
    setupFreeRunning(lfo, AestraLFO::kTargetCutoff, AestraLFO::kWaveSine, 1.0f);
    lfo.activate();

    const auto in = makeSine(48000, 8000.0f, 0.3f, kSampleRate);
    auto out = processStereo(lfo, in, in);
    const auto peaks = windowPeaks(out.left, 2400);
    const float maxP = *std::max_element(peaks.begin(), peaks.end());
    const float minP = *std::min_element(peaks.begin(), peaks.end());
    return maxP > 0.25f && minP < 0.3f * maxP;
}

// Sync mode: the tremolo rate must follow the pushed BPM (1/4 note at
// 120 BPM = 2 Hz, at 240 BPM = 4 Hz). Detected on the rectified envelope.
bool testSyncFollowsBpm() {
    auto envMagAt = [](float bpm, float probeHz) {
        AestraLFO lfo;
        lfo.initialize(kSampleRate, kBlockSize);
        lfo.setParameter(AestraLFO::kTarget, AestraLFO::normFromTarget(AestraLFO::kTargetVolume));
        lfo.setParameter(AestraLFO::kWave, AestraLFO::normFromWave(AestraLFO::kWaveSine));
        lfo.setParameter(AestraLFO::kSyncMode, 1.0f);
        lfo.setParameter(AestraLFO::kNoteDivision, AestraLFO::noteDivisionParamFromIndex(AestraLFO::kDiv1_4));
        lfo.setParameter(AestraLFO::kDepth, 1.0f);
        lfo.setParameter(AestraLFO::kSmooth, 0.0f);
        lfo.setBPM(bpm);
        lfo.activate();

        const auto in = makeSine(4 * kSampleRate, 1000.0f, 0.5f, kSampleRate);
        auto out = processStereo(lfo, in, in);
        std::vector<float> env(out.left.size());
        for (size_t i = 0; i < env.size(); ++i)
            env[i] = std::abs(out.left[i]);
        return goertzelMag(env, 2 * kSampleRate, probeHz, kSampleRate);
    };

    const float at120 = envMagAt(120.0f, 2.0f);
    const float at120Wrong = envMagAt(120.0f, 4.0f);
    const float at240 = envMagAt(240.0f, 4.0f);
    const float at240Wrong = envMagAt(240.0f, 2.0f);
    return at120 > 3.0f * at120Wrong && at240 > 3.0f * at240Wrong;
}

// Smoothing must remove the hard edges of a square LFO (click-free gating).
bool testSmoothingLimitsSlew() {
    auto maxDelta = [](float smoothNorm) {
        AestraLFO lfo;
        setupFreeRunning(lfo, AestraLFO::kTargetVolume, AestraLFO::kWaveSquare, 1.0f);
        lfo.setParameter(AestraLFO::kSmooth, smoothNorm);
        lfo.activate();

        const std::vector<float> in(48000, 0.5f); // DC probe isolates gain slew
        auto out = processStereo(lfo, in, in);
        float d = 0.0f;
        for (size_t i = 1; i < out.left.size(); ++i)
            d = std::max(d, std::abs(out.left[i] - out.left[i - 1]));
        return d;
    };

    const float hard = maxDelta(0.0f);
    const float soft = maxDelta(1.0f); // 200 ms
    return hard > 0.1f && soft < 0.02f;
}

// S&H must stay bounded: volume gain never exceeds unity.
bool testSampleHoldIsBounded() {
    AestraLFO lfo;
    setupFreeRunning(lfo, AestraLFO::kTargetVolume, AestraLFO::kWaveSH, 1.0f);
    lfo.setParameter(AestraLFO::kRateHz, 1.0f); // 20 Hz, many cycles
    lfo.activate();

    const auto in = makeSine(48000, 1000.0f, 0.5f, kSampleRate);
    auto out = processStereo(lfo, in, in);
    return allFinite(out.left) && peakAmplitude(out.left) <= 0.5f + 1e-4f;
}

bool testSurvivesHostileInput() {
    AestraLFO lfo;
    setupFreeRunning(lfo, AestraLFO::kTargetCutoff, AestraLFO::kWaveSquare, 1.0f);
    lfo.activate();

    std::vector<float> in(2048, 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = (i % 2 == 0) ? 100.0f : -100.0f;
    in[100] = std::numeric_limits<float>::quiet_NaN();
    in[200] = std::numeric_limits<float>::infinity();
    in[300] = -std::numeric_limits<float>::infinity();

    auto out = processStereo(lfo, in, in);
    return allFinite(out.left) && allFinite(out.right);
}

// Every parameter at both extremes with hot input: output must stay finite.
bool testStableAtParameterExtremes() {
    for (float extreme : {0.0f, 1.0f}) {
        AestraLFO lfo;
        lfo.initialize(kSampleRate, kBlockSize);
        for (uint32_t p = 0; p < AestraLFO::kParamCount; ++p) {
            if (p != AestraLFO::kBypass)
                lfo.setParameter(p, extreme);
        }
        lfo.activate();

        const auto in = makeSine(8192, 700.0f, 1.0f, kSampleRate);
        auto out = processStereo(lfo, in, in);
        if (!allFinite(out.left) || !allFinite(out.right))
            return false;
    }
    return true;
}

bool testStateRoundTrip() {
    AestraLFO lfo;
    lfo.initialize(kSampleRate, kBlockSize);
    lfo.activate();
    lfo.setParameter(AestraLFO::kTarget, AestraLFO::normFromTarget(AestraLFO::kTargetPan));
    lfo.setParameter(AestraLFO::kWave, AestraLFO::normFromWave(AestraLFO::kWaveTriangle));
    lfo.setParameter(AestraLFO::kSyncMode, 0.0f);
    lfo.setParameter(AestraLFO::kRateHz, 0.42f);
    lfo.setParameter(AestraLFO::kNoteDivision, AestraLFO::noteDivisionParamFromIndex(AestraLFO::kDiv1_8T));
    lfo.setParameter(AestraLFO::kDepth, 0.8f);
    lfo.setParameter(AestraLFO::kPhase, 0.25f);
    lfo.setParameter(AestraLFO::kSmooth, 0.6f);

    const auto state = lfo.saveState();

    AestraLFO restored;
    restored.initialize(kSampleRate, kBlockSize);
    restored.activate();
    if (!restored.loadState(state))
        return false;

    for (uint32_t i = 0; i < AestraLFO::kParamCount; ++i) {
        if (std::abs(restored.getParameter(i) - lfo.getParameter(i)) > 1e-6f)
            return false;
    }
    return true;
}

bool testLoadStateRejectsGarbage() {
    AestraLFO lfo;
    lfo.initialize(kSampleRate, kBlockSize);
    lfo.activate();

    std::vector<uint8_t> tooShort = {0x31, 0x4F};
    if (lfo.loadState(tooShort))
        return false;

    std::vector<uint8_t> wrongMagic(64, 0);
    if (lfo.loadState(wrongMagic))
        return false;
    return true;
}

bool testEnumAndDivisionMapping() {
    if (AestraLFO::targetFromNorm(0.0f) != AestraLFO::kTargetVolume)
        return false;
    if (AestraLFO::targetFromNorm(0.5f) != AestraLFO::kTargetPan)
        return false;
    if (AestraLFO::targetFromNorm(1.0f) != AestraLFO::kTargetCutoff)
        return false;
    if (AestraLFO::waveFromNorm(0.0f) != AestraLFO::kWaveSine)
        return false;
    if (AestraLFO::waveFromNorm(1.0f) != AestraLFO::kWaveSH)
        return false;
    for (int i = 0; i <= 12; ++i) {
        if (AestraLFO::noteDivisionIndexFromParam(AestraLFO::noteDivisionParamFromIndex(i)) != i)
            return false;
    }
    // 1/4 = one beat; the table matches AestraDelay's.
    return std::abs(AestraLFO::noteDivisionMultiplier(AestraLFO::kDiv1_4) - 1.0f) < 1e-6f &&
           std::abs(AestraLFO::noteDivisionMultiplier(AestraLFO::kDiv1_1) - 4.0f) < 1e-6f;
}

// NaN/Inf must not pass the parameter ingress; a NaN-filled (magic-intact)
// state blob must not poison processing.
bool testSetParameterRejectsNonFinite() {
    AestraLFO lfo;
    lfo.initialize(kSampleRate, kBlockSize);
    lfo.activate();
    lfo.setParameter(AestraLFO::kDepth, 0.42f);

    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()}) {
        lfo.setParameter(AestraLFO::kDepth, bad);
        if (std::abs(lfo.getParameter(AestraLFO::kDepth) - 0.42f) > 1e-6f)
            return false;
    }

    std::vector<uint8_t> state = lfo.saveState();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t off = 8; off + sizeof(float) <= state.size(); off += sizeof(float)) {
        std::memcpy(state.data() + off, &nan, sizeof(float));
    }
    if (lfo.loadState(state))
        return false; // an all-NaN blob must be rejected, not accepted as success
    for (uint32_t i = 0; i < AestraLFO::kParamCount; ++i) {
        if (!std::isfinite(lfo.getParameter(i)))
            return false;
    }

    const auto in = makeSine(2048, 1000.0f, 0.5f, kSampleRate);
    auto out = processStereo(lfo, in, in);
    return allFinite(out.left) && allFinite(out.right);
}

// Rapid automation of every continuous parameter (incl. target/wave/sync
// switching), with hostile block sizes.
bool testAutomationStress() {
    AestraLFO lfo;
    lfo.initialize(kSampleRate, kBlockSize);
    lfo.activate();
    lfo.setBPM(128.0f);

    uint32_t lcg = 0x5EED1234u;
    auto next01 = [&lcg]() {
        lcg = lcg * 1664525u + 1013904223u;
        return static_cast<float>(lcg >> 8) * (1.0f / 16777216.0f);
    };

    const auto in = makeSine(96000, 400.0f, 0.5f, kSampleRate); // 2 s
    std::vector<float> outL(in.size(), 0.0f);
    std::vector<float> outR(in.size(), 0.0f);
    const uint32_t sizes[] = {1, 3, 17, 32, 64, 111};
    uint32_t pos = 0;
    uint32_t sizeIdx = 0;
    while (pos < in.size()) {
        for (uint32_t pIdx = 0; pIdx < AestraLFO::kParamCount; ++pIdx) {
            if (pIdx != AestraLFO::kBypass)
                lfo.setParameter(pIdx, next01());
        }
        const uint32_t n = std::min<uint32_t>(sizes[sizeIdx % 6], static_cast<uint32_t>(in.size()) - pos);
        ++sizeIdx;
        const float* ins[] = {in.data() + pos, in.data() + pos};
        float* outs[] = {outL.data() + pos, outR.data() + pos};
        lfo.process(ins, outs, 2, 2, n);
        pos += n;
    }

    if (!allFinite(outL) || !allFinite(outR))
        return false;
    return peakAmplitude(outL) < 4.0f; // gain/pan/cutoff never boost above unity
}

bool testStableAcrossSampleRates() {
    for (double sr : {44100.0, 96000.0, 192000.0}) {
        AestraLFO lfo;
        lfo.initialize(sr, kBlockSize);
        lfo.setParameter(AestraLFO::kTarget, AestraLFO::normFromTarget(AestraLFO::kTargetCutoff));
        lfo.setParameter(AestraLFO::kDepth, 1.0f);
        lfo.setParameter(AestraLFO::kRateHz, 1.0f); // 20 Hz
        lfo.activate();

        const auto in = makeSine(8192, 1000.0f, 0.5f, sr);
        auto out = processStereo(lfo, in, in);
        if (!allFinite(out.left) || !allFinite(out.right))
            return false;
        if (peakAmplitude(out.left) > 16.0f)
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
        {"DepthZeroIsTransparent", testDepthZeroIsTransparent},
        {"VolumeTargetTremolo", testVolumeTargetTremolo},
        {"PanTargetAlternatesChannels", testPanTargetAlternatesChannels},
        {"CutoffTargetSweeps", testCutoffTargetSweeps},
        {"SyncFollowsBpm", testSyncFollowsBpm},
        {"SmoothingLimitsSlew", testSmoothingLimitsSlew},
        {"SampleHoldIsBounded", testSampleHoldIsBounded},
        {"SurvivesHostileInput", testSurvivesHostileInput},
        {"StableAtParameterExtremes", testStableAtParameterExtremes},
        {"StateRoundTrip", testStateRoundTrip},
        {"LoadStateRejectsGarbage", testLoadStateRejectsGarbage},
        {"EnumAndDivisionMapping", testEnumAndDivisionMapping},
        {"SetParameterRejectsNonFinite", testSetParameterRejectsNonFinite},
        {"AutomationStress", testAutomationStress},
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
    std::cout << "All AestraLFO tests passed\n";
    return 0;
}
