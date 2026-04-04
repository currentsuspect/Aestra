// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// K-005: Driver-level soak test
// Exercises the real audio driver path (RtAudio/WASAPI) with a live stream
// for an extended period. Unlike AudioEngineSoakTest.cpp which calls
// processBlock() directly in a loop, this test opens a real audio stream,
// runs the driver callback, and monitors telemetry through the actual path.
//
// Usage:
//   AestraAudioDriverSoakTest [--duration-sec N] [--sr N] [--frames N]
//
// Gated behind AESTRA_ENABLE_RUNTIME_TESTS (requires audio hardware).

#include "AudioDeviceManager.h"
#include "AudioEngine.h"
#include "AudioTelemetry.h"
#include "AestraLog.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace Aestra::Audio;

static std::atomic<bool> g_running{true};

// Simple sine wave generator callback
static int generateSine(float* outputBuffer, const float* /*inputBuffer*/, uint32_t numFrames,
                        double /*streamTime*/, void* userData) {
    auto* phase = static_cast<double*>(userData);
    const double freq = 440.0;
    const double sr = 48000.0;

    for (uint32_t i = 0; i < numFrames * 2; i += 2) {
        double sample = std::sin(2.0 * 3.14159265358979323846 * (*phase) * freq / sr);
        outputBuffer[i] = static_cast<float>(sample);
        outputBuffer[i + 1] = static_cast<float>(sample);
        (*phase) += 1.0;
        if (*phase >= sr) {
            *phase -= sr;
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    uint32_t durationSec = 60;  // Default: 1 minute (short enough for CI validation)
    uint32_t sampleRate = 48000;
    uint32_t bufferSize = 512;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration-sec" && i + 1 < argc) {
            durationSec = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--sr" && i + 1 < argc) {
            sampleRate = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--frames" && i + 1 < argc) {
            bufferSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
    }

    std::printf("=== Aestra Driver Soak Test ===\n");
    std::printf("Duration: %u sec, Sample Rate: %u, Buffer: %u frames\n", durationSec, sampleRate, bufferSize);

    AudioEngine engine;
    engine.setSampleRate(sampleRate);
    engine.setBufferConfig(bufferSize, 2);

    AudioDeviceManager deviceManager;
    if (!deviceManager.initialize()) {
        std::fprintf(stderr, "FAIL: AudioDeviceManager::initialize failed\n");
        return 1;
    }

    AudioStreamConfig config;
    config.sampleRate = sampleRate;
    config.bufferSize = bufferSize;
    config.numOutputChannels = 2;
    config.telemetry = &engine.telemetry();

    double phase = 0.0;

    if (!deviceManager.openStream(config, generateSine, &phase)) {
        std::fprintf(stderr, "FAIL: openStream failed — no audio device available?\n");
        std::fprintf(stderr, "Skipping driver soak test (no hardware).\n");
        return 0; // Not a failure — just no hardware
    }

    if (!deviceManager.startStream()) {
        std::fprintf(stderr, "FAIL: startStream failed\n");
        return 1;
    }

    std::printf("Stream started. Monitoring for %u seconds...\n", durationSec);

    auto startTime = std::chrono::steady_clock::now();
    uint64_t lastBlocks = 0;
    uint64_t lastXruns = 0;
    uint64_t lastUnderruns = 0;
    uint64_t maxCallbackNs = 0;
    bool passed = true;

    while (g_running.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

        if (elapsed >= static_cast<int64_t>(durationSec)) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));

        const auto& tel = engine.telemetry();
        uint64_t blocks = tel.getBlocksProcessed();
        uint64_t xruns = tel.getXruns();
        uint64_t underruns = tel.getUnderruns();
        uint64_t cbMax = tel.getMaxCallbackNs();
        uint32_t bufFrames = tel.getLastBufferFrames();
        uint32_t sr = tel.getLastSampleRate();

        if (cbMax > maxCallbackNs) {
            maxCallbackNs = cbMax;
        }

        double budgetMs = (sr > 0) ? (static_cast<double>(bufFrames) * 1000.0 / sr) : 0.0;
        double maxCbMs = static_cast<double>(cbMax) / 1e6;
        double loadPct = (budgetMs > 0) ? (maxCbMs / budgetMs * 100.0) : 0.0;

        std::printf("[%3lds] blocks=%lu xruns=%lu underruns=%lu cbMax=%.3fms load=%.1f%%\n",
                    static_cast<long>(elapsed), static_cast<unsigned long>(blocks),
                    static_cast<unsigned long>(xruns), static_cast<unsigned long>(underruns),
                    maxCbMs, loadPct);

        // Check for new xruns/underruns since last report
        if (xruns > lastXruns || underruns > lastUnderruns) {
            std::fprintf(stderr, "WARNING: XRun or underrun detected during soak!\n");
        }

        lastBlocks = blocks;
        lastXruns = xruns;
        lastUnderruns = underruns;
    }

    deviceManager.stopStream();
    deviceManager.closeStream();

    const auto& tel = engine.telemetry();
    uint64_t totalBlocks = tel.getBlocksProcessed();
    uint64_t totalXruns = tel.getXruns();
    uint64_t totalUnderruns = tel.getUnderruns();
    double finalMaxCbMs = static_cast<double>(tel.getMaxCallbackNs()) / 1e6;
    double budgetMs = (sampleRate > 0) ? (static_cast<double>(bufferSize) * 1000.0 / sampleRate) : 0.0;
    double finalLoadPct = (budgetMs > 0) ? (finalMaxCbMs / budgetMs * 100.0) : 0.0;

    std::printf("\n=== Results ===\n");
    std::printf("Total blocks:   %lu\n", static_cast<unsigned long>(totalBlocks));
    std::printf("XRuns:          %lu\n", static_cast<unsigned long>(totalXruns));
    std::printf("Underruns:      %lu\n", static_cast<unsigned long>(totalUnderruns));
    std::printf("Max callback:   %.3fms / %.3fms (%.1f%%)\n", finalMaxCbMs, budgetMs, finalLoadPct);

    // Pass/fail
    if (totalXruns > 0) {
        std::fprintf(stderr, "FAIL: %lu xruns during driver soak\n", static_cast<unsigned long>(totalXruns));
        passed = false;
    }
    if (totalUnderruns > 0) {
        std::fprintf(stderr, "FAIL: %lu underruns during driver soak\n", static_cast<unsigned long>(totalUnderruns));
        passed = false;
    }
    if (finalLoadPct > 80.0) {
        std::fprintf(stderr, "FAIL: callback load %.1f%% exceeds 80%% threshold\n", finalLoadPct);
        passed = false;
    }
    if (totalBlocks == 0) {
        std::fprintf(stderr, "FAIL: zero blocks processed\n");
        passed = false;
    }

    if (passed) {
        std::printf("PASS: Driver soak test completed successfully\n");
        return 0;
    }

    return 1;
}
