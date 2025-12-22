// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief Lightweight telemetry counters updated from the RT thread.
 *
 * All fields are atomics for lock-free access; UI/non-RT code should snapshot
 * these periodically and handle presentation/logging off the audio thread.
 * All access uses relaxed memory ordering for optimal real-time performance.
 */
struct AudioTelemetry {
    std::atomic<uint64_t> blocksProcessed{0};
    std::atomic<uint64_t> xruns{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> maxCallbackNs{0};
    std::atomic<uint64_t> lastCallbackNs{0};

    // Callback budget context (set from the audio thread wrapper)
    std::atomic<uint32_t> lastBufferFrames{0};
    std::atomic<uint32_t> lastSampleRate{0};

    // Cycle counter calibration (Hz). If 0, callback ns timing may be unavailable.
    std::atomic<uint64_t> cycleHz{0};

    // SRC activity: number of processed blocks that executed resampling work.
    std::atomic<uint64_t> srcActiveBlocks{0};

    // Convenience methods for relaxed memory ordering access
    // Increments
    void incrementBlocksProcessed() noexcept { blocksProcessed.fetch_add(1, std::memory_order_relaxed); }
    void incrementXruns() noexcept { xruns.fetch_add(1, std::memory_order_relaxed); }
    void incrementUnderruns() noexcept { underruns.fetch_add(1, std::memory_order_relaxed); }
    void incrementOverruns() noexcept { overruns.fetch_add(1, std::memory_order_relaxed); }
    void incrementSrcActiveBlocks() noexcept { srcActiveBlocks.fetch_add(1, std::memory_order_relaxed); }
    
    // Updates
    void updateMaxCallbackNs(uint64_t ns) noexcept {
        uint64_t current = maxCallbackNs.load(std::memory_order_relaxed);
        while (ns > current) {
            if (maxCallbackNs.compare_exchange_weak(current, ns, std::memory_order_relaxed)) {
                break;
            }
        }
    }
    
    void updateLastCallbackNs(uint64_t ns) noexcept {
        lastCallbackNs.store(ns, std::memory_order_relaxed);
    }
    void updateLastBufferFrames(uint32_t frames) noexcept {
        lastBufferFrames.store(frames, std::memory_order_relaxed);
    }
    void updateLastSampleRate(uint32_t rate) noexcept {
        lastSampleRate.store(rate, std::memory_order_relaxed);
    }
    void updateCycleHz(uint64_t hz) noexcept {
        cycleHz.store(hz, std::memory_order_relaxed);
    }
    
    // Reads with relaxed ordering
    uint64_t getBlocksProcessed() const noexcept { return blocksProcessed.load(std::memory_order_relaxed); }
    uint64_t getXruns() const noexcept { return xruns.load(std::memory_order_relaxed); }
    uint64_t getUnderruns() const noexcept { return underruns.load(std::memory_order_relaxed); }
    uint64_t getOverruns() const noexcept { return overruns.load(std::memory_order_relaxed); }
    uint64_t getMaxCallbackNs() const noexcept { return maxCallbackNs.load(std::memory_order_relaxed); }
    uint64_t getLastCallbackNs() const noexcept { return lastCallbackNs.load(std::memory_order_relaxed); }
    uint32_t getLastBufferFrames() const noexcept { return lastBufferFrames.load(std::memory_order_relaxed); }
    uint32_t getLastSampleRate() const noexcept { return lastSampleRate.load(std::memory_order_relaxed); }
    uint64_t getCycleHz() const noexcept { return cycleHz.load(std::memory_order_relaxed); }
    uint64_t getSrcActiveBlocks() const noexcept { return srcActiveBlocks.load(std::memory_order_relaxed); }
};

} // namespace Audio
} // namespace Nomad
