// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include "../../NomadCore/include/NomadLog.h"

namespace Nomad {
namespace Audio {

/**
 * @brief Thread-safe Garbage Collector for deferred object destruction.
 *
 * Used to prevent real-time threads from deleting complex objects (which might call free()).
 * The RT thread pushes objects to a lock-free queue (or swap-buffer).
 * A background thread (or the Main thread during idle) flushes the queue.
 */
class GarbageCollector {
public:
    static GarbageCollector& getInstance() {
        static GarbageCollector instance;
        return instance;
    }

    // Move object into the collector for deferred destruction
    template<typename T>
    void deferDelete(std::shared_ptr<T> ptr) {
        if (!ptr) return;
        // Simple spin-lock protected push (wait-free for reader/deleter, almost wait-free for writer)
        // For strict RT safety, we should use a lock-free queue.
        // Given std::vector isn't lock-free, we use a spinlock.
        // Ideally: LockFreeQueue<std::shared_ptr<void>>.
        // For this patch, we use a std::mutex with try_lock. If locked, we just leak? No.
        // We use a spinlock to ensure progress.

        while (m_spinLock.test_and_set(std::memory_order_acquire)) {
             // Spin
             // In RT context, spinning is better than blocking syscall.
             // Ideally this lock is held for nanoseconds.
        }

        m_trash.push_back(ptr); // Polymorphic shared_ptr<void>

        m_spinLock.clear(std::memory_order_release);
    }

    // Call from Non-RT thread (e.g. idle timer) to free memory
    void collect() {
        if (m_trash.empty()) return;

        std::vector<std::shared_ptr<void>> temp;

        // Grab trash
        while (m_spinLock.test_and_set(std::memory_order_acquire));
        temp.swap(m_trash);
        m_spinLock.clear(std::memory_order_release);

        // Destroy (outside lock)
        temp.clear();
    }

private:
    GarbageCollector() {
        m_trash.reserve(1024);
    }

    std::atomic_flag m_spinLock = ATOMIC_FLAG_INIT;
    std::vector<std::shared_ptr<void>> m_trash;
};

} // namespace Audio
} // namespace Nomad
