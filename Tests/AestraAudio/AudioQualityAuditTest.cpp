// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AudioQualityAuditTest — End-to-end audio quality measurement.
//
// Measures:
//   1. Noise floor — silence through full engine path (RMS, DC offset)
//   2. Distortion — null-test residuals for multiple frequencies/amplitudes
//   3. Frequency response — magnitude at 20Hz–20kHz
//   4. Channel crosstalk — L-only signal, R-channel leakage
//   5. Soft clipper transfer curve — mathematical verification
//   6. DC blocker frequency response
//
// Question answered: "Where does Aestra lose information?"

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                   \
    if (cond) {                                  \
        std::printf("  PASS  %s\n", msg);        \
        ++g_pass;                                \
    } else {                                     \
        std::printf("  FAIL  %s\n", msg);        \
        ++g_fail;                                \
    }                                            \
} while(0)

static constexpr double kTau = 6.28318530717958647692;

// Render a TrackManager-based sine source through the engine, return output
static std::vector<float> renderSine(
    uint32_t sampleRate, uint32_t durationFrames, uint32_t blockSize,
    double freqHz, double amplitudeLin, bool leftOnly = false)
{
    auto buf = std::make_shared<AudioBufferData>();
    buf->sampleRate = sampleRate;
    buf->numChannels = 2;
    buf->numFrames = durationFrames;
    buf->interleavedData.resize(static_cast<size_t>(durationFrames) * 2);
    for (uint32_t i = 0; i < durationFrames; ++i) {
        double s = amplitudeLin * std::sin(kTau * freqHz * i / sampleRate);
        buf->interleavedData[i * 2] = static_cast<float>(s);
        buf->interleavedData[i * 2 + 1] = leftOnly ? 0.0f : static_cast<float>(s);
    }

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(sampleRate));
    auto* channel = tm->addChannel("QualityTest");

    // Portable temp path with a per-run unique name (avoids collisions in
    // parallel test runs; the source is in-memory, path is metadata only)
    static const std::string path =
        (std::filesystem::temp_directory_path() /
         ("aestra_quality_audit_src_" +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
          ".wav")).string();
    ClipSourceID srcId = tm->getSourceManager().createRecordedSource(
        path, "quality_src", buf);
    double durationSec = static_cast<double>(durationFrames) / sampleRate;
    AudioSlicePayload payload;
    payload.audioSourceId = srcId;
    payload.durationSeconds = durationSec;
    payload.slices.push_back(
        {0.0, durationSec, 0.0, static_cast<double>(durationFrames)});

    PlaylistLaneID laneId = tm->getPlaylistModel().createLane("quality");
    PatternID patId = tm->getPatternManager().createAudioPattern(
        "quality_pat", durationSec * 2.0, payload);
    if (channel) {
        tm->getPatternManager().setPatternMixerChannel(patId, channel->getChannelId());
    }
    const ClipInstanceID clipId = tm->getPlaylistModel().addClipFromPattern(laneId, patId, 0.0, durationSec * 2.0);
    if (!tm->getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
        CHECK(false, "Quality fixture uses unity clip gain");
        return {};
    }

    AudioEngine eng;
    eng.setSampleRate(sampleRate);
    eng.setBufferConfig(blockSize, 2);
    eng.setTrackManager(tm);
    eng.setBPM(120.0f);
    eng.setSafetyLimiterEnabled(false);
    eng.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    eng.initialize();
    eng.setMetronomeEnabled(false);
    eng.setAuditionModeEnabled(false);
    eng.setGlobalSamplePos(0);
    eng.setTransportPlaying(true);

    std::vector<float> output;
    std::vector<float> block(blockSize * 2);
    for (uint32_t rendered = 0; rendered < durationFrames; ) {
        uint32_t f = std::min(blockSize, durationFrames - rendered);
        std::fill(block.begin(), block.end(), 0.0f);
        eng.processBlock(block.data(), nullptr, f, 0.0);
        output.insert(output.end(), block.begin(),
                      block.begin() + static_cast<ptrdiff_t>(f * 2));
        rendered += f;
    }
    eng.setTransportPlaying(false);
    return output;
}

// RMS over a range in a single channel (ch = 0 for L, 1 for R)
static double channelRmsDb(const std::vector<float>& data,
                           uint32_t ch, uint32_t numChannels,
                           size_t startSample, size_t endSample)
{
    double sumSq = 0.0;
    size_t count = 0;
    for (size_t i = startSample * numChannels + ch;
         i < endSample * numChannels;
         i += numChannels) {
        double s = static_cast<double>(data[i]);
        sumSq += s * s;
        ++count;
    }
    if (count == 0) return -INFINITY;
    return 10.0 * std::log10(sumSq / static_cast<double>(count));
}

// ---------------------------------------------------------------------------
// 1. Noise floor
// ---------------------------------------------------------------------------
static void testNoiseFloor() {
    std::printf("\n[1. Noise floor]\n");

    AudioEngine eng;
    eng.setSampleRate(48000);
    eng.setBufferConfig(512, 2);
    eng.setSafetyLimiterEnabled(false);
    eng.initialize();

    std::vector<float> out(512 * 2);
    uint32_t warmupBlocks = 48000 / 512;
    uint32_t measureBlocks = warmupBlocks;

    for (uint32_t i = 0; i < warmupBlocks; ++i)
        eng.processBlock(out.data(), nullptr, 512, 0.0);

    double noiseSumSq = 0.0;
    uint64_t totalSamples = 0;
    double dcSum = 0.0;
    for (uint32_t i = 0; i < measureBlocks; ++i) {
        eng.processBlock(out.data(), nullptr, 512, 0.0);
        for (uint32_t j = 0; j < 512 * 2; ++j) {
            double s = static_cast<double>(out[j]);
            noiseSumSq += s * s;
            dcSum += s;
        }
        totalSamples += 512 * 2;
    }

    double rms = 10.0 * std::log10(noiseSumSq / static_cast<double>(totalSamples));
    double dc = dcSum / static_cast<double>(totalSamples);

    std::printf("    RMS noise floor: %.1f dBFS\n", rms);
    std::printf("    DC offset: %.2e\n", dc);

    CHECK(rms < -120.0, "Noise floor below -120 dBFS");
    CHECK(std::abs(dc) < 1e-8, "DC offset negligible");
}

// ---------------------------------------------------------------------------
// 2. Distortion: null-test residual across multiple frequencies/amplitudes
// ---------------------------------------------------------------------------
static void testDistortionNull() {
    std::printf("\n[2. Distortion null test]\n");

    const uint32_t sampleRate = 48000;
    const uint32_t blockSize = 512;
    const uint32_t duration = 2;
    const uint32_t totalFrames = sampleRate * duration;

    struct TestPoint {
        double freq;
        double ampDb;
        double thresholdDb; // max acceptable null error
    };

    TestPoint tests[] = {
        {100.0,   -6.0, -90.0},
        {440.0,   -6.0, -90.0},
        {1000.0,  -6.0, -90.0},
        {5000.0,  -6.0, -80.0},
        {100.0,  -40.0, -80.0},
        {1000.0, -40.0, -80.0},
    };

    for (auto& t : tests) {
        double ampLin = std::pow(10.0, t.ampDb / 20.0);
        // Build a reference sine with the expected pan-law scaling
        // (cos(pi/4) per channel at center pan)
        constexpr double kPanLawCenterGain = 0.7071067811865476;
        double refAmp = ampLin * kPanLawCenterGain;

        std::vector<float> output = renderSine(
            sampleRate, totalFrames, blockSize, t.freq, ampLin);

        // Compute null residual against expected
        size_t trimStart = blockSize; // skip gain ramp
        size_t trimEnd = totalFrames - 256; // skip clip-edge fade
        double rmsSumSq = 0.0;
        double sigSumSq = 0.0;
        uint64_t count = 0;
        for (size_t i = trimStart * 2; i < trimEnd * 2; i += 2) {
            double expected = refAmp * std::sin(
                kTau * t.freq * (i / 2) / sampleRate);
            double actual = static_cast<double>(output[i]);
            double diff = actual - expected;
            rmsSumSq += diff * diff;
            sigSumSq += actual * actual;
            ++count;
        }
        double nullDb = (sigSumSq > 0.0 && count > 0)
            ? 10.0 * std::log10(rmsSumSq / sigSumSq)
            : -INFINITY;

        std::printf("    %5.0f Hz @ %5.1f dBFS: null = %.1f dB\n",
                    t.freq, t.ampDb, nullDb);
        CHECK(nullDb < t.thresholdDb,
              (std::to_string((int)t.freq) + " Hz @ " +
               std::to_string((int)t.ampDb) + " dBFS null").c_str());
    }
}

// ---------------------------------------------------------------------------
// 3. Frequency response
// ---------------------------------------------------------------------------
static void testFrequencyResponse() {
    std::printf("\n[3. Frequency response]\n");

    const uint32_t sampleRate = 48000;
    const uint32_t blockSize = 512;
    const uint32_t durationFrames = sampleRate / 2; // 0.5s
    const double testFreqs[] = {20.0, 100.0, 440.0, 1000.0, 5000.0,
                                10000.0, 16000.0, 20000.0};
    const double refAmp = 0.5; // -6 dBFS
    constexpr double kPanLawCenterGain = 0.7071067811865476;
    double expectedRmsDb = 10.0 * std::log10(
        (refAmp * kPanLawCenterGain) * (refAmp * kPanLawCenterGain) / 2.0);

    for (double freq : testFreqs) {
        std::vector<float> output = renderSine(
            sampleRate, durationFrames, blockSize, freq, refAmp);

        // Measure RMS in last quarter (after transients settle)
        size_t measureStart = durationFrames * 3 / 4;
        double outRmsDb = channelRmsDb(output, 0, 2, measureStart, durationFrames);
        double error = outRmsDb - expectedRmsDb;

        std::printf("    %6.0f Hz: output %.1f dBFS (error %.1f dB)\n",
                    freq, outRmsDb, error);
        double tolerance = (freq <= 100.0 || freq >= 16000.0) ? 2.0 : 1.0;
        CHECK(std::abs(error) < tolerance,
              (std::to_string((int)freq) + " Hz magnitude").c_str());
    }
}

// ---------------------------------------------------------------------------
// 4. Channel crosstalk
// ---------------------------------------------------------------------------
static void testChannelCrosstalk() {
    std::printf("\n[4. Channel crosstalk]\n");

    const uint32_t sampleRate = 48000;
    const uint32_t blockSize = 512;
    const uint32_t durationFrames = sampleRate; // 1s
    const double refAmp = 0.5;

    // Feed signal to left only
    std::vector<float> output = renderSine(
        sampleRate, durationFrames, blockSize, 1000.0, refAmp, true);

    size_t measureStart = blockSize;
    size_t measureEnd = durationFrames - 256;

    double lRmsDb = channelRmsDb(output, 0, 2, measureStart, measureEnd);
    double rRmsDb = channelRmsDb(output, 1, 2, measureStart, measureEnd);

    std::printf("    L RMS: %.1f dBFS, R RMS: %.1f dBFS\n", lRmsDb, rRmsDb);
    CHECK(rRmsDb < -100.0, "Channel crosstalk <-100 dB (L-only signal)");
    CHECK(lRmsDb > -20.0, "Left channel has signal");
}

// ---------------------------------------------------------------------------
// 5. Soft clipper verification
// ---------------------------------------------------------------------------
static void testSoftClipper() {
    std::printf("\n[5. Soft clipper]\n");
    // NOTE: softClipD is defined in AudioEngine.h but unused in processBlock.
    // Tests document the transfer function for when/if it is connected.

    auto softClip = [](double x) -> double {
        if (x > 1.5) return 1.0;
        if (x < -1.5) return -1.0;
        double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    };

    // Transfer curve spot checks
    double s0 = softClip(0.0);
    double s5 = softClip(0.5);  // computed: 0.5 * 27.25/29.25 = 0.4658
    double s8 = softClip(0.8);  // computed: 0.8 * 27.64/32.76 = 0.675
    double s1 = softClip(1.0);  // computed: 1.0 * 28.0/36.0 = 0.777...
    double s2 = softClip(2.0);

    CHECK(s0 == 0.0, "softClip(0) = 0");
    CHECK(std::abs(s5 - 0.4658) < 1e-4, "softClip(0.5) = 0.466 (no unity region)");
    CHECK(std::abs(s8 - 0.675) < 1e-3, "softClip(0.8) = 0.675");
    CHECK(std::abs(s1 - 0.7778) < 1e-3, "softClip(1.0) = 0.778");
    CHECK(s2 == 1.0, "softClip(2.0) = 1.0");
    CHECK(softClip(-2.0) == -1.0, "softClip(-2.0) = -1.0");

    // Monotonic
    bool monotonic = true;
    for (int i = 0; i < 1000; ++i) {
        double x1 = (i / 1000.0) * 2.0 - 0.5;
        double x2 = ((i + 1) / 1000.0) * 2.0 - 0.5;
        if (softClip(x2) < softClip(x1)) { monotonic = false; break; }
    }
    CHECK(monotonic, "softClip monotonic");

    std::printf("    Note: softClipD is defined but not connected in processBlock.\n");
    std::printf("    Transfer: 0.0->%.4f, 0.5->%.4f, 0.8->%.4f, 1.0->%.4f, 2.0->%.4f\n",
                s0, s5, s8, s1, s2);
}

// ---------------------------------------------------------------------------
// 6. DC blocker
// ---------------------------------------------------------------------------
static void testDCBlocker() {
    std::printf("\n[6. DC blocker]\n");
    // NOTE: DCBlockerD is defined in AudioEngine.h but unused in processBlock.

    constexpr double kR = 0.9997;
    struct DCBlocker {
        double x1 = 0.0, y1 = 0.0;
        double process(double x) {
            double y = x - x1 + kR * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    // Step response: after 50000 samples, near-zero
    // R=0.9997 => time constant ~3333 samples; 50000 = ~15 time constants
    DCBlocker blk;
    double y = 0.0;
    for (int i = 0; i < 50000; ++i) y = blk.process(1.0);
    CHECK(std::abs(y) < 0.001, "DC blocker suppresses steady DC to <0.001");

    // 1kHz passes with <1% attenuation
    blk = DCBlocker{};
    double maxAtten = 0.0;
    for (int i = 0; i < 4800; ++i) {
        double in = 0.5 * std::sin(kTau * 1000.0 * i / 48000.0);
        double out = blk.process(in);
        double atten = std::abs(out) / 0.5;
        if (atten > maxAtten) maxAtten = atten;
    }
    CHECK(maxAtten > 0.99, "DC blocker passes 1kHz with >0.99 peak ratio");

    // 20Hz: RMS gain should be very close to unity
    // H(z) = (1 - z^-1) / (1 - R * z^-1); at 20Hz/48kHz gain >0.99
    blk = DCBlocker{};
    double inSumSq = 0.0, outSumSq = 0.0;
    int warmup = 100; // skip startup transient
    for (int i = 0; i < 4800; ++i) {
        double in = 0.5 * std::sin(kTau * 20.0 * i / 48000.0);
        double out = blk.process(in);
        if (i >= warmup) {
            inSumSq += in * in;
            outSumSq += out * out;
        }
    }
    double rmsGain = (inSumSq > 0.0) ? std::sqrt(outSumSq / inSumSq) : 0.0;
    CHECK(rmsGain > 0.95, "DC blocker passes 20Hz with >0.95 RMS gain");
    std::printf("    Note: DCBlockerD defined but NOT connected in processBlock.\n");
    std::printf("    20Hz RMS gain: %.4f (R=%.4f)\n", rmsGain, kR);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::printf("============================================================\n");
    std::printf("  Aestra Audio — End-to-End Quality Audit\n");
    std::printf("============================================================\n");

    testNoiseFloor();
    testDistortionNull();
    testFrequencyResponse();
    testChannelCrosstalk();
    testSoftClipper();
    testDCBlocker();

    std::printf("\n============================================================\n");
    std::printf("  Results: %d/%d passed, %d failed\n",
                g_pass, g_pass + g_fail, g_fail);
    std::printf("============================================================\n");

    return g_fail > 0 ? 1 : 0;
}
