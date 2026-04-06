// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Performance benchmark for Sinc interpolators to validate "Trig Reduction" optimization.

#include "Interpolators.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using namespace Aestra::Audio::Interpolators;

// Prevent compiler optimization
volatile float g_sink = 0.0f;

// ============================================================================
// CLI parsing
// ============================================================================

struct BenchConfig {
    bool jsonMode = false;
    int iterations = 1;
};

static BenchConfig parseArgs(int argc, char* argv[]) {
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json") {
            cfg.jsonMode = true;
        } else if (arg == "--iterations" && i + 1 < argc) {
            cfg.iterations = std::stoi(argv[++i]);
            if (cfg.iterations < 1) cfg.iterations = 1;
        }
    }
    return cfg;
}

// ============================================================================
// Algorithm definition
// ============================================================================

struct AlgoDef {
    std::string algoId;   // stable: e.g. "cubic_4pt"
    std::string name;     // human: e.g. "Cubic (4-point)"
    std::function<void(const float*, int64_t, float*, int)> runFn;
};

template <typename Interpolator>
static void runInterpolator(const float* input, int64_t inputFrames, float* output, int outputFrames) {
    const int kBlocks = 1000;
    const int kBlockSize = 256;
    double phaseStep = 1.0;
    double phase = 0.5;
    for (int b = 0; b < kBlocks; ++b) {
        for (int i = 0; i < kBlockSize; ++i) {
            float l, r;
            Interpolator::interpolate(input, inputFrames, phase, l, r);
            g_sink += l + r;
            phase += phaseStep;
            if (phase >= inputFrames - 64)
                phase = 0.5;
        }
    }
}

static std::vector<AlgoDef> buildAlgos() {
    std::vector<AlgoDef> algos;
    algos.push_back({"cubic_4pt", "Cubic (4-point)",
        [](const float* in, int64_t inF, float* out, int outF) {
            runInterpolator<CubicInterpolator>(in, inF, out, outF);
        }});
    algos.push_back({"sinc8_8pt", "Sinc8 (8-point)",
        [](const float* in, int64_t inF, float* out, int outF) {
            runInterpolator<Sinc8Interpolator>(in, inF, out, outF);
        }});
    algos.push_back({"sinc8_turbo", "Sinc8 TURBO (Polyphase)",
        [](const float* in, int64_t inF, float* out, int outF) {
            runInterpolator<Sinc8Turbo>(in, inF, out, outF);
        }});
    algos.push_back({"sinc64_orig", "Sinc64 (Original Opt)",
        [](const float* in, int64_t inF, float* out, int outF) {
            runInterpolator<Sinc64Interpolator>(in, inF, out, outF);
        }});
    algos.push_back({"sinc64_turbo", "Sinc64 TURBO (Multi-SIMD)",
        [](const float* in, int64_t inF, float* out, int outF) {
            runInterpolator<Sinc64Turbo>(in, inF, out, outF);
        }});
    return algos;
}

// ============================================================================
// Single-run result
// ============================================================================

struct SingleRun {
    double totalUs;
    double avgTimeUs;
    double mframesPerSec;
};

static SingleRun runOnce(const AlgoDef& algo, const float* input, int64_t inputFrames,
                          float* output, int outputFrames) {
    const int kBlocks = 1000;
    const int kBlockSize = 256;
    const int64_t operations = static_cast<int64_t>(kBlocks) * kBlockSize;

    auto t0 = std::chrono::high_resolution_clock::now();
    algo.runFn(input, inputFrames, output, outputFrames);
    auto t1 = std::chrono::high_resolution_clock::now();

    double totalUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    double avgTimeUs = totalUs / kBlocks;
    double mframesPerSec = static_cast<double>(operations) / totalUs;

    return {totalUs, avgTimeUs, mframesPerSec};
}

// ============================================================================
// Statistics
// ============================================================================

struct Stats {
    double median;
    double mean;
    double best;
    double worst;
    double stddev;
    double cv;
};

static Stats computeStats(std::vector<double>& values) {
    if (values.empty()) return {0, 0, 0, 0, 0, 0};
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (double v : values) sum += v;
    double mean = sum / static_cast<double>(values.size());
    double best = values.front();
    double worst = values.back();
    double median = values[values.size() / 2];
    double varSum = 0;
    for (double v : values) varSum += (v - mean) * (v - mean);
    double stddev = std::sqrt(varSum / static_cast<double>(values.size()));
    double cv = (mean > 0) ? (stddev / mean) : 0.0;
    return {median, mean, best, worst, stddev, cv};
}

// ============================================================================
// JSON helpers
// ============================================================================

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    BenchConfig cfg = parseArgs(argc, argv);
    auto algos = buildAlgos();

    // Setup Data
    const int kInputSize = 48000;
    std::vector<float> input(kInputSize * 2);
    std::vector<float> output(256 * 2);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = dist(rng);

    if (!cfg.jsonMode) {
        std::cout << "========================================\n";
        std::cout << " SINC INTERPOLATOR BENCHMARK\n";
        std::cout << " Validating Trig Reduction Optimization\n";
        std::cout << "========================================\n\n";

        std::cout << "Starting tests...\n";
        std::cout << "  CPU Features:\n";
        std::cout << "    AVX512F: " << (Aestra::Core::CPUDetection::get().hasAVX512F() ? "YES" : "NO") << "\n";
        std::cout << "    AVX2:   " << (Aestra::Core::CPUDetection::get().hasAVX2() ? "YES" : "NO") << "\n";
        std::cout << "    FMA:    " << (Aestra::Core::CPUDetection::get().hasFMA() ? "YES" : "NO") << "\n";
        std::cout << "    SSE4.1: " << (Aestra::Core::CPUDetection::get().hasSSE41() ? "YES" : "NO") << "\n";
        std::cout << "    NEON:   " << (Aestra::Core::CPUDetection::get().hasNEON() ? "YES" : "NO") << "\n";
        std::cout << "\n";
    }

    struct AlgoResult {
        AlgoDef def;
        Stats usPerBlock;
        Stats mframesPerSec;
    };
    std::vector<AlgoResult> allResults;

    for (const auto& algo : algos) {
        std::vector<double> usVals, mfpsVals;
        for (int i = 0; i < cfg.iterations; ++i) {
            SingleRun r = runOnce(algo, input.data(), kInputSize, output.data(), 0);
            usVals.push_back(r.avgTimeUs);
            mfpsVals.push_back(r.mframesPerSec);
        }
        AlgoResult ar;
        ar.def = algo;
        ar.usPerBlock = computeStats(usVals);
        ar.mframesPerSec = computeStats(mfpsVals);
        allResults.push_back(ar);

        if (!cfg.jsonMode) {
            std::cout << "Testing " << algo.name << "...\n";
        }
    }

    // Human-readable table
    if (!cfg.jsonMode) {
        std::cout << "\n";
        std::cout << std::left << std::setw(25) << "Algorithm"
                  << "| " << std::setw(15) << "MFrame/sec"
                  << "| " << std::setw(12) << "us/block"
                  << "| " << std::setw(10) << "Relative"
                  << "\n";
        std::cout << "-------------------------|----------------|-------------|-----------\n";

        double baseRate = allResults[0].mframesPerSec.median;
        for (const auto& ar : allResults) {
            std::cout << std::left << std::setw(25) << ar.def.name << "| "
                      << std::setw(15) << std::fixed << std::setprecision(2) << ar.mframesPerSec.median << "| "
                      << std::setw(12) << std::setprecision(2) << ar.usPerBlock.median << "| "
                      << std::setw(10) << std::setprecision(2)
                      << (baseRate > 0 ? (ar.mframesPerSec.median / baseRate) : 0.0) << "x"
                      << "\n";
        }

        std::cout << "\nAnalysis:\n";
        if (allResults.size() >= 4) {
            double s8 = allResults[1].mframesPerSec.median;
            double s64 = allResults[3].mframesPerSec.median;
            double ratio = (s64 > 0) ? (s8 / s64) : 0.0;
            std::cout << "Sinc64 is " << std::setprecision(2) << ratio << "x slower than Sinc8.\n";
            std::cout << "(Theoretical non-optimized would be ~8x slower strictly due to taps,\n";
            std::cout << " but with Trig Reduction, the overhead is purely MAC + RAM, so it should be efficient.)\n";
        }
    }

    // JSON output
    if (cfg.jsonMode) {
        std::cout << "{\n";
        std::cout << "  \"benchmark\": \"AestraSincBenchmark\",\n";
        std::cout << "  \"iterations\": " << cfg.iterations << ",\n";
        std::cout << "  \"algorithms\": [\n";
        for (size_t i = 0; i < allResults.size(); ++i) {
            const auto& ar = allResults[i];
            std::cout << "    {\n";
            std::cout << "      \"algo_id\": \"" << jsonEscape(ar.def.algoId) << "\",\n";
            std::cout << "      \"name\": \"" << jsonEscape(ar.def.name) << "\",\n";
            std::cout << "      \"median_mframes_per_sec\": " << std::fixed << std::setprecision(4) << ar.mframesPerSec.median << ",\n";
            std::cout << "      \"mean_mframes_per_sec\": " << std::fixed << std::setprecision(4) << ar.mframesPerSec.mean << ",\n";
            std::cout << "      \"best_mframes_per_sec\": " << std::fixed << std::setprecision(4) << ar.mframesPerSec.best << ",\n";
            std::cout << "      \"worst_mframes_per_sec\": " << std::fixed << std::setprecision(4) << ar.mframesPerSec.worst << ",\n";
            std::cout << "      \"median_us_per_block\": " << std::fixed << std::setprecision(4) << ar.usPerBlock.median << ",\n";
            std::cout << "      \"cv\": " << std::fixed << std::setprecision(6) << ar.mframesPerSec.cv << "\n";
            std::cout << "    }";
            if (i + 1 < allResults.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ]\n";
        std::cout << "}\n";
    }

    return 0;
}
