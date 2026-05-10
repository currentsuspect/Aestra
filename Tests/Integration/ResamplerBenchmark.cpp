// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraLog.h"
#include "SampleRateConverter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

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
// Benchmark case definition
// ============================================================================

struct BenchCase {
    std::string caseId;      // stable identifier: e.g. "up_44100_48000_sinc16"
    std::string label;       // human-readable: e.g. "Up 44.1->48 [Sinc16]"
    uint32_t srcRate;
    uint32_t dstRate;
    SRCQuality quality;
};

static std::string qualityName(SRCQuality q) {
    switch (q) {
    case SRCQuality::Linear: return "Linear";
    case SRCQuality::Cubic:  return "Cubic";
    case SRCQuality::Sinc8:  return "Sinc8";
    case SRCQuality::Sinc16: return "Sinc16";
    case SRCQuality::Sinc64: return "Sinc64";
    }
    return "Unknown";
}

static std::string qualityId(SRCQuality q) {
    switch (q) {
    case SRCQuality::Linear: return "linear";
    case SRCQuality::Cubic:  return "cubic";
    case SRCQuality::Sinc8:  return "sinc8";
    case SRCQuality::Sinc16: return "sinc16";
    case SRCQuality::Sinc64: return "sinc64";
    }
    return "unknown";
}

static std::vector<BenchCase> buildCases() {
    std::vector<BenchCase> cases;

    // Upsampling 44.1 -> 48 kHz
    for (auto q : {SRCQuality::Linear, SRCQuality::Cubic, SRCQuality::Sinc8,
                   SRCQuality::Sinc16, SRCQuality::Sinc64}) {
        cases.push_back({"up_44100_48000_" + qualityId(q),
                         "Up 44.1->48 [" + qualityName(q) + "]",
                         44100, 48000, q});
    }

    // Downsampling 48 -> 44.1 kHz
    for (auto q : {SRCQuality::Linear, SRCQuality::Cubic, SRCQuality::Sinc8,
                   SRCQuality::Sinc16, SRCQuality::Sinc64}) {
        cases.push_back({"down_48000_44100_" + qualityId(q),
                         "Down 48->44.1 [" + qualityName(q) + "]",
                         48000, 44100, q});
    }

    // Extreme upsampling 48 -> 192 kHz
    for (auto q : {SRCQuality::Cubic, SRCQuality::Sinc64}) {
        cases.push_back({"up_48000_192000_" + qualityId(q),
                         "Up 48->192 [" + qualityName(q) + "]",
                         48000, 192000, q});
    }

    return cases;
}

// ============================================================================
// Single-run result
// ============================================================================

struct RunResult {
    double totalMs;
    double nsPerSample;
    double mhz;
    double realtimeFactor;
};

static RunResult runOnce(const BenchCase& bc) {
    SampleRateConverter src;
    src.configure(bc.srcRate, bc.dstRate, 2, bc.quality);

    const uint32_t durationSec = 10;
    const uint32_t inputFrames = bc.srcRate * durationSec;
    std::vector<float> input(inputFrames * 2);

    // Fill with sine wave
    for (uint32_t i = 0; i < inputFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(bc.srcRate);
        float val = std::sin(2.0f * 3.14159f * 440.0f * t);
        input[i * 2] = val;
        input[i * 2 + 1] = val;
    }

    uint32_t estOut = static_cast<uint32_t>(
        static_cast<double>(inputFrames) * static_cast<double>(bc.dstRate) /
        static_cast<double>(bc.srcRate)) + 100;
    std::vector<float> output(estOut * 2);

    // Warmup
    src.process(input.data(), 1024, output.data(), estOut);
    src.reset();

    // Timed run
    auto t0 = std::chrono::high_resolution_clock::now();
    uint32_t written = src.process(input.data(), inputFrames, output.data(), estOut);
    auto t1 = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double nsPerSample = (totalMs * 1000000.0) / static_cast<double>(inputFrames);
    double mhz = static_cast<double>(inputFrames) / totalMs / 1000.0;
    double audioSec = static_cast<double>(written) / static_cast<double>(bc.dstRate);
    double wallSec = totalMs / 1000.0;
    double rtf = (wallSec > 0) ? (audioSec / wallSec) : 0.0;

    return {totalMs, nsPerSample, mhz, rtf};
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
    double cv; // coefficient of variation
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
// JSON helpers (minimal, no external deps)
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
// Human-readable output
// ============================================================================

static void printHuman(const BenchCase& bc, const Stats& sMs, const Stats& sRtf, int iterations) {
    std::cout << std::left << std::setw(35) << bc.label << "\n";
    std::cout << "  Iterations:  " << iterations << "\n";
    std::cout << "  Median:      " << std::fixed << std::setprecision(2) << sMs.median << " ms\n";
    std::cout << "  Mean:        " << std::fixed << std::setprecision(2) << sMs.mean << " ms\n";
    std::cout << "  Best:        " << std::fixed << std::setprecision(2) << sMs.best << " ms\n";
    std::cout << "  Worst:       " << std::fixed << std::setprecision(2) << sMs.worst << " ms\n";
    std::cout << "  Stddev:      " << std::fixed << std::setprecision(4) << sMs.stddev << " ms\n";
    std::cout << "  CV:          " << std::fixed << std::setprecision(4) << sMs.cv << "\n";
    std::cout << "  RTF (med):   " << std::fixed << std::setprecision(1) << sRtf.median << "x\n";
    std::cout << "\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    BenchConfig cfg = parseArgs(argc, argv);
    auto cases = buildCases();

    if (!cfg.jsonMode) {
        Aestra::Log::init(std::make_shared<Aestra::ConsoleLogger>(Aestra::LogLevel::Info));
        Aestra::Log::info("Starting Resampler Benchmark...");
        SampleRateConverter::hasAVX();
        std::cout << "\n";
    } else {
        // Suppress log output in JSON mode so it doesn't corrupt stdout
        Aestra::Log::init(std::make_shared<Aestra::ConsoleLogger>(Aestra::LogLevel::Error));
    }

    // Collect all case results
    struct CaseResult {
        BenchCase bc;
        Stats ms;
        Stats nsPerSample;
        Stats mhz;
        Stats rtf;
    };
    std::vector<CaseResult> allResults;

    for (const auto& bc : cases) {
        std::vector<double> msVals, nsVals, mhzVals, rtfVals;
        for (int i = 0; i < cfg.iterations; ++i) {
            RunResult r = runOnce(bc);
            msVals.push_back(r.totalMs);
            nsVals.push_back(r.nsPerSample);
            mhzVals.push_back(r.mhz);
            rtfVals.push_back(r.realtimeFactor);
        }
        CaseResult cr;
        cr.bc = bc;
        cr.ms = computeStats(msVals);
        cr.nsPerSample = computeStats(nsVals);
        cr.mhz = computeStats(mhzVals);
        cr.rtf = computeStats(rtfVals);
        allResults.push_back(cr);

        if (!cfg.jsonMode) {
            printHuman(bc, cr.ms, cr.rtf, cfg.iterations);
        }
    }

    // JSON output
    if (cfg.jsonMode) {
        std::cout << "{\n";
        std::cout << "  \"benchmark\": \"ResamplerBenchmark\",\n";
        std::cout << "  \"iterations\": " << cfg.iterations << ",\n";
        std::cout << "  \"cases\": [\n";
        for (size_t i = 0; i < allResults.size(); ++i) {
            const auto& cr = allResults[i];
            std::cout << "    {\n";
            std::cout << "      \"case_id\": \"" << jsonEscape(cr.bc.caseId) << "\",\n";
            std::cout << "      \"name\": \"" << jsonEscape(cr.bc.label) << "\",\n";
            std::cout << "      \"src_rate\": " << cr.bc.srcRate << ",\n";
            std::cout << "      \"dst_rate\": " << cr.bc.dstRate << ",\n";
            std::cout << "      \"quality\": \"" << qualityName(cr.bc.quality) << "\",\n";
            std::cout << "      \"median_ms\": " << std::fixed << std::setprecision(4) << cr.ms.median << ",\n";
            std::cout << "      \"mean_ms\": " << std::fixed << std::setprecision(4) << cr.ms.mean << ",\n";
            std::cout << "      \"best_ms\": " << std::fixed << std::setprecision(4) << cr.ms.best << ",\n";
            std::cout << "      \"worst_ms\": " << std::fixed << std::setprecision(4) << cr.ms.worst << ",\n";
            std::cout << "      \"ns_per_sample\": " << std::fixed << std::setprecision(4) << cr.nsPerSample.median << ",\n";
            std::cout << "      \"mhz\": " << std::fixed << std::setprecision(4) << cr.mhz.median << ",\n";
            std::cout << "      \"realtime_factor\": " << std::fixed << std::setprecision(4) << cr.rtf.median << ",\n";
            std::cout << "      \"cv\": " << std::fixed << std::setprecision(6) << cr.ms.cv << "\n";
            std::cout << "    }";
            if (i + 1 < allResults.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ]\n";
        std::cout << "}\n";
    }

    return 0;
}
