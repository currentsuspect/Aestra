// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
// Performance benchmark for Sinc interpolators to validate "Trig Reduction" optimization.

#include "Interpolators.h"
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <random>

using namespace Nomad::Audio;
using namespace Nomad::Audio::Interpolators;

// Prevent compiler optimization
volatile float g_sink = 0.0f;

struct BenchmarkResult {
    std::string name;
    double avgTimeUs;
    double mframesPerSec;
};

template<typename Interpolator>
BenchmarkResult runBenchmark(const std::string& name, const float* input, int64_t inputFrames, float* output, int outputFrames) {
    auto t0 = std::chrono::high_resolution_clock::now();
    
    // Simulate resampling 100 blocks of 1024 samples
    const int kBlocks = 1000;
    const int kBlockSize = 256;
    
    // Random phase to trigger interpolation (unaligned)
    double phaseStep = 1.0; // 1:1 resampling for simplicity, we just want to measure compute
    double phase = 0.5; // Start offset
    
    int64_t operations = 0;
    
    for (int b=0; b<kBlocks; ++b) {
        for (int i=0; i<kBlockSize; ++i) {
            float l, r;
            Interpolator::interpolate(input, inputFrames, phase, l, r);
            g_sink += l + r;
            
            phase += phaseStep;
            if (phase >= inputFrames - 64) phase = 0.5; // Wrap around safely
        }
        operations += kBlockSize;
    }
    
    auto t1 = std::chrono::high_resolution_clock::now();
    double totalUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    
    BenchmarkResult res;
    res.name = name;
    res.avgTimeUs = totalUs / kBlocks;
    res.mframesPerSec = (double)operations / totalUs; // MFrames/sec (since us is 1e-6)
    
    return res;
}

int main() {
    std::cout << "========================================\n";
    std::cout << " SINC INTERPOLATOR BENCHMARK\n";
    std::cout << " Validating Trig Reduction Optimization\n";
    std::cout << "========================================\n\n";
    
    // Setup Data
    const int kInputSize = 48000;
    std::vector<float> input(kInputSize * 2);
    std::vector<float> output(256 * 2); // Dummy output
    
    // Fill with random noise to prevent bad branch prediction / zero optimizations
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for(size_t i=0; i<input.size(); ++i) input[i] = dist(rng);
    
    // Benchmarks
    std::vector<BenchmarkResult> results;
    
    std::cout << "Starting tests...\n";
    std::cout << "Testing Cubic...\n";
    results.push_back(runBenchmark<CubicInterpolator>("Cubic (4-point)", input.data(), kInputSize, output.data(), 0));
    std::cout << "Testing Sinc8...\n";
    results.push_back(runBenchmark<Sinc8Interpolator>("Sinc8 (8-point)", input.data(), kInputSize, output.data(), 0));
    std::cout << "Testing Sinc64 (Original Opt)...\n";
    results.push_back(runBenchmark<Sinc64Interpolator>("Sinc64 (Original Opt)", input.data(), kInputSize, output.data(), 0));
    std::cout << "Testing Sinc64 TURBO...\n";
    std::cout << "  CPU Features:\n";
    std::cout << "    AVX512F: " << (Nomad::Core::CPUDetection::get().hasAVX512F() ? "YES" : "NO") << "\n";
    std::cout << "    AVX2:   " << (Nomad::Core::CPUDetection::get().hasAVX2() ? "YES" : "NO") << "\n";
    std::cout << "    FMA:    " << (Nomad::Core::CPUDetection::get().hasFMA() ? "YES" : "NO") << "\n";
    std::cout << "    SSE4.1: " << (Nomad::Core::CPUDetection::get().hasSSE41() ? "YES" : "NO") << "\n";
    std::cout << "    NEON:   " << (Nomad::Core::CPUDetection::get().hasNEON() ? "YES" : "NO") << "\n";
    results.push_back(runBenchmark<Sinc64Turbo>("Sinc64 TURBO (Multi-SIMD)", input.data(), kInputSize, output.data(), 0));
    std::cout << "All tests completed.\n\n";
    
    std::cout << std::left << std::setw(20) << "Algorithm" 
              << "| " << std::setw(15) << "MFrame/sec" 
              << "| " << std::setw(10) << "Relative" << "\n";
    std::cout << "--------------------|----------------|-----------\n";
    
    double baseRate = results[0].mframesPerSec;
    
    for(const auto& res : results) {
        std::cout << std::left << std::setw(20) << res.name 
                  << "| " << std::setw(15) << std::fixed << std::setprecision(2) << res.mframesPerSec 
                  << "| " << std::setw(10) << std::setprecision(2) << (res.mframesPerSec / baseRate) << "x" << "\n";
    }
    
    std::cout << "\nAnalysis:\n";
    double s8 = results[1].mframesPerSec;
    double s64 = results[3].mframesPerSec;
    double ratio = s8 / s64;
    
    std::cout << "Sinc64 is " << std::setprecision(2) << ratio << "x slower than Sinc8.\n";
    std::cout << "(Theoretical non-optimized would be ~8x slower strictly due to taps,\n"; 
    std::cout << " but with Trig Reduction, the overhead is purely MAC + RAM, so it should be efficient.)\n";
    
    return 0;
}
