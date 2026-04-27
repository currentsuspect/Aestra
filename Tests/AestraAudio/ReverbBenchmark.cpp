// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ReverbBenchmark — Quantify AestraVerb SIMD optimization speedup and quality gains.

#include "Plugin/AestraVerb.h"
#include "CPUDetection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

using namespace Aestra::Audio;
using namespace Aestra::Audio::Plugins;
using namespace Aestra::Audio::DSP;

// Prevent dead-code elimination
volatile float g_sink = 0.0f;

// ============================================================================
// CLI parsing
// ============================================================================

struct BenchConfig {
    bool jsonStdout = false;
    bool forceScalar = false;
    bool forceLinear = false;
    int iterations = 3;
    int durationSec = 5;
    int warmupIterations = 0;
    std::string outputJsonPath;
    std::string outputMdPath;
    std::string outputTextPath;
};

static BenchConfig parseArgs(int argc, char* argv[]) {
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--scalar") {
            cfg.forceScalar = true;
        } else if (arg == "--linear") {
            cfg.forceLinear = true;
        } else if (arg == "--json") {
            cfg.jsonStdout = true;
        } else if (arg == "--iterations" && i + 1 < argc) {
            cfg.iterations = std::stoi(argv[++i]);
            if (cfg.iterations < 1) cfg.iterations = 1;
        } else if (arg == "--duration" && i + 1 < argc) {
            cfg.durationSec = std::stoi(argv[++i]);
            if (cfg.durationSec < 1) cfg.durationSec = 1;
        } else if (arg == "--warmup" && i + 1 < argc) {
            cfg.warmupIterations = std::stoi(argv[++i]);
            if (cfg.warmupIterations < 0) cfg.warmupIterations = 0;
        } else if (arg == "--output-json" && i + 1 < argc) {
            cfg.outputJsonPath = argv[++i];
        } else if (arg == "--output-md" && i + 1 < argc) {
            cfg.outputMdPath = argv[++i];
        } else if (arg == "--output-text" && i + 1 < argc) {
            cfg.outputTextPath = argv[++i];
        }
    }
    return cfg;
}

// ============================================================================
// Benchmark harness
// ============================================================================

struct BenchResult {
    double medianUs = 0.0;
    double minUs = 0.0;
    double maxUs = 0.0;
    double stdDevUs = 0.0;
    double samplesPerSec = 0.0;
    double rtFactor = 0.0;
    float outputRMS = 0.0f;
    bool hasNaN = false;
    bool hasInf = false;
};

static BenchResult benchmarkPlugin(AestraVerb& verb, const std::vector<float>& inputL,
                                   const std::vector<float>& inputR, int iterations) {
    std::vector<double> times;
    times.reserve(iterations);

    const size_t blockSize = 256;
    const size_t numFrames = inputL.size();
    std::vector<float> outL(numFrames), outR(numFrames);

    float finalRMS = 0.0f;
    bool hasNaN = false;
    bool hasInf = false;

    for (int iter = 0; iter < iterations; ++iter) {
        verb.activate();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        auto t0 = std::chrono::high_resolution_clock::now();

        for (size_t offset = 0; offset < numFrames; offset += blockSize) {
            size_t cur = std::min(blockSize, numFrames - offset);
            const float* inPtrs[2] = { inputL.data() + offset, inputR.data() + offset };
            float* outPtrs[2] = { outL.data() + offset, outR.data() + offset };
            verb.process(inPtrs, outPtrs, 2, 2, static_cast<uint32_t>(cur));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        times.push_back(us);

        // Compute RMS and check for NaN/Inf
        double sumSq = 0.0;
        for (size_t i = 0; i < numFrames; ++i) {
            float vL = outL[i];
            float vR = outR[i];
            if (std::isnan(vL) || std::isnan(vR)) hasNaN = true;
            if (std::isinf(vL) || std::isinf(vR)) hasInf = true;
            sumSq += double(vL) * vL + double(vR) * vR;
        }
        finalRMS = static_cast<float>(std::sqrt(sumSq / (numFrames * 2)));

        g_sink += outL[numFrames / 2] + outR[numFrames / 2];
    }

    std::sort(times.begin(), times.end());
    BenchResult r;
    r.medianUs = times[times.size() / 2];
    r.minUs = times.front();
    r.maxUs = times.back();
    double mean = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    double sqSum = 0.0;
    for (double t : times) sqSum += (t - mean) * (t - mean);
    r.stdDevUs = std::sqrt(sqSum / times.size());
    r.samplesPerSec = (double(numFrames) / r.medianUs) * 1e6;
    r.rtFactor = r.samplesPerSec / 48000.0;
    r.outputRMS = finalRMS;
    r.hasNaN = hasNaN;
    r.hasInf = hasInf;
    return r;
}

// ============================================================================
// Quality metrics
// ============================================================================

static float highFrequencyEnergyRatio(const std::vector<float>& signal, double sampleRate) {
    const size_t N = signal.size();
    const size_t bins = N / 2 + 1;
    double totalEnergy = 0.0;
    double hfEnergy = 0.0;
    const double hfThreshold = 10000.0;

    for (size_t k = 0; k < bins; ++k) {
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < N; ++n) {
            double phase = -2.0 * M_PI * double(k) * double(n) / double(N);
            re += signal[n] * std::cos(phase);
            im += signal[n] * std::sin(phase);
        }
        double magSq = re * re + im * im;
        totalEnergy += magSq;
        double freq = double(k) * sampleRate / double(N);
        if (freq > hfThreshold) {
            hfEnergy += magSq;
        }
    }
    return totalEnergy > 1e-20f ? static_cast<float>(hfEnergy / totalEnergy) : 0.0f;
}

// ============================================================================
// Report generation
// ============================================================================

struct ModeResult {
    std::string name;
    BenchResult result;
};

static std::string generateMarkdownReport(
    const std::vector<ModeResult>& results,
    double sampleRate,
    size_t numFrames,
    int iterations,
    const std::string& cpuFlags,
    const std::string& gitSha,
    const std::string& gitBranch,
    const std::string& interpolationName,
    const std::string& simdPathName,
    float hfRatio) {

    std::ostringstream md;
    md << "# AestraVerb SIMD Hardware Lab Report\n\n";
    md << "| Field | Value |\n";
    md << "|-------|-------|\n";
    md << "| Timestamp | " << std::fixed << std::setprecision(0)
       << std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()
       << " UTC |\n";
    md << "| Git SHA | " << gitSha << " |\n";
    md << "| Branch | " << gitBranch << " |\n";
    md << "| Sample Rate | " << static_cast<int>(sampleRate) << " Hz |\n";
    md << "| Frames | " << numFrames << " |\n";
    md << "| Iterations | " << iterations << " |\n";
    md << "| Interpolation | " << interpolationName << " |\n";
    md << "| SIMD Path | " << simdPathName << " |\n";
    md << "| CPU Flags | " << cpuFlags << " |\n";
    md << "| HF Energy >10kHz | " << std::setprecision(2) << (hfRatio * 100.0f) << "% |\n";
    md << "\n";

    md << "## Results by Mode\n\n";
    md << "| Mode | Median (ms) | Throughput (Ms/s) | RT Factor | CPU Budget (256smp) | RMS | NaN | Inf |\n";
    md << "|------|-------------|-------------------|-----------|---------------------|-----|-----|-----|\n";

    double callbackUs = (256.0 / sampleRate) * 1e6;
    for (const auto& mr : results) {
        double budgetPct = (mr.result.medianUs / (numFrames / 256.0 * callbackUs)) * 100.0;
        md << "| " << mr.name << " | "
           << std::fixed << std::setprecision(1) << (mr.result.medianUs / 1000.0) << " | "
           << std::setprecision(2) << (mr.result.samplesPerSec / 1e6) << " | "
           << std::setprecision(2) << mr.result.rtFactor << " | "
           << std::setprecision(2) << budgetPct << "% | "
           << std::setprecision(4) << mr.result.outputRMS << " | "
           << (mr.result.hasNaN ? "YES" : "no") << " | "
           << (mr.result.hasInf ? "YES" : "no") << " |\n";
    }
    md << "\n";

    if (results.size() >= 2) {
        md << "## Speedup Ratios\n\n";
        for (size_t i = 1; i < results.size(); ++i) {
            double ratio = results[0].result.medianUs / results[i].result.medianUs;
            md << "- " << results[i].name << " vs " << results[0].name << ": "
               << std::fixed << std::setprecision(2) << ratio << "x\n";
        }
        md << "\n";
    }

    md << "## Correctness Checks\n\n";
    bool allClean = true;
    for (const auto& mr : results) {
        if (mr.result.hasNaN) { md << "- **WARNING**: NaN detected in " << mr.name << " output\n"; allClean = false; }
        if (mr.result.hasInf) { md << "- **WARNING**: Inf detected in " << mr.name << " output\n"; allClean = false; }
    }
    if (allClean) md << "- All outputs clean (no NaN/Inf)\n";
    md << "\n";

    return md.str();
}

static std::string generateJsonReport(
    const std::vector<ModeResult>& results,
    double sampleRate,
    size_t numFrames,
    int iterations,
    const std::string& cpuFlags,
    const std::string& gitSha,
    const std::string& gitBranch,
    const std::string& interpolationName,
    const std::string& simdPathName,
    float hfRatio) {

    std::ostringstream j;
    j << "{\n";
    j << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
    j << "  \"gitSha\": \"" << gitSha << "\",\n";
    j << "  \"gitBranch\": \"" << gitBranch << "\",\n";
    j << "  \"sampleRate\": " << static_cast<int>(sampleRate) << ",\n";
    j << "  \"numFrames\": " << numFrames << ",\n";
    j << "  \"iterations\": " << iterations << ",\n";
    j << "  \"interpolation\": \"" << interpolationName << "\",\n";
    j << "  \"simdPath\": \"" << simdPathName << "\",\n";
    j << "  \"cpuFlags\": \"" << cpuFlags << "\",\n";
    j << "  \"hfEnergyRatio\": " << std::setprecision(6) << hfRatio << ",\n";
    j << "  \"modes\": [\n";

    double callbackUs = (256.0 / sampleRate) * 1e6;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& mr = results[i];
        double budgetPct = (mr.result.medianUs / (numFrames / 256.0 * callbackUs)) * 100.0;
        j << "    {\n";
        j << "      \"name\": \"" << mr.name << "\",\n";
        j << "      \"medianUs\": " << std::fixed << std::setprecision(1) << mr.result.medianUs << ",\n";
        j << "      \"minUs\": " << mr.result.minUs << ",\n";
        j << "      \"maxUs\": " << mr.result.maxUs << ",\n";
        j << "      \"stdDevUs\": " << mr.result.stdDevUs << ",\n";
        j << "      \"samplesPerSec\": " << std::setprecision(2) << mr.result.samplesPerSec << ",\n";
        j << "      \"rtFactor\": " << std::setprecision(2) << mr.result.rtFactor << ",\n";
        j << "      \"cpuBudgetPercent\": " << std::setprecision(2) << budgetPct << ",\n";
        j << "      \"outputRMS\": " << std::setprecision(6) << mr.result.outputRMS << ",\n";
        j << "      \"hasNaN\": " << (mr.result.hasNaN ? "true" : "false") << ",\n";
        j << "      \"hasInf\": " << (mr.result.hasInf ? "true" : "false") << "\n";
        j << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    j << "  ]\n";
    j << "}\n";
    return j.str();
}

// ============================================================================
// Git helpers
// ============================================================================

static std::string execCmd(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "";
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    BenchConfig cfg = parseArgs(argc, argv);
    if (cfg.forceScalar) {
        ReverbSIMD::g_forceScalarFallback = true;
    }
    if (cfg.forceLinear) {
        ReverbSIMD::g_forceLinearInterpolation = true;
    }

    const double sampleRate = 48000.0;
    const size_t numFrames = static_cast<size_t>(sampleRate * cfg.durationSec);

    // Generate pink-ish input
    std::mt19937 rng(42);
    std::normal_distribution<float> dist(0.0f, 0.3f);
    std::vector<float> inputL(numFrames), inputR(numFrames);
    for (size_t i = 0; i < numFrames; ++i) {
        inputL[i] = dist(rng);
        inputR[i] = dist(rng);
    }
    inputL[0] = 1.0f;
    inputR[0] = 1.0f;

    // CPU info
    const auto& cpu = Aestra::Core::CPUDetection::get();
    bool hasAVX2 = cpu.hasAVX2();
    bool hasSSE41 = cpu.hasSSE41();
    bool hasNEON = cpu.hasNEON();

    std::string cpuFlags;
    if (hasAVX2) cpuFlags += "AVX2 ";
    if (hasSSE41) cpuFlags += "SSE4.1 ";
    if (hasNEON) cpuFlags += "NEON ";
    if (cpuFlags.empty()) cpuFlags = "Scalar-only";

    std::string simdPathName;
    if (cfg.forceScalar) {
        simdPathName = "Scalar (forced)";
    } else if (hasAVX2) {
        simdPathName = "AVX2 (runtime dispatch)";
    } else if (hasSSE41) {
        simdPathName = "SSE4.1 (runtime dispatch)";
    } else if (hasNEON) {
        simdPathName = "NEON (runtime dispatch)";
    } else {
        simdPathName = "Scalar (runtime dispatch)";
    }

    std::string interpolationName = cfg.forceLinear ? "Linear (forced)" : "Cubic Hermite";

    // Git metadata
    std::string gitSha = execCmd("git rev-parse --short HEAD 2>/dev/null || echo unknown");
    std::string gitBranch = execCmd("git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown");

    // Warmup
    if (cfg.warmupIterations > 0) {
        if (!cfg.jsonStdout) {
            std::cout << "Running " << cfg.warmupIterations << " warmup iteration(s)...\n";
        }
        AestraVerb verb;
        verb.initialize(sampleRate, 256);
        verb.setParameter(AestraVerb::kMode, 0.0f);
        verb.setParameter(AestraVerb::kDecay, 0.6f);
        verb.setParameter(AestraVerb::kSize, 0.5f);
        verb.setParameter(AestraVerb::kMix, 1.0f);
        benchmarkPlugin(verb, inputL, inputR, cfg.warmupIterations);
    }

    // Capture stdout if --output-text is requested
    std::streambuf* originalCoutBuf = nullptr;
    std::ofstream textFile;
    std::ostringstream capturedText;
    if (!cfg.outputTextPath.empty()) {
        originalCoutBuf = std::cout.rdbuf(capturedText.rdbuf());
    }

    if (!cfg.jsonStdout) {
        std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║         AESTRAVERB SIMD OPTIMIZATION BENCHMARK               ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ Input: " << std::setw(6) << cfg.durationSec << "s @ " << static_cast<int>(sampleRate)
                  << "Hz  |  Frames: " << numFrames << "\n";
        std::cout << "║ Iterations: " << cfg.iterations << "\n";
        std::cout << "║ CPU: AVX2=" << (hasAVX2 ? "YES" : "NO")
                  << "  SSE4.1=" << (hasSSE41 ? "YES" : "NO")
                  << "  NEON=" << (hasNEON ? "YES" : "NO") << "\n";
        std::cout << "║ Interpolation: " << interpolationName << "\n";
        std::cout << "║ SIMD Path: " << simdPathName << "\n";
        if (cfg.forceScalar) {
            std::cout << "║ MODE: SCALAR FALLBACK (SIMD disabled)\n";
        } else {
            std::cout << "║ MODE: SIMD optimized (auto-dispatched)\n";
        }
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    }

    std::vector<ModeResult> results;

    // Room
    {
        AestraVerb verb;
        verb.initialize(sampleRate, 256);
        verb.setParameter(AestraVerb::kMode, 0.0f);
        verb.setParameter(AestraVerb::kDecay, 0.6f);
        verb.setParameter(AestraVerb::kSize, 0.5f);
        verb.setParameter(AestraVerb::kDiffusion, 0.7f);
        verb.setParameter(AestraVerb::kModRate, 0.5f);
        verb.setParameter(AestraVerb::kModDepth, 0.3f);
        verb.setParameter(AestraVerb::kMix, 1.0f);
        verb.setParameter(AestraVerb::kWidth, 0.7f);

        auto result = benchmarkPlugin(verb, inputL, inputR, cfg.iterations);
        results.push_back({"Room", result});

        if (cfg.jsonStdout) {
            std::cout << "{\n";
            std::cout << "  \"mode\": \"room\",\n";
            std::cout << "  \"hasAVX2\": " << (hasAVX2 ? "true" : "false") << ",\n";
            std::cout << "  \"medianUs\": " << result.medianUs << ",\n";
            std::cout << "  \"samplesPerSec\": " << result.samplesPerSec << ",\n";
            std::cout << "  \"rtFactor\": " << result.rtFactor << ",\n";
            std::cout << "  \"outputRMS\": " << result.outputRMS << "\n";
            std::cout << "}\n";
        } else {
            std::cout << "─── Room Mode ───\n";
            std::cout << "  Median time:  " << std::fixed << std::setprecision(1) << result.medianUs << " us\n";
            std::cout << "  Min/Max:      " << result.minUs << " / " << result.maxUs << " us\n";
            std::cout << "  StdDev:       " << std::setprecision(2) << result.stdDevUs << " us ("
                      << (result.stdDevUs / result.medianUs * 100.0) << "% CV)\n";
            std::cout << "  Throughput:   " << std::setprecision(2) << (result.samplesPerSec / 1e6)
                      << " Msamples/sec\n";
            std::cout << "  Real-time:    " << std::setprecision(2) << result.rtFactor << "x\n";
            std::cout << "  Output RMS:   " << std::setprecision(4) << result.outputRMS << "\n";
            if (result.hasNaN) std::cout << "  **WARNING**: NaN detected in output\n";
            if (result.hasInf) std::cout << "  **WARNING**: Inf detected in output\n";

            double callbackUs = (256.0 / sampleRate) * 1e6;
            double budgetPct = (result.medianUs / (numFrames / 256.0 * callbackUs)) * 100.0;
            std::cout << "  CPU Budget:   " << std::setprecision(2) << budgetPct << "% per callback (256smp)\n";
        }
    }

    // Hall
    {
        AestraVerb verb;
        verb.initialize(sampleRate, 256);
        verb.setParameter(AestraVerb::kMode, 1.0f);
        verb.setParameter(AestraVerb::kDecay, 0.9f);
        verb.setParameter(AestraVerb::kSize, 0.9f);
        verb.setParameter(AestraVerb::kDiffusion, 0.85f);
        verb.setParameter(AestraVerb::kModRate, 1.0f);
        verb.setParameter(AestraVerb::kModDepth, 0.6f);
        verb.setParameter(AestraVerb::kMix, 1.0f);
        verb.setParameter(AestraVerb::kWidth, 0.9f);

        auto result = benchmarkPlugin(verb, inputL, inputR, cfg.iterations);
        results.push_back({"Hall", result});

        if (!cfg.jsonStdout) {
            std::cout << "\n─── Hall Mode (Stress) ───\n";
            std::cout << "  Median time:  " << std::fixed << std::setprecision(1) << result.medianUs << " us\n";
            std::cout << "  Throughput:   " << std::setprecision(2) << (result.samplesPerSec / 1e6)
                      << " Msamples/sec\n";
            std::cout << "  Real-time:    " << std::setprecision(2) << result.rtFactor << "x\n";
            if (result.hasNaN) std::cout << "  **WARNING**: NaN detected\n";
            if (result.hasInf) std::cout << "  **WARNING**: Inf detected\n";

            double callbackUs = (256.0 / sampleRate) * 1e6;
            double budgetPct = (result.medianUs / (numFrames / 256.0 * callbackUs)) * 100.0;
            std::cout << "  CPU Budget:   " << std::setprecision(2) << budgetPct << "% per callback (256smp)\n";
        }
    }

    // Plate
    {
        AestraVerb verb;
        verb.initialize(sampleRate, 256);
        verb.setParameter(AestraVerb::kMode, 2.0f);
        verb.setParameter(AestraVerb::kDecay, 0.7f);
        verb.setParameter(AestraVerb::kSize, 0.6f);
        verb.setParameter(AestraVerb::kDiffusion, 0.8f);
        verb.setParameter(AestraVerb::kModRate, 0.7f);
        verb.setParameter(AestraVerb::kModDepth, 0.5f);
        verb.setParameter(AestraVerb::kMix, 1.0f);
        verb.setParameter(AestraVerb::kWidth, 0.8f);

        auto result = benchmarkPlugin(verb, inputL, inputR, cfg.iterations);
        results.push_back({"Plate", result});

        if (!cfg.jsonStdout) {
            std::cout << "\n─── Plate Mode ───\n";
            std::cout << "  Median time:  " << std::fixed << std::setprecision(1) << result.medianUs << " us\n";
            std::cout << "  Throughput:   " << std::setprecision(2) << (result.samplesPerSec / 1e6)
                      << " Msamples/sec\n";
            std::cout << "  Real-time:    " << std::setprecision(2) << result.rtFactor << "x\n";
            if (result.hasNaN) std::cout << "  **WARNING**: NaN detected\n";
            if (result.hasInf) std::cout << "  **WARNING**: Inf detected\n";

            double callbackUs = (256.0 / sampleRate) * 1e6;
            double budgetPct = (result.medianUs / (numFrames / 256.0 * callbackUs)) * 100.0;
            std::cout << "  CPU Budget:   " << std::setprecision(2) << budgetPct << "% per callback (256smp)\n";
        }
    }

    // Quality analysis
    float hfRatio = 0.0f;
    if (!cfg.jsonStdout) {
        AestraVerb verb;
        verb.initialize(sampleRate, 4096);
        verb.setParameter(AestraVerb::kMode, 1.0f);
        verb.setParameter(AestraVerb::kDecay, 0.8f);
        verb.setParameter(AestraVerb::kSize, 0.7f);
        verb.setParameter(AestraVerb::kMix, 1.0f);
        verb.activate();

        std::vector<float> impulseL(4096, 0.0f), impulseR(4096, 0.0f);
        impulseL[0] = 1.0f;
        impulseR[0] = 1.0f;
        std::vector<float> tailL(4096), tailR(4096);
        const float* inPtrs[2] = { impulseL.data(), impulseR.data() };
        float* outPtrs[2] = { tailL.data(), tailR.data() };
        verb.process(inPtrs, outPtrs, 2, 2, 4096);

        hfRatio = highFrequencyEnergyRatio(tailL, sampleRate);
        std::cout << "\n─── Quality Metrics ───\n";
        std::cout << "  Interpolation:  " << interpolationName << "\n";
        std::cout << "  HF Energy >10k: " << std::setprecision(4) << (hfRatio * 100.0f) << "%\n";
    }

    if (!cfg.jsonStdout) {
        std::cout << "\n✅ Benchmark complete.\n";
    }

    // Write output files
    if (!cfg.outputJsonPath.empty()) {
        std::ofstream f(cfg.outputJsonPath);
        if (f) {
            f << generateJsonReport(results, sampleRate, numFrames, cfg.iterations,
                                    cpuFlags, gitSha, gitBranch, interpolationName, simdPathName, hfRatio);
        }
    }

    if (!cfg.outputMdPath.empty()) {
        std::ofstream f(cfg.outputMdPath);
        if (f) {
            f << generateMarkdownReport(results, sampleRate, numFrames, cfg.iterations,
                                        cpuFlags, gitSha, gitBranch, interpolationName, simdPathName, hfRatio);
        }
    }

    if (originalCoutBuf != nullptr) {
        std::cout.rdbuf(originalCoutBuf);
        std::ofstream f(cfg.outputTextPath);
        if (f) f << capturedText.str();
    }

    // Return error if NaN/Inf detected
    for (const auto& mr : results) {
        if (mr.result.hasNaN || mr.result.hasInf) {
            std::cerr << "ERROR: NaN or Inf detected in benchmark output. Failing.\n";
            return 1;
        }
    }

    return 0;
}
