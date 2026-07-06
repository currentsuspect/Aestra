// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CallbackDeadlineTelemetryTest — validates the consolidated callback deadline
// accounting in AudioTelemetry::recordCallbackDuration():
//   average callback time, worst (max) callback time, buffer budget, and
//   over-budget (deadline overrun) counting.
//
// Part 1 checks the math with synthetic durations. Part 2 smoke-tests the
// same API against real processBlock timings measured test-side, asserting
// counter consistency (not absolute timing, which is machine-dependent).

#include "GoldenAudio/GoldenAudioHarness.h"

#include "Core/AudioTelemetry.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

int g_failures = 0;

void require(bool cond, const char* label) {
    std::cout << "[" << (cond ? "PASS" : "FAIL") << "] " << label << "\n";
    if (!cond) ++g_failures;
}

void syntheticMathCase() {
    AudioTelemetry tel;

    // 512 frames @ 48 kHz → budget = 10,666,666 ns.
    constexpr uint32_t kFrames = 512;
    constexpr uint32_t kRate = 48000;
    constexpr uint64_t kExpectedBudgetNs = (static_cast<uint64_t>(kFrames) * 1000000000ull) / kRate;

    tel.recordCallbackDuration(2'000'000, kFrames, kRate);  // 2 ms — under budget
    tel.recordCallbackDuration(4'000'000, kFrames, kRate);  // 4 ms — under budget
    tel.recordCallbackDuration(12'000'000, kFrames, kRate); // 12 ms — OVER budget

    require(tel.getCallbackBudgetNs() == kExpectedBudgetNs, "budget = frames/sampleRate (10.667 ms)");
    require(tel.lastCallbackNs.load() == 12'000'000, "last = most recent duration");
    require(tel.maxCallbackNs.load() == 12'000'000, "worst = max duration");
    require(tel.getAverageCallbackNs() == 6'000'000, "average = mean of recorded durations");
    require(tel.overruns.load() == 1, "over-budget count = exactly the one 12 ms callback");
    require(tel.timedCallbackCount.load() == 3, "timed callback count");

    // Degenerate context: unknown sample rate must not divide by zero or
    // count phantom overruns.
    AudioTelemetry tel2;
    tel2.recordCallbackDuration(5'000'000, kFrames, 0);
    require(tel2.overruns.load() == 0, "no overrun counted when sample rate unknown");
    require(tel2.getCallbackBudgetNs() == 0, "budget reports 0 when sample rate unknown");
    require(tel2.getAverageCallbackNs() == 5'000'000, "average still tracked without budget context");
}

void liveSmokeCase() {
    SessionConfig cfg;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    tm->addChannel("DeadlineSmoke");

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);

    AudioTelemetry tel;
    std::vector<float> block(static_cast<size_t>(cfg.blockSize) * cfg.channels, 0.0f);
    constexpr uint32_t kBlocks = 64;
    for (uint32_t i = 0; i < kBlocks; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, cfg.blockSize, 0.0);
        const auto t1 = std::chrono::steady_clock::now();
        const uint64_t ns =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        tel.recordCallbackDuration(ns, cfg.blockSize, cfg.sampleRate);
    }
    engine.setTransportPlaying(false);

    require(tel.timedCallbackCount.load() == kBlocks, "live: every block recorded");
    require(tel.getAverageCallbackNs() > 0, "live: average is nonzero");
    require(tel.getAverageCallbackNs() <= tel.maxCallbackNs.load(), "live: average <= worst");
    require(tel.lastCallbackNs.load() <= tel.maxCallbackNs.load(), "live: last <= worst");
    require(tel.getCallbackBudgetNs() > 0, "live: budget known");
    std::cout << "  live timings: avg=" << tel.getAverageCallbackNs() / 1000 << " us, worst="
              << tel.maxCallbackNs.load() / 1000 << " us, budget=" << tel.getCallbackBudgetNs() / 1000
              << " us, over-budget=" << tel.overruns.load() << "/" << kBlocks << "\n";
}

} // namespace

int main() {
    std::cout << "=== Aestra Callback Deadline Telemetry Test ===\n\n";
    syntheticMathCase();
    liveSmokeCase();
    std::cout << "\n" << (g_failures == 0 ? "ALL PASS" : "FAILURES: " + std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
