// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../include/AestraMath.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra;
using Clock = std::chrono::steady_clock;
using DurationNs = int64_t; // nanoseconds

// =============================================================================
// Statistics helpers
// =============================================================================

struct Stats {
    DurationNs median{0};
    DurationNs p95{0};
    DurationNs p99{0};
    DurationNs min{0};
    DurationNs max{0};
    double mean{0};
    double stddev{0};
    int xruns{0};          // operations exceeding 10× median
    int missedDeadlines{0}; // operations exceeding a hard deadline
    int samples{0};
    DurationNs deadlineNs{0};
};

void computeStats(Stats& s, std::vector<DurationNs>& samples, DurationNs deadlineNs = 0) {
    s.samples = static_cast<int>(samples.size());
    s.deadlineNs = deadlineNs;
    if (samples.empty()) return;

    std::sort(samples.begin(), samples.end());
    s.min = samples.front();
    s.max = samples.back();
    s.median = samples[samples.size() / 2];
    s.p95 = samples[static_cast<size_t>(samples.size() * 0.95)];
    s.p99 = samples[static_cast<size_t>(samples.size() * 0.99)];

    double sum = 0;
    for (auto v : samples) sum += v;
    s.mean = sum / samples.size();

    double sqSum = 0;
    for (auto v : samples) sqSum += (v - s.mean) * (v - s.mean);
    s.stddev = std::sqrt(sqSum / samples.size());

    DurationNs xrunThreshold = s.median > 0 ? s.median * 10 : 10'000'000;
    for (auto v : samples) {
        if (v > xrunThreshold) s.xruns++;
        if (deadlineNs > 0 && v > deadlineNs) s.missedDeadlines++;
    }
}

std::string statsJson(const std::string& name, const Stats& s) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    os << "    \"" << name << "\": {\n";
    os << "      \"samples\": " << s.samples << ",\n";
    os << "      \"median_ns\": " << s.median << ",\n";
    os << "      \"p95_ns\": " << s.p95 << ",\n";
    os << "      \"p99_ns\": " << s.p99 << ",\n";
    os << "      \"min_ns\": " << s.min << ",\n";
    os << "      \"max_ns\": " << s.max << ",\n";
    os << "      \"mean_ns\": " << s.mean << ",\n";
    os << "      \"stddev_ns\": " << s.stddev << ",\n";
    os << "      \"xruns\": " << s.xruns << ",\n";
    os << "      \"missed_deadlines\": " << s.missedDeadlines << ",\n";
    os << "      \"deadline_ns\": " << s.deadlineNs << "\n";
    os << "    }";
    return os.str();
}

// =============================================================================
// Benchmark: Vector3 Arithmetic (add/sub/mul/div)
// =============================================================================

Stats benchVector3Arithmetic(int iterations = 1'000'000) {
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    Vector3 a(1.0f, 2.0f, 3.0f);
    Vector3 b(4.0f, 5.0f, 6.0f);
    float scalar = 2.5f;
    volatile float sink = 0; // prevent compiler optimization

    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        Vector3 r = a + b;
        r = r - a;
        r = r * scalar;
        r = r / scalar;
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        sink += r.x; // prevent dead-code elimination
    }
    (void)sink;

    Stats s;
    // Deadline: 100ns for 4 vector ops
    computeStats(s, samples, 100);
    return s;
}

// =============================================================================
// Benchmark: Vector3 Normalization
// =============================================================================

Stats benchVector3Normalization(int iterations = 500'000) {
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    Vector3 v(3.0f, 4.0f, 0.0f);
    volatile float sink = 0;

    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        Vector3 n = v.normalized();
        float len = n.length();
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        sink += len;
    }
    (void)sink;

    Stats s;
    // Deadline: 200ns for normalize + length
    computeStats(s, samples, 200);
    return s;
}

// =============================================================================
// Benchmark: Matrix4x4 Multiplication
// =============================================================================

Stats benchMatrixMultiplication(int iterations = 100'000) {
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    Matrix4x4 a = Matrix4x4::translation(1.0f, 2.0f, 3.0f);
    Matrix4x4 b = Matrix4x4::scale(2.0f, 2.0f, 2.0f);
    volatile float sink = 0;

    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        Matrix4x4 r = a * b;
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        sink += r.m[0];
    }
    (void)sink;

    Stats s;
    // Deadline: 500ns for 4×4 matrix multiply
    computeStats(s, samples, 500);
    return s;
}

// =============================================================================
// Benchmark: Matrix-Vector Transform
// =============================================================================

Stats benchMatrixVectorTransform(int iterations = 500'000) {
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    Matrix4x4 m = Matrix4x4::translation(1.0f, 2.0f, 3.0f);
    Vector4 v(1.0f, 0.0f, 0.0f, 1.0f);
    volatile float sink = 0;

    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        Vector4 r = m * v;
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        sink += r.x;
    }
    (void)sink;

    Stats s;
    // Deadline: 200ns for matrix × vector
    computeStats(s, samples, 200);
    return s;
}

// =============================================================================
// Benchmark: DSP Math (Scalar)
// =============================================================================

Stats benchDSPMath(int iterations = 1'000'000) {
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    volatile float sink = 0;

    for (int i = 0; i < iterations; ++i) {
        float t = (i % 1000) / 1000.0f;
        auto t0 = Clock::now();
        float v1 = lerp(0.0f, 100.0f, t);
        float v2 = clamp(v1, 0.0f, 100.0f);
        float v3 = smoothstep(0.0f, 100.0f, v2);
        float v4 = dbToGain(v3 - 50.0f);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        sink += v4;
    }
    (void)sink;

    Stats s;
    // Deadline: 500ns for lerp + clamp + smoothstep + dbToGain
    computeStats(s, samples, 500);
    return s;
}

// =============================================================================
// Benchmark: Batch Transform (1000 vectors through one matrix)
// =============================================================================

Stats benchBatchTransform(int iterations = 10'000) {
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    Matrix4x4 m = Matrix4x4::rotationY(0.5f) * Matrix4x4::translation(0.0f, 1.0f, 0.0f);
    constexpr int batchSize = 1000;

    // Pre-allocate vectors
    Vector4 input[batchSize];
    for (int i = 0; i < batchSize; ++i) {
        input[i] = Vector4(
            static_cast<float>(i) / batchSize,
            0.0f,
            static_cast<float>(batchSize - i) / batchSize,
            1.0f);
    }

    volatile float sink = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        auto t0 = Clock::now();
        for (int i = 0; i < batchSize; ++i) {
            Vector4 r = m * input[i];
            sink += r.x + r.y + r.z;
        }
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }
    (void)sink;

    Stats s;
    // Deadline: 50μs for 1000 vector transforms
    computeStats(s, samples, 50'000);
    return s;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  AestraCore Math Benchmark" << std::endl;
    std::cout << "==================================\n" << std::endl;

    auto overallStart = Clock::now();

    std::cout << "[1/6] Vector3 arithmetic..." << std::flush;
    Stats vec3Arith = benchVector3Arithmetic();
    std::cout << " done (" << vec3Arith.samples << " samples)\n";
    std::cout << "  median=" << vec3Arith.median << "ns p95=" << vec3Arith.p95
              << "ns p99=" << vec3Arith.p99 << "ns xruns=" << vec3Arith.xruns
              << " missed=" << vec3Arith.missedDeadlines << "\n" << std::endl;

    std::cout << "[2/6] Vector3 normalization..." << std::flush;
    Stats vec3Norm = benchVector3Normalization();
    std::cout << " done (" << vec3Norm.samples << " samples)\n";
    std::cout << "  median=" << vec3Norm.median << "ns p95=" << vec3Norm.p95
              << "ns p99=" << vec3Norm.p99 << "ns xruns=" << vec3Norm.xruns
              << " missed=" << vec3Norm.missedDeadlines << "\n" << std::endl;

    std::cout << "[3/6] Matrix4x4 multiplication..." << std::flush;
    Stats matMul = benchMatrixMultiplication();
    std::cout << " done (" << matMul.samples << " samples)\n";
    std::cout << "  median=" << matMul.median << "ns p95=" << matMul.p95
              << "ns p99=" << matMul.p99 << "ns xruns=" << matMul.xruns
              << " missed=" << matMul.missedDeadlines << "\n" << std::endl;

    std::cout << "[4/6] Matrix-vector transform..." << std::flush;
    Stats matVec = benchMatrixVectorTransform();
    std::cout << " done (" << matVec.samples << " samples)\n";
    std::cout << "  median=" << matVec.median << "ns p95=" << matVec.p95
              << "ns p99=" << matVec.p99 << "ns xruns=" << matVec.xruns
              << " missed=" << matVec.missedDeadlines << "\n" << std::endl;

    std::cout << "[5/6] DSP math (scalar)..." << std::flush;
    Stats dsp = benchDSPMath();
    std::cout << " done (" << dsp.samples << " samples)\n";
    std::cout << "  median=" << dsp.median << "ns p95=" << dsp.p95
              << "ns p99=" << dsp.p99 << "ns xruns=" << dsp.xruns
              << " missed=" << dsp.missedDeadlines << "\n" << std::endl;

    std::cout << "[6/6] Batch transform (1000 vectors)..." << std::flush;
    Stats batch = benchBatchTransform();
    std::cout << " done (" << batch.samples << " samples)\n";
    std::cout << "  median=" << batch.median << "ns p95=" << batch.p95
              << "ns p99=" << batch.p99 << "ns xruns=" << batch.xruns
              << " missed=" << batch.missedDeadlines << "\n" << std::endl;

    auto overallEnd = Clock::now();
    auto overallMs = std::chrono::duration_cast<std::chrono::milliseconds>(overallEnd - overallStart).count();

    // Output JSON
    std::cout << "==================================" << std::endl;
    std::cout << "  JSON Output" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "{\n";
    std::cout << "  \"total_duration_ms\": " << overallMs << ",\n";
    std::cout << statsJson("vector3_arithmetic_ns", vec3Arith) << ",\n";
    std::cout << statsJson("vector3_normalization_ns", vec3Norm) << ",\n";
    std::cout << statsJson("matrix_multiplication_ns", matMul) << ",\n";
    std::cout << statsJson("matrix_vector_transform_ns", matVec) << ",\n";
    std::cout << statsJson("dsp_math_scalar_ns", dsp) << ",\n";
    std::cout << statsJson("batch_transform_1000_ns", batch) << "\n";
    std::cout << "}\n";

    // Summary verdict
    int totalXruns = vec3Arith.xruns + vec3Norm.xruns + matMul.xruns +
                     matVec.xruns + dsp.xruns + batch.xruns;
    int totalMissed = vec3Arith.missedDeadlines + vec3Norm.missedDeadlines +
                      matMul.missedDeadlines + matVec.missedDeadlines +
                      dsp.missedDeadlines + batch.missedDeadlines;

    int totalSamples = vec3Arith.samples + vec3Norm.samples + matMul.samples +
                       matVec.samples + dsp.samples + batch.samples;
    double xrunRate = totalSamples > 0 ? (double)totalXruns / totalSamples : 0;
    double missRate = totalSamples > 0 ? (double)totalMissed / totalSamples : 0;

    std::cout << "\n==================================" << std::endl;
    std::cout << "  Benchmark Verdict" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "  Total samples: " << totalSamples << std::endl;
    std::cout << "  Total XRUNs: " << totalXruns << " (" << std::fixed << std::setprecision(3) << (xrunRate * 100) << "%)" << std::endl;
    std::cout << "  Total missed deadlines: " << totalMissed << " (" << (missRate * 100) << "%)" << std::endl;

    bool gatePass = (xrunRate < 0.001) && (missRate < 0.001);

    if (!gatePass) {
        std::cerr << "  ⚠ BENCHMARK GATE FAILED" << std::endl;
        if (xrunRate >= 0.001) std::cerr << "  XRUN rate " << (xrunRate * 100) << "% >= 0.1% threshold" << std::endl;
        if (missRate >= 0.001) std::cerr << "  Deadline miss rate " << (missRate * 100) << "% >= 0.1% threshold" << std::endl;
        return 2;
    }

    std::cout << "  ✓ All benchmarks within thresholds" << std::endl;
    return 0;
}
