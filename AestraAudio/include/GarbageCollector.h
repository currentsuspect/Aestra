// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "../../AestraCore/include/AestraThreading.h"
#include "RealtimeThreadGuard.h"

namespace Aestra {
namespace Audio {

struct GarbageCollectorStats {
    size_t totalReleased = 0;
    size_t totalCollected = 0;
    size_t currentlyTracked = 0;
    size_t incomingQueueFullCount = 0;
    size_t overflowCount = 0;
    size_t totalOverflowDrained = 0;
    size_t maxZombieCount = 0;
};

/**
 * @brief Deferred destruction / RCU-style reclamation for shared resources.
 *
 * This is not a tracing garbage collector. It holds retired shared_ptr-owned
 * resources until the audio side has dropped any shared references obtained
 * through an atomic resource swap.
 *
 * Threading contract:
 *   - release() is non-RT only. It may lock and allocate on overflow.
 *   - collect() is non-RT only. It uses std::vector and may destroy objects.
 *   - zombieCount() and stats() are for non-RT diagnostics/debug/UI only.
 *   - Destruction happens on the thread that calls collect().
 *   - The real-time audio thread must never call release(), collect(),
 *     drainUntilStable(), zombieCount(), or stats APIs.
 *
 * Expected usage:
 *   1. Publish a new resource with an atomic shared_ptr swap.
 *   2. Pass the old shared_ptr to release() from UI/loading/shutdown code.
 *   3. Call collect() from an idle/background/shutdown context.
 */
template <size_t IncomingCapacity> class BasicGarbageCollector {
public:
    BasicGarbageCollector() { m_overflow.reserve(IncomingCapacity); }

    BasicGarbageCollector(const BasicGarbageCollector&) = delete;
    BasicGarbageCollector& operator=(const BasicGarbageCollector&) = delete;

    /**
     * @brief Schedule a shared_ptr for deferred destruction.
     *
     * Non-RT only. The common path uses the bounded incoming queue. If that
     * queue is full, release() retains the object in a mutex-protected overflow
     * vector so admission failure cannot silently drop or destroy a resource.
     */
    template <typename T> void release(std::shared_ptr<T> ptr) { release(std::move(ptr), nullptr); }

    /**
     * @brief Schedule a shared_ptr for deferred destruction with a debug label.
     *
     * The label is optional metadata for future diagnostics. It must point to
     * storage with static or otherwise externally managed lifetime.
     */
    template <typename T> void release(std::shared_ptr<T> ptr, const char* label) {
        assertNonRealtime("GarbageCollector::release");
        if (!ptr)
            return;

        RetiredResource resource{std::static_pointer_cast<const void>(std::move(ptr)), label};
        std::lock_guard<std::mutex> lock(m_releaseMutex);
        if (!m_incoming.push(resource)) {
            m_incomingQueueFullCount.fetch_add(1, std::memory_order_relaxed);
            m_overflow.push_back(std::move(resource));
            const size_t overflowSize = m_overflow.size();
            updateMax(m_overflowCount, overflowSize);
        }

        const size_t tracked = m_currentlyTracked.fetch_add(1, std::memory_order_relaxed) + 1;
        m_totalReleased.fetch_add(1, std::memory_order_relaxed);
        updateMax(m_maxZombieCount, tracked);
    }

    /**
     * @brief Run one cleanup pass.
     *
     * Non-RT only. Moves incoming releases to the collector-owned zombie list,
     * drains overflow entries, and destroys objects whose use_count() indicates
     * that only the collector is still holding them.
     */
    void collect() {
        assertNonRealtime("GarbageCollector::collect");
        noteCollectorThread();

        RetiredResource item;
        while (m_incoming.popMoveAndClear(item)) {
            m_zombies.push_back(std::move(item));
        }

        {
            std::lock_guard<std::mutex> lock(m_releaseMutex);
            if (!m_overflow.empty()) {
                const size_t drained = m_overflow.size();
                m_zombies.insert(m_zombies.end(), std::make_move_iterator(m_overflow.begin()),
                                 std::make_move_iterator(m_overflow.end()));
                m_overflow.clear();
                m_totalOverflowDrained.fetch_add(drained, std::memory_order_relaxed);
                m_overflowCount.store(0, std::memory_order_relaxed);
            }
        }

        internalCleanup();
        updateMax(m_maxZombieCount, m_currentlyTracked.load(std::memory_order_relaxed));
    }

    /**
     * @brief Run collect() until no additional objects are reclaimed.
     * @return number of collect passes performed.
     */
    size_t drainUntilStable(size_t maxPasses = 8) {
        assertNonRealtime("GarbageCollector::drainUntilStable");
        size_t passes = 0;
        size_t previousCollected = m_totalCollected.load(std::memory_order_relaxed);
        for (; passes < maxPasses; ++passes) {
            collect();
            const size_t collected = m_totalCollected.load(std::memory_order_relaxed);
            if (collected == previousCollected) {
                break;
            }
            previousCollected = collected;
        }
        return passes + (passes < maxPasses ? 1 : 0);
    }

    /** @return Number of retired resources still retained by the collector. */
    size_t zombieCount() const {
        assertNonRealtime("GarbageCollector::zombieCount");
        return m_currentlyTracked.load(std::memory_order_relaxed);
    }

    GarbageCollectorStats stats() const {
        assertNonRealtime("GarbageCollector::stats");
        GarbageCollectorStats result;
        result.totalReleased = m_totalReleased.load(std::memory_order_relaxed);
        result.totalCollected = m_totalCollected.load(std::memory_order_relaxed);
        result.currentlyTracked = m_currentlyTracked.load(std::memory_order_relaxed);
        result.incomingQueueFullCount = m_incomingQueueFullCount.load(std::memory_order_relaxed);
        result.overflowCount = m_overflowCount.load(std::memory_order_relaxed);
        result.totalOverflowDrained = m_totalOverflowDrained.load(std::memory_order_relaxed);
        result.maxZombieCount = m_maxZombieCount.load(std::memory_order_relaxed);
        return result;
    }

private:
    struct RetiredResource {
        std::shared_ptr<const void> object;
        const char* label = nullptr;
    };

    Aestra::LockFreeRingBuffer<RetiredResource, IncomingCapacity> m_incoming;

    // Serializes non-RT producers and protects the non-RT overflow list.
    std::mutex m_releaseMutex;
    std::vector<RetiredResource> m_overflow;

    // Collector-owned state. Only the collect() thread may touch this vector.
    std::vector<RetiredResource> m_zombies;

    std::atomic<size_t> m_totalReleased{0};
    std::atomic<size_t> m_totalCollected{0};
    std::atomic<size_t> m_currentlyTracked{0};
    std::atomic<size_t> m_incomingQueueFullCount{0};
    std::atomic<size_t> m_overflowCount{0};
    std::atomic<size_t> m_totalOverflowDrained{0};
    std::atomic<size_t> m_maxZombieCount{0};

#ifndef NDEBUG
    std::thread::id m_collectorThreadId;
#endif

    static void updateMax(std::atomic<size_t>& target, size_t candidate) {
        size_t current = target.load(std::memory_order_relaxed);
        while (candidate > current &&
               !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    static void assertNonRealtime(const char* apiName) {
        (void)reportRealtimeMisuse(apiName);
    }

    void noteCollectorThread() {
#ifndef NDEBUG
        const std::thread::id current = std::this_thread::get_id();
        if (m_collectorThreadId == std::thread::id{}) {
            m_collectorThreadId = current;
        } else {
            assert(m_collectorThreadId == current && "GarbageCollector::collect() must stay on one non-RT owner thread");
        }
#endif
    }

    void internalCleanup() {
        size_t collected = 0;
        auto it = std::remove_if(m_zombies.begin(), m_zombies.end(), [&collected](const RetiredResource& resource) {
            const bool ready = resource.object.use_count() == 1;
            if (ready) {
                ++collected;
            }
            return ready;
        });

        m_zombies.erase(it, m_zombies.end());
        if (collected > 0) {
            m_totalCollected.fetch_add(collected, std::memory_order_relaxed);
            m_currentlyTracked.fetch_sub(collected, std::memory_order_relaxed);
        }
    }
};

class GarbageCollector : public BasicGarbageCollector<4096> {
public:
    static GarbageCollector& instance() {
        static GarbageCollector inst;
        return inst;
    }

private:
    GarbageCollector() = default;
};

} // namespace Audio
} // namespace Aestra
