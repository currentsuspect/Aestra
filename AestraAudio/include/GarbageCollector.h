// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

#include "../../AestraCore/include/AestraThreading.h"

namespace Aestra {
namespace Audio {

/**
 * @brief Lock-free Garbage Collector for deferred object destruction.
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
 *
 * Thread safety:
 *   - release() uses a lock-free ring buffer for the incoming queue
 *   - collect() swaps the incoming queue with the processing list atomically
 *   - No mutex contention — pure atomic operations
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
     * Lock-free — uses SPSC ring buffer.
     */
    template <typename T> void release(std::shared_ptr<T> ptr) {
        if (!ptr)
            return;
        (void)m_incoming.push(std::static_pointer_cast<void>(ptr));
    }

    /**
     * @brief Force a cleanup pass.
     * Call from Idle timer or low-priority thread.
     * Lock-free — atomically swaps incoming queue with processing list.
     */
    void collect() {
        // Atomically swap incoming zombies to our local list
        std::shared_ptr<void> item;
        while (m_incoming.pop(item)) {
            m_zombies.push_back(std::move(item));
        }

        // Clean up dead objects
        internalCleanup();
    }

    /** @return Number of zombies currently tracked. */
    size_t zombieCount() const {
        return m_zombies.size() + m_incoming.size();
    }

private:
    // Lock-free ring buffer for incoming releases.
    // Capacity is generous — 4096 entries should never fill under normal use.
    static constexpr size_t INCOMING_CAPACITY = 4096;
    Aestra::LockFreeRingBuffer<std::shared_ptr<void>, INCOMING_CAPACITY> m_incoming;

    // Processing list — only accessed from collect() thread.
    std::vector<std::shared_ptr<void>> m_zombies;

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
