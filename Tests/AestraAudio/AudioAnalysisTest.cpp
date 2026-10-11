// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The analysis service (V8-S8) against signals whose answers are published:
// EBU Tech 3341 (loudness, gating) and Tech 3342 (loudness range) cases built
// from 1 kHz stereo sines, an intersample peak for true peak, and simple
// signals for correlation and band energy. Then the cache: computed once.

#include "Analysis/AudioAnalysis.h"
#include "DSP/KWeighting.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

void checkNear(double actual, double expected, double tol, const char* message) {
    if (std::fabs(actual - expected) > tol) {
        std::printf("[FAIL] %s: expected %.3f ±%.3f, got %.3f\n", message, expected, tol, actual);
        ++g_failures;
    }
}

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kRate = 48000;

// Stereo 1 kHz sine, the same on both channels, in segments of (seconds, peak dBFS).
std::vector<float> segments(const std::vector<std::pair<double, double>>& parts, uint32_t rate = kRate,
                            double hz = 1000.0) {
    std::vector<float> out;
    uint64_t n = 0;
    for (const auto& [seconds, dbfs] : parts) {
        const double amp = std::pow(10.0, dbfs / 20.0);
        const auto frames = static_cast<uint64_t>(seconds * rate);
        for (uint64_t f = 0; f < frames; ++f, ++n) {
            const auto x = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(n) / rate));
            out.push_back(x);
            out.push_back(x);
        }
    }
    return out;
}

void testTech3341() {
    // Cases 1 and 2: a steady stereo sine at -23 / -33 dBFS reads -23 / -33 LUFS.
    {
        const AudioAnalysis a = analyzeAudio(segments({{20.0, -23.0}}), 2, kRate);
        checkNear(a.integratedLufs, -23.0, 0.1, "3341-1 integrated");
        checkNear(a.maxMomentaryLufs, -23.0, 0.1, "3341-1 momentary");
        checkNear(a.maxShortTermLufs, -23.0, 0.1, "3341-1 short-term");
    }
    checkNear(analyzeAudio(segments({{20.0, -33.0}}), 2, kRate).integratedLufs, -33.0, 0.1, "3341-2 integrated");
    // Case 3: the relative gate drops the quiet ends.
    checkNear(analyzeAudio(segments({{10.0, -36.0}, {60.0, -23.0}, {10.0, -36.0}}), 2, kRate).integratedLufs, -23.0,
              0.1, "3341-3 relative gate");
    // Case 4: the absolute gate drops -72, the relative gate the -36.
    checkNear(analyzeAudio(segments({{10.0, -72.0}, {10.0, -36.0}, {60.0, -23.0}, {10.0, -36.0}, {10.0, -72.0}}), 2,
                           kRate)
                  .integratedLufs,
              -23.0, 0.1, "3341-4 absolute + relative gate");
    // Case 5: both levels pass the gate and average.
    checkNear(analyzeAudio(segments({{20.0, -26.0}, {20.1, -20.0}, {20.0, -26.0}}), 2, kRate).integratedLufs, -23.0,
              0.1, "3341-5 gated mean");
}

void testTech3342() {
    checkNear(analyzeAudio(segments({{20.0, -20.0}, {20.0, -30.0}}), 2, kRate).loudnessRangeLu, 10.0, 1.0,
              "3342-1 range 10");
    checkNear(analyzeAudio(segments({{20.0, -20.0}, {20.0, -15.0}}), 2, kRate).loudnessRangeLu, 5.0, 1.0,
              "3342-2 range 5");
    checkNear(analyzeAudio(segments({{20.0, -40.0}, {20.0, -20.0}}), 2, kRate).loudnessRangeLu, 20.0, 1.0,
              "3342-3 range 20");
    checkNear(analyzeAudio(segments({{20.0, -50.0}, {20.0, -35.0}, {20.0, -20.0}, {20.0, -35.0}, {20.0, -50.0}}), 2,
                           kRate)
                  .loudnessRangeLu,
              15.0, 1.0, "3342-4 range 15 (the -50s fall under the relative gate)");
    check(analyzeAudio(segments({{20.0, -23.0}}), 2, kRate).loudnessRangeLu < 0.1, "a steady tone has no range");
}

void testOtherRatesAndMono() {
    // K-weighting is computed per rate, so a tone reads the same at any rate.
    for (uint32_t rate : {44100u, 96000u}) {
        checkNear(analyzeAudio(segments({{10.0, -23.0}}, rate), 2, rate).integratedLufs, -23.0, 0.1,
                  "the same tone at another rate reads the same");
    }
    // Mono is one channel (BS.1770), so 3 dB under the same tone in stereo.
    std::vector<float> stereo = segments({{10.0, -23.0}});
    std::vector<float> mono(stereo.size() / 2);
    for (size_t f = 0; f < mono.size(); ++f) mono[f] = stereo[f * 2];
    const AudioAnalysis m = analyzeAudio(mono, 1, kRate);
    checkNear(m.integratedLufs, -23.0 - 3.0103, 0.1, "mono measures one channel");
    checkNear(m.correlation, 1.0, 1e-12, "mono correlates with itself");
    // 48 kHz uses the reference coefficients (computed or by fallback).
    const KWeighting::Biquad pre = KWeighting::preFilter(48000.0);
    checkNear(pre.b0, KWeighting::kPreFilter48k.b0, 1e-9, "48 kHz pre-filter is the reference");
    const KWeighting::Biquad rlb = KWeighting::rlbHighPass(48000.0);
    checkNear(rlb.a1, KWeighting::kRlb48k.a1, 1e-9, "48 kHz RLB is the reference");
    // Just off 48 kHz the fallback cannot apply, so this pins the formula itself:
    // the engine's old 10^(G/40) shelf missed b0 by 0.3 and metered other rates ~1 dB low.
    const KWeighting::Biquad computed = KWeighting::preFilter(48000.0001);
    checkNear(computed.b0, KWeighting::kPreFilter48k.b0, 1e-6, "the computed shelf reproduces the reference");
    checkNear(computed.b1, KWeighting::kPreFilter48k.b1, 1e-6, "(b1)");
    checkNear(KWeighting::rlbHighPass(48000.0001).a2, KWeighting::kRlb48k.a2, 1e-8,
              "the computed RLB reproduces the reference");
}

void testSurroundWeights() {
    // 5.1 is L R C LFE Ls Rs: the LFE is not measured, a surround weighs +1.5 dB.
    const std::vector<float> stereo = segments({{10.0, -23.0}});
    auto onChannel = [&](uint32_t channel) {
        std::vector<float> six(stereo.size() / 2 * 6, 0.0f);
        for (size_t f = 0; f < stereo.size() / 2; ++f) six[f * 6 + channel] = stereo[f * 2];
        return analyzeAudio(six, 6, kRate);
    };
    checkNear(onChannel(0).integratedLufs, -23.0 - 3.0103, 0.1, "a front channel weighs 1");
    check(onChannel(3).integratedLufs == AudioAnalysis::kSilenceDb, "the LFE is not measured");
    checkNear(onChannel(4).integratedLufs, -23.0 - 3.0103 + 10.0 * std::log10(1.41), 0.1, "a surround weighs +1.5 dB");
}

void testTruePeak() {
    // A full-scale sine at fs/4 sampled 45 degrees off its crests: every sample is
    // ±0.707 (-3 dBFS), but the reconstructed wave reaches 0 dBTP between them.
    std::vector<float> v;
    for (int n = 0; n < kRate; ++n) {
        const auto x = static_cast<float>(std::sin(kPi / 2.0 * n + kPi / 4.0));
        v.push_back(x);
        v.push_back(x);
    }
    const AudioAnalysis a = analyzeAudio(v, 2, kRate);
    checkNear(a.samplePeakDbfs, -3.0103, 0.01, "the samples peak at -3 dBFS");
    checkNear(a.truePeakDbtp, 0.0, 0.5, "the true peak is the wave's, 0 dBTP");
    check(a.truePeakDbtp >= a.samplePeakDbfs, "true peak is never under sample peak");
}

void testCorrelation() {
    const std::vector<float> same = segments({{2.0, -12.0}});
    checkNear(analyzeAudio(same, 2, kRate).correlation, 1.0, 1e-9, "identical channels correlate +1");

    std::vector<float> inverted = same;
    for (size_t i = 1; i < inverted.size(); i += 2) inverted[i] = -inverted[i];
    checkNear(analyzeAudio(inverted, 2, kRate).correlation, -1.0, 1e-9, "inverted channels correlate -1");

    std::vector<float> different(same.size());
    for (size_t f = 0; f < same.size() / 2; ++f) {
        different[f * 2] = static_cast<float>(0.25 * std::sin(2.0 * kPi * 440.0 * f / kRate));
        different[f * 2 + 1] = static_cast<float>(0.25 * std::sin(2.0 * kPi * 1330.0 * f / kRate));
    }
    checkNear(analyzeAudio(different, 2, kRate).correlation, 0.0, 0.02, "unrelated tones are uncorrelated");

    std::vector<float> oneSide = same;
    for (size_t i = 1; i < oneSide.size(); i += 2) oneSide[i] = 0.0f;
    checkNear(analyzeAudio(oneSide, 2, kRate).correlation, 0.0, 1e-12, "a silent side reads 0");
}

void testBands() {
    struct Case {
        double hz;
        AudioAnalysis::Band band;
        const char* message;
    };
    const Case cases[] = {
        {60.0, AudioAnalysis::Low, "60 Hz lands in Low"},
        {250.0, AudioAnalysis::LowMid, "250 Hz lands in LowMid"},
        {1000.0, AudioAnalysis::Mid, "1 kHz lands in Mid"},
        {3500.0, AudioAnalysis::HighMid, "3.5 kHz lands in HighMid"},
        {12000.0, AudioAnalysis::High, "12 kHz lands in High"},
    };
    for (const Case& c : cases) {
        const AudioAnalysis a = analyzeAudio(segments({{2.0, -6.0}}, kRate, c.hz), 2, kRate);
        check(a.bandShare[c.band] > 0.85, c.message);
        double sum = 0.0;
        for (double s : a.bandShare) sum += s;
        checkNear(sum, 1.0, 1e-9, "band shares sum to 1");
        check(a.bandRmsDbfs[c.band] > -12.0 && a.bandRmsDbfs[c.band] < -8.0,
              "the band's RMS is the tone's (-6 dBFS peak is -9 dBFS RMS)");
    }
    // At 8 kHz the top edges are past Nyquist: those bands stay empty, nothing breaks.
    const AudioAnalysis low = analyzeAudio(segments({{2.0, -6.0}}, 8000, 1000.0), 2, 8000);
    check(low.bandShare[AudioAnalysis::High] == 0.0, "a band above Nyquist is empty");
    check(low.bandShare[AudioAnalysis::Mid] > 0.85, "and the tone still lands where it should");
}

void testSilenceAndBrokenInput() {
    const AudioAnalysis silent = analyzeAudio(std::vector<float>(kRate * 2 * 5, 0.0f), 2, kRate);
    check(silent.integratedLufs == AudioAnalysis::kSilenceDb, "silence has no integrated loudness");
    check(silent.maxMomentaryLufs == AudioAnalysis::kSilenceDb, "nor momentary");
    check(silent.truePeakDbtp == AudioAnalysis::kSilenceDb, "nor a peak");
    check(silent.correlation == 0.0 && silent.loudnessRangeLu == 0.0, "nor correlation or range");

    const AudioAnalysis tooShort = analyzeAudio(segments({{0.2, -23.0}}), 2, kRate);
    check(tooShort.integratedLufs == AudioAnalysis::kSilenceDb, "under one 400 ms block, nothing is gated in");
    check(tooShort.shortTermLufs.empty(), "and there is no short-term value");
    check(tooShort.samplePeakDbfs > -24.0, "but the peak is still measured");

    std::vector<float> withNan = segments({{10.0, -23.0}});
    withNan[1000] = std::numeric_limits<float>::quiet_NaN();
    withNan[2001] = std::numeric_limits<float>::infinity();
    const AudioAnalysis a = analyzeAudio(withNan, 2, kRate);
    checkNear(a.integratedLufs, -23.0, 0.1, "a NaN or infinity does not poison the measurement");
    check(std::isfinite(a.truePeakDbtp), "nor the true peak");

    check(analyzeAudio(nullptr, 0, 2, kRate).frames == 0, "no input is an empty analysis");
}

void testShortTermSeries() {
    const AudioAnalysis a = analyzeAudio(segments({{10.0, -23.0}}), 2, kRate);
    // 100 hops in 10 s; the first short-term value needs 30 of them.
    check(a.shortTermLufs.size() == 71, "one short-term value per 100 ms once 3 s are in");
    checkNear(a.shortTermLufs.front(), -23.0, 0.1, "and each reads the tone");
}

void testCache() {
    AudioAnalysisCache cache(2);
    int computed = 0;
    const std::vector<float> tone = segments({{1.0, -12.0}});
    auto compute = [&] {
        ++computed;
        return analyzeAudio(tone, 2, kRate);
    };
    const std::string key = AudioAnalysisCache::renderKey(0x1234abcdULL, kRate, 2);
    const auto first = cache.getOrCompute(key, compute);
    const auto second = cache.getOrCompute(key, compute);
    check(computed == 1, "an analysis is computed once");
    check(first == second && first != nullptr, "and the same result is handed back");
    check(cache.find("missing") == nullptr, "find does not compute");

    cache.getOrCompute(AudioAnalysisCache::renderKey(2, kRate, 2), compute);
    cache.getOrCompute(AudioAnalysisCache::renderKey(3, kRate, 2), compute);
    check(cache.size() == 2, "the cache holds its capacity");
    check(cache.find(key) == nullptr, "and lets the oldest go first");

    check(AudioAnalysisCache::fileKey("/a.wav", 10, 1) != AudioAnalysisCache::fileKey("/a.wav", 10, 2),
          "an edited file (new mtime) is a new key");
    check(AudioAnalysisCache::renderKey(1, 44100, 2) != AudioAnalysisCache::renderKey(1, 48000, 2),
          "the same samples at another rate are a new key");
    check(AudioAnalysisCache::renderKey(0x1234abcdULL, kRate, 2) == "render:000000001234abcd:48000:2",
          "render keys spell the hash the way a golden does");
    cache.clear();
    check(cache.size() == 0, "clear empties it");
}

} // namespace

int main() {
    testTech3341();
    testTech3342();
    testOtherRatesAndMono();
    testSurroundWeights();
    testTruePeak();
    testCorrelation();
    testBands();
    testSilenceAndBrokenInput();
    testShortTermSeries();
    testCache();
    if (g_failures) {
        std::printf("AudioAnalysisTest: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("AudioAnalysisTest: all passed\n");
    return 0;
}
