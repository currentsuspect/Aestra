// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Tests for the AudioArena allocator — correctness, alignment, thread safety.

#include "AestraMemory.h"
#include "../../AestraAudio/include/DSP/AudioProcessor.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

static int g_passes = 0;
static int g_fails = 0;

#define CHECK(cond, msg) do { \
    if (cond) { \
        std::cout << "[PASS] " << msg << "\n"; \
        g_passes++; \
    } else { \
        std::cout << "[FAIL] " << msg << "\n"; \
        g_fails++; \
    } \
} while(0)

// ============================================================================
// Test: Basic allocation
// ============================================================================

void testBasicAllocation() {
    std::cout << "\n=== Test: Basic Allocation ===\n";

    AudioArena arena(4096);
    void* p1 = arena.allocate(100);
    void* p2 = arena.allocate(200);

    CHECK(p1 != nullptr, "First allocation succeeds");
    CHECK(p2 != nullptr, "Second allocation succeeds");
    CHECK(p1 != p2, "Two allocations return different pointers");
    CHECK(arena.used() >= 300, "Used bytes >= sum of allocations");
}

// ============================================================================
// Test: Alignment
// ============================================================================

void testAlignment() {
    std::cout << "\n=== Test: Alignment ===\n";

    AudioArena arena(4096);

    // 16-byte alignment (common for SIMD)
    void* p1 = arena.allocate(10, 16);
    CHECK(p1 != nullptr, "16-byte aligned allocation succeeds");
    CHECK(reinterpret_cast<uintptr_t>(p1) % 16 == 0, "Pointer is 16-byte aligned");

    // 32-byte alignment (AVX)
    void* p2 = arena.allocate(10, 32);
    CHECK(p2 != nullptr, "32-byte aligned allocation succeeds");
    auto p2addr = reinterpret_cast<uintptr_t>(p2);
    std::cout << "  p2 address: " << std::hex << p2addr << " mod 32 = " << (p2addr % 32) << std::dec << "\n";
    CHECK(p2addr % 32 == 0, "Pointer is 32-byte aligned");

    // Float alignment
    void* p3 = arena.allocate(100, alignof(float));
    CHECK(p3 != nullptr, "Float-aligned allocation succeeds");
    CHECK(reinterpret_cast<uintptr_t>(p3) % alignof(float) == 0, "Pointer is float-aligned");
}

// ============================================================================
// Test: Boundary — out of space
// ============================================================================

void testOutOfSpace() {
    std::cout << "\n=== Test: Out of Space ===\n";

    AudioArena arena(256);

    void* p1 = arena.allocate(200);
    CHECK(p1 != nullptr, "First 200-byte alloc succeeds in 256-byte arena");

    void* p2 = arena.allocate(100);
    CHECK(p2 == nullptr, "Second allocation fails when arena is full");

    CHECK(arena.remaining() < 100, "Remaining bytes < requested size");
}

// ============================================================================
// Test: Reset clears everything
// ============================================================================

void testReset() {
    std::cout << "\n=== Test: Arena Reset ===\n";

    AudioArena arena(4096);

    arena.allocate(100);
    arena.allocate(200);
    arena.allocate(300);
    CHECK(arena.allocationCount() == 3, "Allocation count is 3");
    CHECK(arena.used() > 0, "Used bytes > 0");

    arena.reset();
    CHECK(arena.used() == 0, "Used bytes == 0 after reset");
    CHECK(arena.allocationCount() == 0, "Allocation count == 0 after reset");
    CHECK(arena.peakUsage() == 0, "Peak usage == 0 after reset");
    CHECK(arena.remaining() == arena.capacity(), "Remaining == capacity after reset");

    // Verify arena is reusable after reset
    void* p = arena.allocate(500);
    CHECK(p != nullptr, "Allocation succeeds after reset");
}

// ============================================================================
// Test: Peak tracking
// ============================================================================

void testPeakTracking() {
    std::cout << "\n=== Test: Peak Tracking ===\n";

    AudioArena arena(4096);

    arena.allocate(100);
    size_t peak1 = arena.peakUsage();
    CHECK(peak1 >= 100, "Peak >= 100 after first alloc");

    arena.allocate(500);
    size_t peak2 = arena.peakUsage();
    CHECK(peak2 >= peak1, "Peak only increases");
}

// ============================================================================
// Test: Edge cases
// ============================================================================

void testEdgeCases() {
    std::cout << "\n=== Test: Edge Cases ===\n";

    AudioArena arena(4096);

    // Zero-size allocation
    void* p1 = arena.allocate(0);
    CHECK(p1 == nullptr, "Zero-size allocation returns nullptr");

    // Non-power-of-2 alignment (should return nullptr)
    void* p2 = arena.allocate(100, 3);
    CHECK(p2 == nullptr, "Non-power-of-2 alignment returns nullptr");

    // Tiny arena
    AudioArena tiny(1);
    void* p3 = tiny.allocate(1);
    CHECK(p3 != nullptr, "1-byte allocation in 1-byte arena");
}

// ============================================================================
// Test: Thread safety (multi-producer)
// ============================================================================

void testThreadSafety() {
    std::cout << "\n=== Test: Thread Safety ===\n";

    AudioArena arena(1024 * 1024); // 1 MB
    const int kThreads = 4;
    const int kAllocsPerThread = 1000;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    auto worker = [&]() {
        for (int i = 0; i < kAllocsPerThread; ++i) {
            void* p = arena.allocate(64, 16);
            if (p) {
                // Write to memory to prevent dead-code elimination
                std::memset(p, 0xAA, 64);
                successCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                failCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    int total = successCount.load() + failCount.load();
    CHECK(total == kThreads * kAllocsPerThread,
          "Total attempts == threads * allocs (" + std::to_string(total) +
          " == " + std::to_string(kThreads * kAllocsPerThread) + ")");
    CHECK(successCount.load() > 0, "Some allocations succeeded");
    // Some failures are expected if arena fills up — that's correct behavior
    CHECK(arena.allocationCount() == static_cast<size_t>(successCount.load()),
          "Arena alloc count matches successful allocations");
}

// ============================================================================
// Test: GlobalAudioArena singleton
// ============================================================================

void testGlobalArena() {
    std::cout << "\n=== Test: Global Audio Arena ===\n";

    auto& ga = GlobalAudioArena::instance();
    CHECK(ga.capacity() > 0, "Global arena has positive capacity");

    void* p = ga.allocate(1024, alignof(float));
    CHECK(p != nullptr, "Global arena allocation succeeds");

    size_t used1 = ga.used();
    CHECK(used1 >= 1024, "Used bytes >= allocation size");
}

// ============================================================================
// Test: AudioBufferManager integration
// ============================================================================

void testAudioBufferManagerIntegration() {
    std::cout << "\n=== Test: AudioBufferManager Integration ===\n";

    // Reset global arena to ensure clean state
    GlobalAudioArena::instance().reset();

    // Create AudioBufferManager — it uses GlobalAudioArena internally
    AudioBufferManager mgr;

    void* buf = mgr.allocate(1024, 2);
    CHECK(buf != nullptr, "AudioBufferManager allocate succeeds");

    // Write to buffer
    float* fbuf = static_cast<float*>(buf);
    fbuf[0] = 1.0f;
    fbuf[1] = -1.0f;
    CHECK(fbuf[0] == 1.0f, "Buffer is writable and readable");

    mgr.clear();
    CHECK(fbuf[0] == 0.0f, "Buffer is zeroed after clear");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=========================================\n";
    std::cout << "  Aestra Memory Allocator Test Suite\n";
    std::cout << "=========================================\n";

    testBasicAllocation();
    testAlignment();
    testOutOfSpace();
    testReset();
    testPeakTracking();
    testEdgeCases();
    testThreadSafety();
    testGlobalArena();
    testAudioBufferManagerIntegration();

    std::cout << "\n=========================================\n";
    std::cout << "  Test Summary\n";
    std::cout << "=========================================\n";
    std::cout << "  Passed: " << g_passes << "\n";
    std::cout << "  Failed: " << g_fails << "\n";
    std::cout << "  Total:  " << (g_passes + g_fails) << "\n";
    std::cout << "=========================================\n";

    return g_fails > 0 ? 1 : 0;
}
