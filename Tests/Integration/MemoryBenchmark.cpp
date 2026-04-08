// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Memory allocation benchmark — measures allocation counts and timing per audio operation.
// Uses the AESTRA_MEMORY_ALLOC/FREE macros wired at real allocation sites.

#include "AestraMemory.h"
#include "../../AestraAudio/include/DSP/AudioProcessor.h"
#include "../../AestraAudio/include/DSP/SampleRateConverter.h"
#include "../../AestraCore/include/AestraUnifiedProfiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Aestra;
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

struct CaseResult {
    std::string caseId;
    std::string name;
    int64_t arenaAllocs{0};    // Arena allocation count
    int64_t profilerAllocs{0}; // Profiler AESTRA_MEMORY_ALLOC count
    int64_t profilerFrees{0};
    size_t arenaBytes{0};      // Bytes used from arena
    size_t profilerBytes{0};   // Bytes tracked by profiler
    double medianMs{0.0};
    double meanMs{0.0};
    double bestMs{0.0};
    double worstMs{0.0};
    double cv{0.0};
};

struct BenchCase {
    std::string caseId;
    std::string name;
    std::function<void()> runFn;
};

// ============================================================================
// Helpers
// ============================================================================

// Generate a sine wave buffer
static std::vector<float> makeSine(double freq, double sampleRate, int frames, int channels = 2) {
    std::vector<float> buf(frames * channels);
    for (int i = 0; i < frames; ++i) {
        float s = std::sin(2.0 * 3.14159265358979323846 * freq * i / sampleRate);
        for (int ch = 0; ch < channels; ++ch)
            buf[i * channels + ch] = s;
    }
    return buf;
}

// Measure alloc/free counts from arena + profiler during a function call
static void measureAllocations(std::function<void()> fn, int64_t& outArenaAllocs, int64_t& outAllocs,
                               int64_t& outFrees, size_t& outBytes, size_t& outArenaUsed) {
    // Reset arena to get clean state
    auto& arena = GlobalAudioArena::instance();
    arena.reset();

    // Reset profiler frame
    auto& prof = UnifiedProfiler::getInstance();
    prof.beginFrame();

    size_t arenaUsedBefore = arena.used();
    size_t arenaAllocsBefore = arena.allocationCount();

    fn();

    outArenaAllocs = static_cast<int64_t>(arena.allocationCount() - arenaAllocsBefore);
    outArenaUsed = arena.used() - arenaUsedBefore;

    const auto& frame = prof.getCurrentFrame();
    outAllocs = static_cast<int64_t>(frame.memory.allocationCount);
    outFrees = static_cast<int64_t>(frame.memory.deallocationCount);
    outBytes = frame.memory.currentBytes;
}

static double timedMs(std::function<void()> fn) {
    auto t0 = std::chrono::high_resolution_clock::now();
    fn();
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
}

// ============================================================================
// Benchmark cases
// ============================================================================

static std::vector<BenchCase> buildCases() {
    std::vector<BenchCase> cases;

    // Case 1: AudioBufferManager construction
    cases.push_back({"bufmgr_construct", "AudioBufferManager Construct", []() {
        AudioBufferManager mgr;
        (void)mgr;
    }});

    // Case 2: AudioBufferManager allocate + clear
    cases.push_back({"bufmgr_alloc_clear", "AudioBufferManager Allocate + Clear", []() {
        GlobalAudioArena::instance().reset();
        AudioBufferManager mgr;
        float* buf = mgr.allocate(1024, 2);
        if (buf) mgr.clear();
    }});

    // Case 3: SampleRateConverter configure (Sinc16, upsampling)
    cases.push_back({"src_configure_sinc16_up", "SRC Configure Sinc16 44.1→48k", []() {
        SampleRateConverter src;
        src.configure(44100, 48000, 2, SRCQuality::Sinc16);
    }});

    // Case 4: SampleRateConverter process (Sinc16)
    cases.push_back({"src_process_sinc16", "SRC Process Sinc16 (44100 frames)", []() {
        SampleRateConverter src;
        src.configure(44100, 48000, 2, SRCQuality::Sinc16);
        auto input = makeSine(440.0, 44100.0, 44100, 2);
        std::vector<float> output(48000 * 2);
        uint32_t written = src.process(input.data(), 44100, output.data(), 48000);
        (void)written;
    }});

    // Case 5: SampleRateConverter process (Sinc64)
    cases.push_back({"src_process_sinc64", "SRC Process Sinc64 (44100 frames)", []() {
        SampleRateConverter src;
        src.configure(44100, 48000, 2, SRCQuality::Sinc64);
        auto input = makeSine(440.0, 44100.0, 44100, 2);
        std::vector<float> output(48000 * 2);
        uint32_t written = src.process(input.data(), 44100, output.data(), 48000);
        (void)written;
    }});

    // Case 6: Full round-trip (up then down)
    cases.push_back({"src_roundtrip", "SRC Round-Trip 44.1→48→44.1k Sinc16", []() {
        auto input = makeSine(440.0, 44100.0, 44100, 2);

        // Upsample
        SampleRateConverter srcUp;
        srcUp.configure(44100, 48000, 2, SRCQuality::Sinc16);
        std::vector<float> mid(48000 * 2);
        uint32_t nUp = srcUp.process(input.data(), 44100, mid.data(), 48000);

        // Downsample
        SampleRateConverter srcDown;
        srcDown.configure(48000, 44100, 2, SRCQuality::Sinc16);
        std::vector<float> out(nUp * 2);
        uint32_t nDown = srcDown.process(mid.data(), nUp, out.data(), nUp);
        (void)nDown;
    }});

    return cases;
}

// ============================================================================
// Stats
// ============================================================================

struct Stats {
    double median{0};
    double mean{0};
    double best{0};
    double worst{0};
    double cv{0};
};

static Stats computeStats(std::vector<double>& values) {
    if (values.empty()) return {};
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (double v : values) sum += v;
    double mean = sum / values.size();
    double varianceSum = 0;
    for (double v : values) varianceSum += (v - mean) * (v - mean);
    double stddev = std::sqrt(varianceSum / values.size());
    return {values[values.size() / 2], mean, values.front(), values.back(),
            mean > 0 ? stddev / mean : 0.0};
}

// ============================================================================
// JSON
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
    auto benchCases = buildCases();

    if (!cfg.jsonMode) {
        std::cout << "========================================\n";
        std::cout << "  MEMORY ALLOCATION BENCHMARK\n";
        std::cout << "========================================\n\n";
    }

    std::vector<CaseResult> results;

    for (const auto& bc : benchCases) {
        std::vector<double> timeVals;

        // First run: warmup + baseline allocation count
        int64_t wArenaAllocs = 0, wAllocs = 0, wFrees = 0;
        size_t wBytes = 0, wArenaUsed = 0;
        measureAllocations(bc.runFn, wArenaAllocs, wAllocs, wFrees, wBytes, wArenaUsed);

        // Timed + allocation measurement runs
        for (int iter = 0; iter < cfg.iterations; ++iter) {
            int64_t aAllocs = 0, pAllocs = 0, pFrees = 0;
            size_t pBytes = 0, arenaUsed = 0;
            double ms = timedMs([&]() {
                measureAllocations(bc.runFn, aAllocs, pAllocs, pFrees, pBytes, arenaUsed);
            });
            timeVals.push_back(ms);
        }

        Stats ts = computeStats(timeVals);

        CaseResult cr;
        cr.caseId = bc.caseId;
        cr.name = bc.name;
        cr.arenaAllocs = wArenaAllocs;
        cr.profilerAllocs = wAllocs;
        cr.profilerFrees = wFrees;
        cr.arenaBytes = wArenaUsed;
        cr.profilerBytes = wBytes;
        cr.medianMs = ts.median;
        cr.meanMs = ts.mean;
        cr.bestMs = ts.best;
        cr.worstMs = ts.worst;
        cr.cv = ts.cv;
        results.push_back(cr);

        if (!cfg.jsonMode) {
            std::cout << std::left << std::setw(40) << bc.name
                      << " arena_allocs=" << cr.arenaAllocs
                      << " arena_bytes=" << cr.arenaBytes
                      << " prof_allocs=" << cr.profilerAllocs
                      << " prof_frees=" << cr.profilerFrees
                      << " time=" << std::fixed << std::setprecision(2) << cr.medianMs << "ms"
                      << "\n";
        }
    }

    // JSON output
    if (cfg.jsonMode) {
        std::cout << "{\n";
        std::cout << "  \"benchmark\": \"MemoryBenchmark\",\n";
        std::cout << "  \"iterations\": " << cfg.iterations << ",\n";
        std::cout << "  \"cases\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            std::cout << "    {\n";
            std::cout << "      \"case_id\": \"" << jsonEscape(r.caseId) << "\",\n";
            std::cout << "      \"name\": \"" << jsonEscape(r.name) << "\",\n";
            std::cout << "      \"arena_allocs\": " << r.arenaAllocs << ",\n";
            std::cout << "      \"arena_bytes\": " << r.arenaBytes << ",\n";
            std::cout << "      \"profiler_allocs\": " << r.profilerAllocs << ",\n";
            std::cout << "      \"profiler_frees\": " << r.profilerFrees << ",\n";
            std::cout << "      \"profiler_bytes\": " << r.profilerBytes << ",\n";
            std::cout << "      \"median_ms\": " << std::fixed << std::setprecision(4) << r.medianMs << ",\n";
            std::cout << "      \"mean_ms\": " << std::fixed << std::setprecision(4) << r.meanMs << ",\n";
            std::cout << "      \"best_ms\": " << std::fixed << std::setprecision(4) << r.bestMs << ",\n";
            std::cout << "      \"worst_ms\": " << std::fixed << std::setprecision(4) << r.worstMs << ",\n";
            std::cout << "      \"cv\": " << std::fixed << std::setprecision(6) << r.cv << "\n";
            std::cout << "    }";
            if (i + 1 < results.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ]\n";
        std::cout << "}\n";
    }

    return 0;
}
