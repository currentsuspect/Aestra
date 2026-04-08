// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../include/AestraThreading.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
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

    DurationNs deadlineNs{0}; // configured deadline for this benchmark
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

    DurationNs xrunThreshold = s.median > 0 ? s.median * 10 : 10'000'000; // 10ms fallback
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
// Benchmark: Ring Buffer Push/Pop (single-threaded, micro-latency)
// =============================================================================

Stats benchRingBufferLatency(int iterations = 100'000) {
    LockFreeRingBuffer<int, 1024> buffer;
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    int pushVal = 42;
    int popVal = 0;
    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        if (!buffer.push(pushVal)) { /* should not happen in empty buffer */ }
        if (!buffer.pop(popVal)) { /* should not happen */ }
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    Stats s;
    // Deadline: 1μs per push+pop pair is generous for lock-free ops
    computeStats(s, samples, 1'000);
    return s;
}

// =============================================================================
// Benchmark: Ring Buffer Contended (producer + consumer)
// =============================================================================

Stats benchRingBufferContention(int items = 100'000) {
    LockFreeRingBuffer<int, 1024> buffer;
    std::vector<DurationNs> pushSamples;
    std::vector<DurationNs> popSamples;
    pushSamples.reserve(items);
    popSamples.reserve(items);
    std::atomic<bool> producerDone{false};
    int consumed = 0;

    auto producer = [&]() {
        for (int i = 0; i < items; ++i) {
            auto t0 = Clock::now();
            while (!buffer.push(i)) {
                // spin on full — count the total time including backoff
                std::this_thread::yield();
            }
            auto t1 = Clock::now();
            pushSamples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
        producerDone = true;
    };

    auto consumer = [&]() {
        int value = 0;
        while (!producerDone || !buffer.isEmpty()) {
            auto t0 = Clock::now();
            if (buffer.pop(value)) {
                auto t1 = Clock::now();
                popSamples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    };

    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();

    Stats s;
    // Deadline: 10μs for a push under contention
    computeStats(s, pushSamples, 10'000);
    return s;
}

// =============================================================================
// Benchmark: ThreadPool Task Dispatch Latency
// =============================================================================

Stats benchThreadPoolDispatch(int tasks = 10'000) {
    ThreadPool pool(4);
    std::vector<DurationNs> roundTrip;
    roundTrip.reserve(tasks);
    std::atomic<int> completed{0};

    // Warm up
    for (int i = 0; i < 100; ++i) {
        (void)pool.enqueue([]{});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Measure enqueue → task start latency
    for (int i = 0; i < tasks; ++i) {
        auto t0 = Clock::now();
        (void)pool.enqueue([&roundTrip, t0, &completed]() {
            auto t1 = Clock::now();
            roundTrip.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            completed.fetch_add(1, std::memory_order_release);
        });
    }

    // Wait for completion
    while (completed.load(std::memory_order_acquire) < tasks) {
        std::this_thread::yield();
    }

    Stats s;
    // Deadline: 10ms round-trip for a trivial task (includes mutex + condvar wakeup)
    computeStats(s, roundTrip, 10'000'000);
    return s;
}

// =============================================================================
// Benchmark: Barrier Synchronization Latency
// =============================================================================
// Measures the cost of N threads reaching a barrier and the coordinator
// waiting for all of them. Workers do minimal work (a counter increment).

Stats benchBarrierSync(int iterations = 10'000, int numWorkers = 4) {
    std::vector<DurationNs> samples;
    samples.reserve(iterations);

    Barrier barrier(numWorkers);
    std::atomic<int> iterationCounter{0};
    std::atomic<bool> stopWorkers{false};
    std::atomic<int> startIteration{-1}; // workers wait for this to increment

    std::vector<std::thread> workers;
    for (int t = 0; t < numWorkers; ++t) {
        workers.emplace_back([&, t]() {
            int lastIter = -1;
            while (!stopWorkers.load(std::memory_order_acquire)) {
                // Wait for next iteration
                int target = lastIter + 1;
                while (startIteration.load(std::memory_order_acquire) < target) {
                    std::this_thread::yield();
                }
                lastIter = target;
                // Minimal work: increment shared counter
                iterationCounter.fetch_add(1, std::memory_order_relaxed);
                // Signal barrier
                barrier.signal();
            }
        });
    }

    for (int iter = 0; iter < iterations; ++iter) {
        // Reset barrier for this round
        barrier.reset(numWorkers);
        auto t0 = Clock::now();
        // Signal workers to start
        startIteration.store(iter, std::memory_order_release);
        // Wait for all workers to reach barrier
        barrier.wait();
        auto t1 = Clock::now();

        samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        // Wait until counter confirms all workers completed
        while (iterationCounter.load(std::memory_order_acquire) < (iter + 1) * numWorkers) {
            std::this_thread::yield();
        }
    }

    stopWorkers.store(true, std::memory_order_release);
    startIteration.store(2000000000, std::memory_order_release); // unblock workers
    for (auto& w : workers) w.join();

    Stats s;
    // Deadline: 50μs for barrier sync (spin-wait, workers do 1 atomic op)
    computeStats(s, samples, 50'000);
    return s;
}

// =============================================================================
// Benchmark: SpinLock Contention
// =============================================================================

Stats benchSpinLockContention(int iterations = 50'000, int threads = 4) {
    SpinLock lock;
    std::vector<DurationNs> samples;
    samples.reserve(iterations);
    std::atomic<int> totalOps{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&]() {
            for (int i = 0; i < iterations / threads; ++i) {
                auto t0 = Clock::now();
                lock.lock();
                // Critical section: trivial increment
                totalOps.fetch_add(1, std::memory_order_relaxed);
                lock.unlock();
                auto t1 = Clock::now();
                samples.push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            }
        });
    }

    for (auto& w : workers) w.join();

    Stats s;
    // Deadline: 5μs to acquire + critical section under contention
    computeStats(s, samples, 5'000);
    return s;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  AestraCore Threading Benchmark" << std::endl;
    std::cout << "==================================\n" << std::endl;

    auto overallStart = Clock::now();

    std::cout << "[1/5] Ring buffer latency (single-threaded)..." << std::flush;
    Stats rbLatency = benchRingBufferLatency();
    std::cout << " done (" << rbLatency.samples << " samples)\n";
    std::cout << "  median=" << rbLatency.median << "ns p95=" << rbLatency.p95
              << "ns p99=" << rbLatency.p99 << "ns xruns=" << rbLatency.xruns
              << " missed=" << rbLatency.missedDeadlines << "\n" << std::endl;

    std::cout << "[2/5] Ring buffer contention (producer+consumer)..." << std::flush;
    Stats rbContention = benchRingBufferContention();
    std::cout << " done (" << rbContention.samples << " samples)\n";
    std::cout << "  median=" << rbContention.median << "ns p95=" << rbContention.p95
              << "ns p99=" << rbContention.p99 << "ns xruns=" << rbContention.xruns
              << " missed=" << rbContention.missedDeadlines << "\n" << std::endl;

    std::cout << "[3/5] ThreadPool dispatch round-trip..." << std::flush;
    Stats tpDispatch = benchThreadPoolDispatch();
    std::cout << " done (" << tpDispatch.samples << " samples)\n";
    std::cout << "  median=" << tpDispatch.median << "ns p95=" << tpDispatch.p95
              << "ns p99=" << tpDispatch.p99 << "ns xruns=" << tpDispatch.xruns
              << " missed=" << tpDispatch.missedDeadlines << "\n" << std::endl;

    std::cout << "[4/5] Barrier synchronization (4 threads)..." << std::flush;
    Stats barrier = benchBarrierSync();
    std::cout << " done (" << barrier.samples << " samples)\n";
    std::cout << "  median=" << barrier.median << "ns p95=" << barrier.p95
              << "ns p99=" << barrier.p99 << "ns xruns=" << barrier.xruns
              << " missed=" << barrier.missedDeadlines << "\n" << std::endl;

    std::cout << "[5/5] SpinLock contention (4 threads)..." << std::flush;
    Stats spinlock = benchSpinLockContention();
    std::cout << " done (" << spinlock.samples << " samples)\n";
    std::cout << "  median=" << spinlock.median << "ns p95=" << spinlock.p95
              << "ns p99=" << spinlock.p99 << "ns xruns=" << spinlock.xruns
              << " missed=" << spinlock.missedDeadlines << "\n" << std::endl;

    auto overallEnd = Clock::now();
    auto overallMs = std::chrono::duration_cast<std::chrono::milliseconds>(overallEnd - overallStart).count();

    // Output JSON
    std::cout << "==================================" << std::endl;
    std::cout << "  JSON Output" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "{\n";
    std::cout << "  \"total_duration_ms\": " << overallMs << ",\n";
    std::cout << statsJson("ring_buffer_latency_ns", rbLatency) << ",\n";
    std::cout << statsJson("ring_buffer_contention_ns", rbContention) << ",\n";
    std::cout << statsJson("threadpool_dispatch_ns", tpDispatch) << ",\n";
    std::cout << statsJson("barrier_sync_ns", barrier) << ",\n";
    std::cout << statsJson("spinlock_contention_ns", spinlock) << "\n";
    std::cout << "}\n";

    // Summary verdict
    int totalXruns = rbLatency.xruns + rbContention.xruns + tpDispatch.xruns +
                     barrier.xruns + spinlock.xruns;
    int totalMissed = rbLatency.missedDeadlines + rbContention.missedDeadlines +
                      tpDispatch.missedDeadlines + barrier.missedDeadlines + spinlock.missedDeadlines;

    // Verdict: XRUN rate must be < 0.1% of total samples
    // Missed deadlines must be < 0.1% of total samples
    // These thresholds account for OS scheduler jitter on non-realtime machines
    int totalSamples = rbLatency.samples + rbContention.samples + tpDispatch.samples +
                       barrier.samples + spinlock.samples;
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
        return 2; // special exit code for benchmark failure
    }

    std::cout << "  ✓ All benchmarks within thresholds" << std::endl;
    return 0;
}
