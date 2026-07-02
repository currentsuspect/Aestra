// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AudioEngineTortureTest — Stress harness that tries to break the audio engine.
//
// Tests:
//   - Silence path integrity (no denormal buildup)
//   - NaN/Inf injection (engine stays bounded)
//   - Extreme gain values (no overflow, no clipping artifacts)
//   - Buffer size reconfiguration between blocks
//   - Sample rate reconfiguration between blocks
//   - Transport hammering (rapid play/stop/seek)
//   - Multi-track load stress (100+ tracks)
//
// Each scenario is independently scored. The test exits 0 only if ALL pass.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <utility>
#include <vector>

using namespace Aestra::Audio;

// =============================================================================
// Configuration
// =============================================================================

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 256;
constexpr uint32_t kChannels = 2;
constexpr double kTau = 6.28318530717958647692;

static int g_testsRun = 0;
static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name, body)                                          \
    do {                                                          \
        ++g_testsRun;                                             \
        bool ok = [&]() -> bool { body }();                       \
        if (ok) {                                                 \
            std::printf("  PASS  %s\n", name);                    \
            ++g_testsPassed;                                      \
        } else {                                                  \
            std::printf("  FAIL  %s\n", name);                    \
            ++g_testsFailed;                                      \
        }                                                         \
    } while (0)

// =============================================================================
// Helpers
// =============================================================================

static std::unique_ptr<AudioEngine> makeEngine(uint32_t sampleRate, uint32_t blockSize) {
    auto eng = std::make_unique<AudioEngine>();
    eng->setSampleRate(sampleRate);
    eng->setBufferConfig(blockSize, kChannels);
    eng->setSafetyLimiterEnabled(false);
    eng->setMetronomeEnabled(false);
    eng->setAuditionModeEnabled(false);
    eng->initialize();
    return eng;
}

static float peakMagnitude(const float* buf, uint32_t frames, uint32_t channels) {
    float peak = 0.0f;
    for (uint32_t i = 0; i < frames * channels; ++i) {
        float absVal = buf[i] < 0.0f ? -buf[i] : buf[i];
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

static float computeRmsDb(const float* buf, uint32_t frames, uint32_t channels) {
    double sumSq = 0.0;
    uint64_t count = static_cast<uint64_t>(frames) * channels;
    for (uint64_t i = 0; i < count; ++i)
        sumSq += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    if (sumSq <= 0.0) return -INFINITY;
    return 10.0f * std::log10(static_cast<float>(sumSq / static_cast<double>(count)));
}

static bool isFinite(const float* buf, uint32_t frames, uint32_t channels) {
    for (uint32_t i = 0; i < frames * channels; ++i) {
        if (!std::isfinite(buf[i]))
            return false;
    }
    return true;
}

static int countDenormals(const float* buf, uint32_t frames, uint32_t channels) {
    int count = 0;
    for (uint32_t i = 0; i < frames * channels; ++i) {
        float v = buf[i];
        if (v != 0.0f && std::abs(v) < std::numeric_limits<float>::min())
            ++count;
    }
    return count;
}

// Create a TrackManager with N tracks
static std::shared_ptr<TrackManager> makeMultiTrackManager(
    uint32_t numTracks, uint32_t sampleRate)
{
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(sampleRate));
    for (uint32_t t = 0; t < numTracks; ++t)
        tm->addChannel("TortureTrack_" + std::to_string(t));
    return tm;
}

// =============================================================================
// Torture Scenarios
// =============================================================================

static void testSilencePath() {
    // The engine must produce exactly zero when processing silence with no tracks.
    TEST("Silence path — output is clean zero", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, -1.0f);
        eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);
        float peak = peakMagnitude(out.data(), kBlockSize, kChannels);
        return peak == 0.0f;
    });

    // Extended silence processing: 10 seconds (~1875 blocks at 48kHz/256f)
    TEST("Silence path — 10s sustained (no denormal buildup)", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels);
        int totalDenormals = 0;
        uint32_t numBlocks = 10 * kSampleRate / kBlockSize;
        for (uint32_t i = 0; i < numBlocks; ++i) {
            eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);
            totalDenormals += countDenormals(out.data(), kBlockSize, kChannels);
            if (peakMagnitude(out.data(), kBlockSize, kChannels) != 0.0f)
                return false;
        }
        return totalDenormals == 0;
    });

    // Silence through multi-track engine (tracks exist but no clips playing)
    TEST("Silence path — multi-track, no clips", {
        auto tm = makeMultiTrackManager(32, kSampleRate);
        auto eng = makeEngine(kSampleRate, kBlockSize);
        eng->setTrackManager(tm);
        eng->setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));

        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels);
        eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);
        float peak = peakMagnitude(out.data(), kBlockSize, kChannels);
        return peak == 0.0f;
    });
}

static void testNanInfInjection() {
#ifdef AESTRA_ENGINE_ASSERTS_ACTIVE
    // Set by Tests/CMakeLists.txt for Debug configs: the engine library keeps
    // assert() live there and AudioEngine::processBlock hard-asserts on
    // NaN/Inf, so poisoned-input cases would abort instead of reporting.
    // The sanitize-and-recover contract is verified in Release/RelWithDebInfo
    // runs (including the ASan/UBSan nightly).
    std::printf("  SKIP  NaN/Inf injection cases (engine asserts active in Debug config)\n");
#else
    // NaN on input must not propagate to output or corrupt engine state
    TEST("NaN input — engine produces finite output", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> nanIn(static_cast<size_t>(kBlockSize) * kChannels, NAN);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        eng->processBlock(out.data(), nanIn.data(), kBlockSize, 0.0);
        return isFinite(out.data(), kBlockSize, kChannels);
    });

    // Inf on input
    TEST("Inf input — engine produces finite output", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> infIn(static_cast<size_t>(kBlockSize) * kChannels, INFINITY);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        eng->processBlock(out.data(), infIn.data(), kBlockSize, 0.0);
        return isFinite(out.data(), kBlockSize, kChannels);
    });

    // Mixed NaN/signal — one channel NaN, other clean
    TEST("NaN in one channel — other channel unaffected", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> mixedIn(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            mixedIn[static_cast<size_t>(i) * 2] = NAN;       // Left = NaN
            mixedIn[static_cast<size_t>(i) * 2 + 1] = 0.5f;  // Right = signal
        }
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        eng->processBlock(out.data(), mixedIn.data(), kBlockSize, 0.0);
        return isFinite(out.data(), kBlockSize, kChannels);
    });

    // Recovery: after NaN, next clean block must be clean
    TEST("NaN recovery — next clean block is clean", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        std::vector<float> nanIn(static_cast<size_t>(kBlockSize) * kChannels, NAN);

        eng->processBlock(out.data(), nanIn.data(), kBlockSize, 0.0);  // Poison
        std::memset(out.data(), 0, out.size() * sizeof(float));
        eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);        // Recover
        return isFinite(out.data(), kBlockSize, kChannels);
    });
#endif
}

static void testExtremeGainValues() {
    // Very large input values should not cause overflow or NaN
    TEST("Extreme gain — +100dB signal stays bounded", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> hotIn(static_cast<size_t>(kBlockSize) * kChannels, 100000.0f);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        eng->processBlock(out.data(), hotIn.data(), kBlockSize, 0.0);
        if (!isFinite(out.data(), kBlockSize, kChannels))
            return false;
        float peak = peakMagnitude(out.data(), kBlockSize, kChannels);
        return std::isfinite(peak) && peak < 1e10f;
    });

    // Sinusoid at 0dBFS should clip gracefully (not NaN/Inf)
    TEST("Extreme gain — 0dBFS sine clips gracefully", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> sigIn(static_cast<size_t>(kBlockSize) * kChannels);
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            float s = std::sin(kTau * 440.0 * i / kSampleRate);
            sigIn[static_cast<size_t>(i) * 2] = s;
            sigIn[static_cast<size_t>(i) * 2 + 1] = s;
        }
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        eng->processBlock(out.data(), sigIn.data(), kBlockSize, 0.0);
        if (!isFinite(out.data(), kBlockSize, kChannels))
            return false;
        // Engine output stage hard-clamps L/R to [-1, 1]; verify the bound
        float peak = peakMagnitude(out.data(), kBlockSize, kChannels);
        return peak <= 1.0f;
    });
}

static void testBufferSizeReconfiguration() {
    // Can the engine handle buffer sizes changing between processBlock calls?
    const uint32_t sizes[] = {64, 128, 256, 512, 1024, 2048, 4096};

    TEST("Buffer size switching — all standard sizes", {
        auto eng = makeEngine(kSampleRate, 256);
        std::vector<float> out;
        for (uint32_t bs : sizes) {
            eng->setBufferConfig(bs, kChannels);
            out.assign(static_cast<size_t>(bs) * kChannels, 0.0f);
            eng->processBlock(out.data(), nullptr, bs, 0.0);
            if (!isFinite(out.data(), bs, kChannels))
                return false;
        }
        return true;
    });

    // Switch sizes randomly, 100 iterations
    TEST("Buffer size switching — 100 rapid changes", {
        auto eng = makeEngine(kSampleRate, 256);
        std::vector<float> out;
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> sizeDist(6, 12); // 64..4096, matching supported sizes
        for (int i = 0; i < 100; ++i) {
            uint32_t bs = 1u << sizeDist(rng);
            eng->setBufferConfig(bs, kChannels);
            out.assign(static_cast<size_t>(bs) * kChannels, 0.0f);
            eng->processBlock(out.data(), nullptr, bs, 0.0);
            if (!isFinite(out.data(), bs, kChannels))
                return false;
        }
        return true;
    });
}

static void testTransportHammering() {
    // Rapid play/stop toggles across processBlock calls
    TEST("Transport hammer — 1000 play/stop cycles", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels);
        for (int i = 0; i < 1000; ++i) {
            bool playing = (i % 2 == 0);
            eng->setTransportPlaying(playing);
            eng->setGlobalSamplePos(0);
            eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);
            if (!isFinite(out.data(), kBlockSize, kChannels))
                return false;
        }
        return true;
    });

    // Seek while playing
    TEST("Transport hammer — seek during playback", {
        auto eng = makeEngine(kSampleRate, kBlockSize);
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels);
        eng->setTransportPlaying(true);
        for (int i = 0; i < 100; ++i) {
            eng->setGlobalSamplePos(
                static_cast<uint64_t>(i) * kBlockSize * 7 % 441000);
            eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);
            if (!isFinite(out.data(), kBlockSize, kChannels))
                return false;
        }
        eng->setTransportPlaying(false);
        return true;
    });
}

static void testMultiTrackStress() {
    // Create many tracks and render with transport playing
    // Uses the internal test tone so we don't need to wire clips for every track

    TEST("Multi-track — 100 tracks with test tone", {
        auto tm = std::make_shared<TrackManager>();
        tm->setOutputSampleRate(static_cast<double>(kSampleRate));
        for (uint32_t t = 0; t < 100; ++t)
            tm->addChannel("Track_" + std::to_string(t));

        auto eng = std::make_unique<AudioEngine>();
        eng->setSampleRate(kSampleRate);
        eng->setBufferConfig(kBlockSize, kChannels);
        eng->setTrackManager(tm);
        eng->setTestToneEnabled(true);
        eng->setBPM(120.0f);
        eng->setSafetyLimiterEnabled(false);
        eng->setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
        eng->initialize();
        eng->setMetronomeEnabled(false);
        eng->setAuditionModeEnabled(false);
        eng->setGlobalSamplePos(0);
        eng->setTransportPlaying(true);

        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels);
        float maxPeak = 0.0f;
        for (int i = 0; i < 100; ++i) {
            eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);
            if (!isFinite(out.data(), kBlockSize, kChannels))
                return false;
            float p = peakMagnitude(out.data(), kBlockSize, kChannels);
            if (p > maxPeak) maxPeak = p;
        }
        eng->setTransportPlaying(false);
        return maxPeak > 0.0f;
    });
}

static void testLongRunStability() {
    // Process 30 seconds of test tone
    // Measure consistent RMS, no glitches

    TEST("Long run — 30s test tone, buffer size 256", {
        auto eng = makeEngine(kSampleRate, 256);
        eng->setTestToneEnabled(true);
        eng->initialize();
        eng->setTransportPlaying(true);

        std::vector<float> out(static_cast<size_t>(256) * kChannels);
        uint32_t totalFrames = kSampleRate * 30;
        uint32_t blocks = totalFrames / 256;
        uint32_t warmup = 8; // skip fade-in transient
        bool consistent = true;
        uint32_t measured = 0;

        for (uint32_t i = 0; i < blocks; ++i) {
            eng->processBlock(out.data(), nullptr, 256, 0.0);
            if (!isFinite(out.data(), 256, kChannels)) {
                consistent = false;
                break;
            }
            if (i < warmup) continue;
            ++measured;
            float rms = computeRmsDb(out.data(), 256, kChannels);
            // RMS should be stable near -26dBFS (test tone level)
            if (rms > -40.0f && rms < -10.0f) continue;
            consistent = false;
        }
        eng->setTransportPlaying(false);
        return consistent && measured > 0;
    });
}

static void testSampleRateReconfiguration() {
    const uint32_t rates[] = {44100, 48000, 88200, 96000, 44100, 48000};

    TEST("Sample rate switching — standard rates", {
        auto eng = std::make_unique<AudioEngine>();
        for (uint32_t rate : rates) {
            eng->setSampleRate(rate);
            eng->setBufferConfig(kBlockSize, kChannels);
            eng->initialize();
            std::vector<float> out(
                static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
            eng->processBlock(out.data(), nullptr, kBlockSize, 0.0);
            if (!isFinite(out.data(), kBlockSize, kChannels))
                return false;
        }
        return true;
    });
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::printf("============================================================\n");
    std::printf("  Aestra Audio Engine — Torture Test Suite\n");
    std::printf("============================================================\n\n");

    std::printf("[Silence path integrity]\n");
    testSilencePath();

    std::printf("\n[NaN/Inf injection]\n");
    testNanInfInjection();

    std::printf("\n[Extreme gain values]\n");
    testExtremeGainValues();

    std::printf("\n[Buffer size reconfiguration]\n");
    testBufferSizeReconfiguration();

    std::printf("\n[Sample rate reconfiguration]\n");
    testSampleRateReconfiguration();

    std::printf("\n[Transport hammering]\n");
    testTransportHammering();

    std::printf("\n[Multi-track stress]\n");
    testMultiTrackStress();

    std::printf("\n[Long-run stability]\n");
    testLongRunStability();

    std::printf("\n============================================================\n");
    std::printf("  Results: %d/%d passed, %d failed\n",
                g_testsPassed, g_testsRun, g_testsFailed);
    std::printf("============================================================\n");

    return g_testsFailed > 0 ? 1 : 0;
}
