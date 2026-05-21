// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AsyncCleanupManager.h"

#include <chrono>

namespace Aestra {
namespace Audio {

AsyncCleanupManager::AsyncCleanupManager() = default;

AsyncCleanupManager::~AsyncCleanupManager() {
    stop();
}

bool AsyncCleanupManager::enqueueCleanup(uint64_t pluginId, std::function<void()> cleanupFn) noexcept {
    const size_t write = m_writeIndex.load(std::memory_order_relaxed);
    const size_t read = m_readIndex.load(std::memory_order_acquire);

    // Check if queue is full
    if (write - read >= kQueueCapacity) {
        return false;
    }

    auto& task = m_queue[write % kQueueCapacity];
    task.pluginId = pluginId;
    task.fn = std::move(cleanupFn);

    m_writeIndex.store(write + 1, std::memory_order_release);
    return true;
}

void AsyncCleanupManager::start() {
    if (m_running.load(std::memory_order_relaxed)) return;
    m_running.store(true, std::memory_order_relaxed);
    m_thread = std::thread(&AsyncCleanupManager::cleanupThreadFunc, this);
}

void AsyncCleanupManager::stop() {
    m_running.store(false, std::memory_order_relaxed);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AsyncCleanupManager::cleanupThreadFunc() {
    while (m_running.load(std::memory_order_relaxed)) {
        const size_t read = m_readIndex.load(std::memory_order_relaxed);
        const size_t write = m_writeIndex.load(std::memory_order_acquire);

        if (read == write) {
            // Queue empty, sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        auto& task = m_queue[read % kQueueCapacity];
        if (task.fn) {
            task.fn();
            task.fn = nullptr;
        }
        m_readIndex.store(read + 1, std::memory_order_release);
        m_processedCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Drain remaining items on shutdown
    while (true) {
        const size_t read = m_readIndex.load(std::memory_order_relaxed);
        const size_t write = m_writeIndex.load(std::memory_order_acquire);
        if (read == write) break;

        auto& task = m_queue[read % kQueueCapacity];
        if (task.fn) {
            task.fn();
            task.fn = nullptr;
        }
        m_readIndex.store(read + 1, std::memory_order_release);
        m_processedCount.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace Audio
} // namespace Aestra
