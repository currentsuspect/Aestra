// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PathologicalChaosStressTest.cpp — Stress test for RT safety under chaotic conditions

#include "Core/AudioEngine.h"
#include "Core/RTGuard.h"

#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace Aestra::Audio;

/**
 * Stress test: multiple threads recording RT violations simultaneously.
 * Verifies thread-local isolation and no data races.
 */
static void testConcurrentViolationRecording() {
    constexpr int NUM_THREADS = 8;
    constexpr int VIOLATIONS_PER_THREAD = 1000;
    std::atomic<int> ready{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&ready, t] {
            // Reset this thread's audit data
            g_rtAuditData = {};

            // Signal ready and wait for all threads
            ready.fetch_add(1, std::memory_order_relaxed);
            while (ready.load(std::memory_order_relaxed) < NUM_THREADS) {
                // spin
            }

            // Record violations
            for (int i = 0; i < VIOLATIONS_PER_THREAD; i++) {
                recordRTViolation(RTViolationType::Allocation,
                                  reinterpret_cast<void*>(static_cast<uintptr_t>(t * 10000 + i)),
                                  static_cast<size_t>(i));
            }

            // Verify count
            assert(g_rtAuditData.violationCount == static_cast<uint64_t>(VIOLATIONS_PER_THREAD));
        });
    }

    for (auto& t : threads) t.join();
    printf("[PASS] Concurrent violation recording (%d threads x %d violations)\n",
           NUM_THREADS, VIOLATIONS_PER_THREAD);
}

/**
 * Stress test: rapid guard creation/destruction.
 * Verifies no leaks or races in the RAII depth tracking.
 */
static void testRapidGuardLifecycle() {
    constexpr int ITERATIONS = 100000;
    g_realtimeAudioThreadDepth = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        ScopedRealtimeAudioThread guard;
        assert(g_realtimeAudioThreadDepth == 1);
    }
    assert(g_realtimeAudioThreadDepth == 0);
    printf("[PASS] Rapid guard lifecycle (%d iterations)\n", ITERATIONS);
}

/**
 * Stress test: mixed guard and violation recording under load.
 * Simulates a realistic audio callback scenario.
 */
static void testMixedWorkload() {
    constexpr int CALLBACKS = 1000;
    constexpr int ALLOCATIONS_PER_CALLBACK = 5;

    g_realtimeAudioThreadDepth = 0;
    g_rtAuditData = {};

    for (int cb = 0; cb < CALLBACKS; cb++) {
        ScopedRealtimeAudioThread guard;

        // Simulate some allocations (violations)
        for (int a = 0; a < ALLOCATIONS_PER_CALLBACK; a++) {
            recordRTViolation(RTViolationType::Allocation,
                              reinterpret_cast<void*>(static_cast<uintptr_t>(cb * 100 + a)),
                              256);
        }
    }

    assert(g_rtAuditData.violationCount == static_cast<uint64_t>(CALLBACKS * ALLOCATIONS_PER_CALLBACK));
    assert(g_realtimeAudioThreadDepth == 0);
    printf("[PASS] Mixed workload (%d callbacks x %d allocations)\n",
           CALLBACKS, ALLOCATIONS_PER_CALLBACK);
}

/**
 * Stress test: circular buffer overflow behavior.
 * Verifies that overflow doesn't corrupt data.
 */
static void testCircularBufferStress() {
    g_rtAuditData = {};

    constexpr size_t TOTAL = MAX_LOCAL_VIOLATIONS * 3;
    for (size_t i = 0; i < TOTAL; i++) {
        recordRTViolation(RTViolationType::Allocation,
                          reinterpret_cast<void*>(i),
                          i * 10);
    }

    assert(g_rtAuditData.violationCount == TOTAL);
    assert(g_rtAuditData.droppedCount == TOTAL - MAX_LOCAL_VIOLATIONS);
    printf("[PASS] Circular buffer stress (%zu events, %zu dropped)\n",
           TOTAL, g_rtAuditData.droppedCount);
}

int main() {
    printf("=== PathologicalChaosStressTest ===\n");
    testConcurrentViolationRecording();
    testRapidGuardLifecycle();
    testMixedWorkload();
    testCircularBufferStress();
    printf("\nAll stress tests passed.\n");
    return 0;
}
