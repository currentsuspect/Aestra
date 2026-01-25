// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <chrono>

namespace Aestra {
namespace Audio {

/**
 * @brief Thread-safe Garbage Collector for deferred object destruction.
 *
 * Used to safely release resources (like Sample Buffers) that might still be
 * in use by the Real-Time Audio Thread.
 *
 * Usage:
 *   // UI/Loading Thread (swapping pointers)
 *   GarbageCollector::instance().release(oldData);
 *
 * Logic:
 *   - 'zombies' list holds shared_ptrs and release timestamps.
 *   - Objects are only deleted if:
 *     1. use_count() == 1 (Only GC holds it) - AND
 *     2. Enough time has passed (e.g. 200ms) to ensure RT thread is done.
 *
 *   For raw pointer swaps (where RT thread holds no ref), we MUST rely on time.
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
    template<typename T>
    void release(std::shared_ptr<T> ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_zombies.push_back({ std::static_pointer_cast<void>(ptr), std::chrono::steady_clock::now() });
    }

    /**
     * @brief Force a cleanup pass.
     * Call from Idle timer or low-priority thread (e.g., Loudness Worker).
     */
    void collect() {
        std::lock_guard<std::mutex> lock(m_mutex);
        internalCleanup();
    }

private:
    struct Zombie {
        std::shared_ptr<void> ptr;
        std::chrono::steady_clock::time_point releaseTime;
    };

    std::vector<Zombie> m_zombies;
    std::mutex m_mutex;

    void internalCleanup() {
        auto now = std::chrono::steady_clock::now();
        // Safe duration: 200ms.
        // Audio block @ 48kHz / 256 frames = ~5ms.
        // 200ms is ample time even with severe jitter.
        auto minAge = std::chrono::milliseconds(200);

        // Remove zombies that are old enough AND unique.
        // Since we support raw pointer users (who don't hold refs), we CANNOT rely solely on use_count == 1
        // to imply "safe to delete immediately". We must wait for the grace period.
        // If someone else DOES hold a ref (e.g. UI visualization), use_count > 1, so we definitely keep it.

        auto it = std::remove_if(m_zombies.begin(), m_zombies.end(),
            [&](const Zombie& z) {
                if (z.ptr.use_count() > 1) return false; // Still held by someone else
                auto age = now - z.releaseTime;
                return age > minAge; // Only delete if old enough
            });

        m_zombies.erase(it, m_zombies.end());
    }
};

} // namespace Audio
} // namespace Aestra
