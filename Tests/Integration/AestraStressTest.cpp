// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Stress test: measures how many simultaneous sinc-resampled clips Aestra can handle
// before missing real-time deadlines.
//
// Usage:
//   AestraStressTest [--clips N] [--quality Q] [--buffer-size N] [--sample-rate N] [--iterations N] [--json]
//
// Output: Reports CPU time per callback vs. available budget, and finds the breaking point.

#include "Interpolators.h"
#include "../../AestraCore/include/AestraUnifiedProfiler.h"
#include "../../AestraCore/include/AestraMemory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio::Interpolators;

// ============================================================================
// CLI parsing
// ============================================================================

struct TestConfig {
    int numClips = 16;
    std::string quality = "sinc8_turbo";
    int bufferSize = 256;
    int sampleRate = 48000;
    int iterations = 10;
    bool jsonMode = false;
};

static TestConfig parseArgs(int argc, char* argv[]) {
    TestConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--clips" && i + 1 < argc) {
            cfg.numClips = std::stoi(argv[++i]);
        } else if (arg == "--quality" && i + 1 < argc) {
            cfg.quality = argv[++i];
        } else if (arg == "--buffer-size" && i + 1 < argc) {
            cfg.bufferSize = std::stoi(argv[++i]);
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            cfg.sampleRate = std::stoi(argv[++i]);
        } else if (arg == "--iterations" && i + 1 < argc) {
            cfg.iterations = std::stoi(argv[++i]);
        } else if (arg == "--json") {
            cfg.jsonMode = true;
        }
    }
    return cfg;
}

// ============================================================================
// Interpolation function dispatch
// ============================================================================

using InterpFunc = void (*)(const float*, int64_t, double, float&, float&);

static InterpFunc resolveInterp(const std::string& quality) {
    if (quality == "cubic") {
        return [](const float* d, int64_t f, double p, float& l, float& r) {
            CubicInterpolator::interpolate(d, f, p, l, r);
        };
    } else if (quality == "sinc8") {
        return [](const float* d, int64_t f, double p, float& l, float& r) {
            Sinc8Interpolator::interpolate(d, f, p, l, r);
        };
    } else if (quality == "sinc8_turbo") {
        return [](const float* d, int64_t f, double p, float& l, float& r) {
            Sinc8Turbo::interpolate(d, f, p, l, r);
        };
    } else if (quality == "sinc16_turbo") {
        return [](const float* d, int64_t f, double p, float& l, float& r) {
            Sinc16Turbo::interpolate(d, f, p, l, r);
        };
    } else if (quality == "sinc64_turbo") {
        return [](const float* d, int64_t f, double p, float& l, float& r) {
            Sinc64Turbo::interpolate(d, f, p, l, r);
        };
    } else if (quality == "mixed") {
        // Mixed quality: distributes clips across tiers
        return nullptr; // handled specially
    }
    return Sinc8Turbo::interpolate; // default
}

// Mixed quality dispatch: round-robin through Sinc8 TURBO, Sinc16 TURBO, Sinc64 TURBO
static void mixedInterpolate(int clipIdx, const float* d, int64_t f, double p, float& l, float& r) {
    switch (clipIdx % 3) {
        case 0: Sinc8Turbo::interpolate(d, f, p, l, r); break;
        case 1: Sinc16Turbo::interpolate(d, f, p, l, r); break;
        case 2: Sinc64Turbo::interpolate(d, f, p, l, r); break;
    }
}

// ============================================================================
// Simulated clip state
// ============================================================================

struct ClipState {
    int clipIdx;           // For mixed quality dispatch
    double srcPosition;    // Source position accumulator
    double srcRatio;       // Playback pitch (1.0 = native, <1 = slower, >1 = faster)
    float* clipData;       // Interleaved stereo clip data
    int64_t clipLength;    // Frames in clip
    InterpFunc interp;     // Interpolation function (nullptr for mixed)
};

// Generate a sine wave clip
static std::vector<float> makeClip(double freq, double sr, int frames, int channels = 2) {
    std::vector<float> buf(frames * channels);
    std::mt19937 rng(42 + static_cast<unsigned>(freq));
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
    for (int i = 0; i < frames; ++i) {
        float s = std::sin(2.0 * 3.14159265358979323846 * freq * i / sr);
        for (int ch = 0; ch < channels; ++ch)
            buf[i * channels + ch] = s * dist(rng); // Add slight variation
    }
    return buf;
}

// ============================================================================
// Simulated audio callback
// ============================================================================

// Process one buffer of output for N simultaneous clips
static void processCallback(std::vector<ClipState>& clips, float* output, int bufferSize) {
    std::memset(output, 0, bufferSize * 2 * sizeof(float)); // Zero output buffer (stereo)

    for (int frame = 0; frame < bufferSize; ++frame) {
        for (auto& clip : clips) {
            // Check bounds
            if (clip.srcPosition < 0 || clip.srcPosition >= clip.clipLength - 1)
                continue;

            float l = 0, r = 0;
            if (clip.interp) {
                clip.interp(clip.clipData, clip.clipLength, clip.srcPosition, l, r);
            } else {
                mixedInterpolate(clip.clipIdx, clip.clipData, clip.clipLength, clip.srcPosition, l, r);
            }

            output[frame * 2] += l;
            output[frame * 2 + 1] += r;

            clip.srcPosition += clip.srcRatio;
        }
    }
}

// ============================================================================
// Statistics
// ============================================================================

struct Stats {
    double median{0};
    double mean{0};
    double best{0};
    double worst{0};
    double p95{0};
    double p99{0};
    double stddev{0};
    double cv{0};
};

static Stats computeStats(std::vector<double>& values) {
    if (values.empty()) return {};
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (double v : values) sum += v;
    double mean = sum / values.size();
    double varSum = 0;
    for (double v : values) varSum += (v - mean) * (v - mean);
    double stddev = std::sqrt(varSum / values.size());
    size_t n = values.size();
    return {
        values[n / 2],
        mean,
        values.front(),
        values.back(),
        values[static_cast<size_t>(n * 0.95)],
        values[static_cast<size_t>(n * 0.99)],
        stddev,
        mean > 0 ? stddev / mean : 0.0
    };
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    TestConfig cfg = parseArgs(argc, argv);

    // Initialize global arena
    GlobalAudioArena::instance().reset();

    // Suppress profiler output in JSON mode
    if (cfg.jsonMode) {
        UnifiedProfiler::getInstance().setEnabled(false);
    }

    // Calculate buffer time budget
    double bufferTimeMs = (static_cast<double>(cfg.bufferSize) / cfg.sampleRate) * 1000.0;
    double targetTimeMs = bufferTimeMs * 0.70; // 70% budget target

    if (!cfg.jsonMode) {
        std::cout << "========================================\n";
        std::cout << "  AESTRA STRESS TEST — Simultaneous Clips\n";
        std::cout << "========================================\n\n";
        std::cout << "  Clips:       " << cfg.numClips << "\n";
        std::cout << "  Quality:     " << cfg.quality << "\n";
        std::cout << "  Buffer size: " << cfg.bufferSize << " samples\n";
        std::cout << "  Sample rate: " << cfg.sampleRate << " Hz\n";
        std::cout << "  Iterations:  " << cfg.iterations << "\n";
        std::cout << "  Budget:      " << std::fixed << std::setprecision(2) << bufferTimeMs << " ms\n";
        std::cout << "  Target:      " << std::fixed << std::setprecision(2) << targetTimeMs << " ms (70%)\n";
        std::cout << "\n";
    }

    // Generate clip data
    std::vector<std::vector<float>> clipData;
    std::vector<double> pitches;
    std::vector<double> frequencies = {261.63, 329.63, 392.00, 440.00, 523.25, 659.25, 783.99, 880.00};
    std::mt19937 rng(12345);

    for (int i = 0; i < cfg.numClips; ++i) {
        // Each clip is 10 seconds of audio
        int clipFrames = cfg.sampleRate * 10;
        double freq = frequencies[i % frequencies.size()] * (0.5 + static_cast<double>(i) * 0.1);
        clipData.push_back(makeClip(freq, cfg.sampleRate, clipFrames));

        // Pitch: vary between 0.5x and 2.0x (always resampling, never passthrough)
        double pitch = 0.5 + static_cast<double>(rng() % 1500) / 1000.0; // 0.5 to 2.0
        pitches.push_back(pitch);
    }

    // Build clip states
    InterpFunc interp = resolveInterp(cfg.quality);
    std::vector<ClipState> clips;
    clips.reserve(cfg.numClips);

    for (int i = 0; i < cfg.numClips; ++i) {
        ClipState cs;
        cs.clipIdx = i;
        cs.srcPosition = static_cast<double>(rng() % 10000); // Random start position
        cs.srcRatio = pitches[i];
        cs.clipData = clipData[i].data();
        cs.clipLength = static_cast<int64_t>(clipData[i].size() / 2);
        cs.interp = cfg.quality == "mixed" ? nullptr : interp;
        clips.push_back(cs);
    }

    // Output buffer
    std::vector<float> output(cfg.bufferSize * 2);

    // Run iterations
    std::vector<double> timesUs;
    timesUs.reserve(cfg.iterations);

    // Warmup
    for (int i = 0; i < 3; ++i) {
        // Reset clip positions for each iteration
        for (auto& c : clips) c.srcPosition = static_cast<double>(rng() % 10000);
        processCallback(clips, output.data(), cfg.bufferSize);
    }

    // Timed runs
    for (int iter = 0; iter < cfg.iterations; ++iter) {
        // Reset positions
        for (auto& c : clips) c.srcPosition = static_cast<double>(rng() % 10000);

        auto t0 = std::chrono::high_resolution_clock::now();
        processCallback(clips, output.data(), cfg.bufferSize);
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        timesUs.push_back(us);
    }

    Stats st = computeStats(timesUs);
    double budgetUs = bufferTimeMs * 1000.0;
    double targetUs = targetTimeMs * 1000.0;
    bool passed = st.median < targetUs;
    double usagePercent = (st.median / budgetUs) * 100.0;

    if (cfg.jsonMode) {
        std::cout << "{\n";
        std::cout << "  \"benchmark\": \"AestraStressTest\",\n";
        std::cout << "  \"config\": {\n";
        std::cout << "    \"clips\": " << cfg.numClips << ",\n";
        std::cout << "    \"quality\": \"" << cfg.quality << "\",\n";
        std::cout << "    \"buffer_size\": " << cfg.bufferSize << ",\n";
        std::cout << "    \"sample_rate\": " << cfg.sampleRate << ",\n";
        std::cout << "    \"iterations\": " << cfg.iterations << "\n";
        std::cout << "  },\n";
        std::cout << "  \"budget_ms\": " << std::fixed << std::setprecision(2) << bufferTimeMs << ",\n";
        std::cout << "  \"target_ms\": " << std::fixed << std::setprecision(2) << targetTimeMs << ",\n";
        std::cout << "  \"results\": {\n";
        std::cout << "    \"median_ms\": " << std::fixed << std::setprecision(3) << (st.median / 1000.0) << ",\n";
        std::cout << "    \"mean_ms\": " << std::fixed << std::setprecision(3) << (st.mean / 1000.0) << ",\n";
        std::cout << "    \"best_ms\": " << std::fixed << std::setprecision(3) << (st.best / 1000.0) << ",\n";
        std::cout << "    \"worst_ms\": " << std::fixed << std::setprecision(3) << (st.worst / 1000.0) << ",\n";
        std::cout << "    \"p95_ms\": " << std::fixed << std::setprecision(3) << (st.p95 / 1000.0) << ",\n";
        std::cout << "    \"p99_ms\": " << std::fixed << std::setprecision(3) << (st.p99 / 1000.0) << ",\n";
        std::cout << "    \"cv\": " << std::fixed << std::setprecision(4) << st.cv << ",\n";
        std::cout << "    \"budget_usage_pct\": " << std::fixed << std::setprecision(1) << usagePercent << ",\n";
        std::cout << "    \"passed\": " << (passed ? "true" : "false") << "\n";
        std::cout << "  }\n";
        std::cout << "}\n";
    } else {
        std::cout << "========================================\n";
        std::cout << "  Results\n";
        std::cout << "========================================\n";
        std::cout << std::left << std::setw(20) << "Metric"
                  << std::right << std::setw(12) << "Value"
                  << std::setw(10) << "Unit"
                  << "\n";
        std::cout << "-------------------|------------|----------\n";
        std::cout << std::left << std::setw(20) << "Median"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(3) << (st.median / 1000.0)
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "Mean"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(3) << (st.mean / 1000.0)
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "Best"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(3) << (st.best / 1000.0)
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "Worst"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(3) << (st.worst / 1000.0)
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "P95"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(3) << (st.p95 / 1000.0)
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "P99"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(3) << (st.p99 / 1000.0)
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "CV"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(2) << (st.cv * 100.0)
                  << std::setw(10) << "%" << "\n";
        std::cout << std::left << std::setw(20) << "Budget"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(2) << bufferTimeMs
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "Target (70%)"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(2) << targetTimeMs
                  << std::setw(10) << "ms" << "\n";
        std::cout << std::left << std::setw(20) << "Usage"
                  << std::right << std::setw(12) << std::fixed << std::setprecision(1) << usagePercent
                  << std::setw(10) << "%" << "\n";
        std::cout << "\n";
        std::cout << "========================================\n";
        if (passed) {
            std::cout << "  ✅ PASS — Callback within 70% budget\n";
        } else {
            std::cout << "  ❌ FAIL — Callback exceeds 70% budget\n";
        }
        std::cout << "========================================\n";
    }

    return passed ? 0 : 1;
}
