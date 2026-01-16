// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * @brief Lightweight telemetry counters updated from the RT thread.
 *
 * All fields are atomics for lock-free access; UI/non-RT code should snapshot
 * these periodically and handle presentation/logging off the audio thread.
 * All access uses relaxed memory ordering for optimal real-time performance.
 * 
 * B-009: Buffer Underrun Recovery
 * Added consecutive underrun tracking and recovery state.
 * When consecutive underruns exceed threshold, recovery mode is activated:
 * - Audio output may be temporarily muted
 * - Plugin processing may be reduced
 * - Recovery completes after stable frames
 * 
 * B-010: Audio Thread Priority Verification
 * Added thread priority verification status to track whether MMCSS/priority
 * was successfully configured. Check getThreadPriorityStatus() after audio
 * thread starts to verify real-time scheduling is active.
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
    
    // B-009: Underrun recovery state
    std::atomic<uint32_t> consecutiveUnderruns{0};      // Current streak
    std::atomic<uint32_t> stableFramesSinceRecovery{0}; // Frames since last underrun
    std::atomic<bool>     inRecoveryMode{false};         // True when recovering
    std::atomic<uint64_t> recoveryModeActivations{0};    // Total recovery mode entries
    
    // B-009: Configuration (can be tuned at runtime)
    static constexpr uint32_t kRecoveryThreshold = 3;     // Underruns to trigger recovery
    static constexpr uint32_t kRecoveryFrames = 100;      // Stable frames to exit recovery
    
    // B-010: Thread priority verification status
    // Bitmask: bit 0 = SetThreadPriority success
    //          bit 1 = MMCSS (AvSetMmThreadCharacteristics) success  
    //          bit 2 = MMCSS priority (AvSetMmThreadPriority) success
    std::atomic<uint32_t> threadPriorityStatus{0};
    
    // B-010: Thread priority status bits
    static constexpr uint32_t kPriorityBit_ThreadPriority = 0x01;
    static constexpr uint32_t kPriorityBit_MMCSS = 0x02;
    static constexpr uint32_t kPriorityBit_MMCSSPriority = 0x04;
    static constexpr uint32_t kPriorityBit_AllSuccess = 0x07;

    // Convenience methods for relaxed memory ordering access
    // Increments
    void incrementBlocksProcessed() noexcept { blocksProcessed.fetch_add(1, std::memory_order_relaxed); }
    void incrementXruns() noexcept { xruns.fetch_add(1, std::memory_order_relaxed); }
    void incrementOverruns() noexcept { overruns.fetch_add(1, std::memory_order_relaxed); }
    void incrementSrcActiveBlocks() noexcept { srcActiveBlocks.fetch_add(1, std::memory_order_relaxed); }
    
    /**
     * @brief B-009: Record an underrun and manage recovery state
     * 
     * Called from audio callback when underrun detected.
     * Tracks consecutive underruns and triggers recovery mode.
     */
    void incrementUnderruns() noexcept { 
        underruns.fetch_add(1, std::memory_order_relaxed); 
        uint32_t consecutive = consecutiveUnderruns.fetch_add(1, std::memory_order_relaxed) + 1;
        stableFramesSinceRecovery.store(0, std::memory_order_relaxed);
        
        // Trigger recovery mode if threshold exceeded
        if (consecutive >= kRecoveryThreshold && !inRecoveryMode.load(std::memory_order_relaxed)) {
            inRecoveryMode.store(true, std::memory_order_release);
            recoveryModeActivations.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    /**
     * @brief B-009: Record a successful (stable) audio block
     * 
     * Called after each successful audio callback.
     * Clears consecutive underrun counter and may exit recovery mode.
     */
    void recordStableBlock() noexcept {
        consecutiveUnderruns.store(0, std::memory_order_relaxed);
        
        if (inRecoveryMode.load(std::memory_order_relaxed)) {
            uint32_t stable = stableFramesSinceRecovery.fetch_add(1, std::memory_order_relaxed) + 1;
            if (stable >= kRecoveryFrames) {
                inRecoveryMode.store(false, std::memory_order_release);
                stableFramesSinceRecovery.store(0, std::memory_order_relaxed);
            }
        }
    }
    
    /**
     * @brief B-009: Check if currently in recovery mode
     * 
     * Audio processing may be simplified during recovery.
     */
    bool isInRecoveryMode() const noexcept {
        return inRecoveryMode.load(std::memory_order_acquire);
    }
    
    /**
     * @brief B-010: Set thread priority status bit
     * 
     * Called from audio thread startup to record priority configuration status.
     */
    void setThreadPriorityBit(uint32_t bit) noexcept {
        threadPriorityStatus.fetch_or(bit, std::memory_order_relaxed);
    }
    
    /**
     * @brief B-010: Get thread priority status
     * 
     * Returns bitmask indicating which priority settings succeeded.
     */
    uint32_t getThreadPriorityStatus() const noexcept {
        return threadPriorityStatus.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief B-010: Check if all thread priority settings succeeded
     */
    bool isThreadPriorityOptimal() const noexcept {
        return (getThreadPriorityStatus() & kPriorityBit_AllSuccess) == kPriorityBit_AllSuccess;
    }
    
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
    uint32_t getConsecutiveUnderruns() const noexcept { return consecutiveUnderruns.load(std::memory_order_relaxed); }
    uint64_t getRecoveryModeActivations() const noexcept { return recoveryModeActivations.load(std::memory_order_relaxed); }
};

} // namespace Audio
} // namespace Aestra
