// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace Aestra {
namespace Audio {

/**
 * @brief A shared_ptr the audio thread can read without a lock.
 *
 * In this C++17 build, std::atomic_load on a shared_ptr takes a mutex from a
 * small global pool (libstdc++'s _Sp_locker), so every audio-thread read of
 * an AtomicSharedPtr was a lock shared with every other shared_ptr atomic op
 * in the process — a priority inversion waiting to happen. This slot keeps a
 * few owned generations; a reader pins one with a reader count and gets a raw
 * pointer, and the control thread only overwrites a generation nobody reads.
 *
 * - readRealtime(): any number of concurrent readers, any thread. Wait-free
 *   except for a retry when it races a publish. Never locks, never allocates,
 *   never drops the last reference.
 * - publish() / owner(): control threads (serialised internally). publish()
 *   may yield while every spare generation is still pinned by a reader.
 *
 * The pin is a store-then-load handshake on both sides (bump a count, re-read
 * the index / store the index, read the counts), so those operations are
 * seq_cst; acquire/release would let both sides read stale values.
 */
template <typename T> class RealtimeSharedSlot {
public:
    class ReadHandle {
    public:
        ReadHandle() = default;
        ReadHandle(const ReadHandle&) = delete;
        ReadHandle& operator=(const ReadHandle&) = delete;
        ReadHandle(ReadHandle&& other) noexcept : m_slot(other.m_slot), m_index(other.m_index), m_ptr(other.m_ptr) {
            other.m_slot = nullptr;
        }
        ReadHandle& operator=(ReadHandle&& other) noexcept {
            if (this != &other) {
                release();
                m_slot = other.m_slot;
                m_index = other.m_index;
                m_ptr = other.m_ptr;
                other.m_slot = nullptr;
            }
            return *this;
        }
        ~ReadHandle() { release(); }

        T* get() const noexcept { return m_ptr; }
        /** @brief The pinned generation's owner. Copying it is a refcount bump (no lock); the
         *  pin guarantees no writer touches it meanwhile. Valid while this handle lives. */
        const std::shared_ptr<T>& shared() const noexcept { return m_slot->m_owned[static_cast<size_t>(m_index)]; }
        T* operator->() const noexcept { return m_ptr; }
        explicit operator bool() const noexcept { return m_ptr != nullptr; }

    private:
        friend class RealtimeSharedSlot;
        ReadHandle(const RealtimeSharedSlot* slot, int index, T* ptr) noexcept
            : m_slot(slot), m_index(index), m_ptr(ptr) {}
        void release() noexcept {
            if (m_slot) {
                m_slot->m_readers[static_cast<size_t>(m_index)].fetch_sub(1, std::memory_order_release);
                m_slot = nullptr;
            }
        }
        const RealtimeSharedSlot* m_slot{nullptr};
        int m_index{0};
        T* m_ptr{nullptr};
    };

    RealtimeSharedSlot() = default;
    explicit RealtimeSharedSlot(std::shared_ptr<T> initial) { publish(std::move(initial)); }
    RealtimeSharedSlot(const RealtimeSharedSlot&) = delete;
    RealtimeSharedSlot& operator=(const RealtimeSharedSlot&) = delete;

    /** @brief Any thread, real-time included. */
    ReadHandle readRealtime() const noexcept {
        // Default-constructed slot: nothing published yet, generation 0 holds null.
        for (;;) {
            const int index = m_active.load(std::memory_order_acquire);
            m_readers[static_cast<size_t>(index)].fetch_add(1, std::memory_order_seq_cst);
            if (index == m_active.load(std::memory_order_seq_cst)) {
                return ReadHandle(this, index, m_raw[static_cast<size_t>(index)].load(std::memory_order_acquire));
            }
            m_readers[static_cast<size_t>(index)].fetch_sub(1, std::memory_order_release);
        }
    }

    /** @brief Control thread. The generation it replaces is destroyed here, never by a reader. */
    void publish(std::shared_ptr<T> object) {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        int target = -1;
        while (target < 0) {
            const int active = m_active.load(std::memory_order_relaxed);
            for (int i = 0; i < kGenerations; ++i) {
                if (i != active && m_readers[static_cast<size_t>(i)].load(std::memory_order_seq_cst) == 0) {
                    target = i;
                    break;
                }
            }
            if (target < 0) {
                std::this_thread::yield();
            }
        }
        m_raw[static_cast<size_t>(target)].store(object.get(), std::memory_order_relaxed);
        m_owned[static_cast<size_t>(target)] = std::move(object);
        m_active.store(target, std::memory_order_seq_cst);
        // Let go of every other generation nobody has pinned, so the slot keeps no object
        // alive beyond the current one. A reader that read a stale index and pins it after
        // this check fails its re-check (the index is no longer active) and never uses it.
        for (int i = 0; i < kGenerations; ++i) {
            if (i != target && m_readers[static_cast<size_t>(i)].load(std::memory_order_seq_cst) == 0) {
                m_raw[static_cast<size_t>(i)].store(nullptr, std::memory_order_relaxed);
                m_owned[static_cast<size_t>(i)].reset();
            }
        }
    }

    /** @brief Control thread: the current generation as a shared_ptr. */
    std::shared_ptr<T> owner() const {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        return m_owned[static_cast<size_t>(m_active.load(std::memory_order_relaxed))];
    }

private:
    static constexpr int kGenerations = 3;
    std::array<std::shared_ptr<T>, kGenerations> m_owned{};
    std::array<std::atomic<T*>, kGenerations> m_raw{};
    mutable std::array<std::atomic<uint32_t>, kGenerations> m_readers{};
    std::atomic<int> m_active{0};
    mutable std::mutex m_controlMutex;
};

} // namespace Audio
} // namespace Aestra
