// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraAtomicSharedPtr.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * Publishes shared control-thread ownership as a non-owning real-time pointer.
 *
 * One control thread owns publish(), owner(), and collectRetired(). One real-time
 * reader uses acquireRealtime()/releaseRealtime(). Replaced objects remain owned
 * by the control thread until the real-time hazard no longer references them, so
 * the last shared_ptr release never occurs in the callback.
 */
template <typename T> class RealtimeObjectPublisher {
public:
    RealtimeObjectPublisher() = default;
    explicit RealtimeObjectPublisher(std::shared_ptr<T> initial) { publish(std::move(initial)); }

    RealtimeObjectPublisher(const RealtimeObjectPublisher&) = delete;
    RealtimeObjectPublisher& operator=(const RealtimeObjectPublisher&) = delete;

    void publish(std::shared_ptr<T> object) {
        auto previous = m_owner.exchange(object, std::memory_order_acq_rel);
        m_published.store(object.get(), std::memory_order_release);
        if (previous && previous.get() != object.get()) {
            m_retired.push_back(std::move(previous));
        }
        collectRetired();
    }

    std::shared_ptr<T> owner() const { return m_owner.load(std::memory_order_acquire); }

    T* acquireRealtime() noexcept {
        T* object = nullptr;
        do {
            object = m_published.load(std::memory_order_acquire);
            m_realtimeHazard.store(object, std::memory_order_release);
        } while (object != m_published.load(std::memory_order_acquire));
        return object;
    }

    void releaseRealtime() noexcept { m_realtimeHazard.store(nullptr, std::memory_order_release); }

    void collectRetired() {
        const T* active = m_published.load(std::memory_order_acquire);
        const T* inUse = m_realtimeHazard.load(std::memory_order_acquire);
        m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
                                       [active, inUse](const auto& object) {
                                           return object.get() != active && object.get() != inUse;
                                       }),
                        m_retired.end());
    }

private:
    AtomicSharedPtr<T> m_owner;
    std::atomic<T*> m_published{nullptr};
    std::atomic<T*> m_realtimeHazard{nullptr};
    std::vector<std::shared_ptr<T>> m_retired;
};

} // namespace Audio
} // namespace Aestra
