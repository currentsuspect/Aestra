// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace Aestra {
namespace Audio {

/// Cleanup function type — uses function pointer + context to avoid std::function heap allocation
using CleanupFn = void (*)(void* context);

/**
 * Lock-free, background cleanup manager for quarantined plugin instances.
 *
 * When a plugin is quarantined by EngineSupervisor, it is logically detached
 * from the audio graph immediately (zero further process() calls). Its physical
 * teardown (destructor calls, resource release) is offloaded to this manager
 * to prevent blocking the audio thread.
 *
 * Uses a fixed-size SPSC (Single-Producer, Single-Consumer) ring buffer
 * for lock-free communication from the audio thread to the cleanup thread.
 * The enqueue path is lock-free and allocation-free (function pointer + context).
 *
 * The cleanup thread processes one item per cycle to avoid CPU spikes.
 */
class AsyncCleanupManager {
public:
    static constexpr size_t kQueueCapacity = 64;

    AsyncCleanupManager();
    ~AsyncCleanupManager();

    // Non-copyable, non-movable
    AsyncCleanupManager(const AsyncCleanupManager&) = delete;
    AsyncCleanupManager& operator=(const AsyncCleanupManager&) = delete;

    /**
     * Enqueue a cleanup task from the audio thread.
     * Lock-free and allocation-free. Returns false if queue is full.
     * The cleanup function is a plain function pointer (no std::function).
     */
    bool enqueueCleanup(uint64_t pluginId, CleanupFn fn, void* context) noexcept;

    /**
     * Start the background cleanup thread.
     * Must be called before any enqueue operations.
     * Thread-safe: uses compare_exchange_strong to prevent double-start.
     */
    void start();

    /**
     * Stop the background cleanup thread and drain remaining items.
     * Blocks until all pending cleanups are complete.
     */
    void stop();

    /**
     * Get the number of pending cleanup tasks.
     * Thread-safe: lock-free atomic read.
     * Note: unsigned subtraction wraps correctly for monotonic indices.
     */
    size_t getPendingCount() const noexcept {
        return m_writeIndex.load(std::memory_order_relaxed) -
               m_readIndex.load(std::memory_order_relaxed);
    }

    /**
     * Get the total number of cleanup tasks processed.
     * Thread-safe: lock-free atomic read.
     */
    [[nodiscard]] uint64_t getProcessedCount() const noexcept {
        return m_processedCount.load(std::memory_order_relaxed);
    }

private:
    struct CleanupTask {
        uint64_t pluginId{0};
        CleanupFn fn{nullptr};
        void* context{nullptr};
    };

    void cleanupThreadFunc();

    CleanupTask m_queue[kQueueCapacity];
    std::atomic<size_t> m_writeIndex{0};
    std::atomic<size_t> m_readIndex{0};
    std::atomic<uint64_t> m_processedCount{0};
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace Audio
} // namespace Aestra
