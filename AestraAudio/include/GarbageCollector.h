// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Thread-safe Garbage Collector for deferred object destruction.
 *
 * Used to safely release resources (like Sample Buffers) that might still be
 * in use by the Real-Time Audio Thread.
 *
 * Usage:
 *   // UI Thread
 *   GarbageCollector::instance().release(oldData);
 *
 * Logic:
 *   - 'zombies' list holds shared_ptrs.
 *   - If use_count() == 1, it means only the GC holds it. Audio thread is done.
 *   - Safe to delete.
 */
class GarbageCollector {
public:
    static GarbageCollector& instance() {
        static GarbageCollector inst;
        return inst;
    }

    /**
     * @brief Schedule a shared_ptr for deferred destruction.
     * Call from Non-RT thread (UI/Loading).
     */
    template <typename T> void release(std::shared_ptr<T> ptr) {
        if (!ptr)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_zombies.push_back(std::static_pointer_cast<void>(ptr));
    }

    /**
     * @brief Force a cleanup pass.
     * Call from Idle timer or low-priority thread.
     */
    void collect() {
        std::lock_guard<std::mutex> lock(m_mutex);
        internalCleanup();
    }

private:
    std::vector<std::shared_ptr<void>> m_zombies;
    std::mutex m_mutex;

    void internalCleanup() {
        // Identify dead objects (use_count == 1 means only we hold it)
        auto it = std::remove_if(m_zombies.begin(), m_zombies.end(),
                                 [](const std::shared_ptr<void>& p) { return p.use_count() == 1; });

        // Erase them (triggering destructor)
        // Destruction happens here, on the calling thread (UI/Idle)
        m_zombies.erase(it, m_zombies.end());
    }
};

} // namespace Audio
} // namespace Aestra
