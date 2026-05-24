// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioGraph.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace Aestra {
namespace Audio {

/**
 * @brief Double-buffered engine state for safe UI → RT handoff.
 *
 * Build new graphs off the audio thread, then swap them in atomically so the
 * callback always reads an immutable snapshot without locking.
 */
class EngineState {
public:
    class GraphReadHandle {
    public:
        GraphReadHandle() = default;
        GraphReadHandle(const GraphReadHandle&) = delete;
        GraphReadHandle& operator=(const GraphReadHandle&) = delete;

        GraphReadHandle(GraphReadHandle&& other) noexcept : m_state(other.m_state), m_index(other.m_index) {
            other.m_state = nullptr;
            other.m_index = -1;
        }

        GraphReadHandle& operator=(GraphReadHandle&& other) noexcept {
            if (this != &other) {
                release();
                m_state = other.m_state;
                m_index = other.m_index;
                other.m_state = nullptr;
                other.m_index = -1;
            }
            return *this;
        }

        ~GraphReadHandle() { release(); }

        const AudioGraph& get() const noexcept { return m_state->m_graphs[m_index]; }

    private:
        friend class EngineState;

        GraphReadHandle(const EngineState* state, int index) noexcept : m_state(state), m_index(index) {}

        void release() noexcept {
            if (m_state && m_index >= 0) {
                m_state->m_readers[static_cast<size_t>(m_index)].fetch_sub(1, std::memory_order_release);
                m_state = nullptr;
                m_index = -1;
            }
        }

        const EngineState* m_state{nullptr};
        int m_index{-1};
    };

    GraphReadHandle activeGraphRead() const noexcept {
        for (;;) {
            const int index = m_activeIndex.load(std::memory_order_acquire);
            m_readers[static_cast<size_t>(index)].fetch_add(1, std::memory_order_acquire);
            if (index == m_activeIndex.load(std::memory_order_acquire)) {
                return GraphReadHandle(this, index);
            }
            m_readers[static_cast<size_t>(index)].fetch_sub(1, std::memory_order_release);
        }
    }

    const AudioGraph& activeGraph() const noexcept { return m_graphs[m_activeIndex.load(std::memory_order_acquire)]; }

    void swapGraph(const AudioGraph& next) {
        std::lock_guard<std::mutex> lock(m_swapMutex);
        int target = -1;
        while (target < 0) {
            const int active = m_activeIndex.load(std::memory_order_acquire);
            for (int i = 0; i < kGraphSlots; ++i) {
                if (i != active && m_readers[static_cast<size_t>(i)].load(std::memory_order_acquire) == 0) {
                    target = i;
                    break;
                }
            }
            if (target < 0) {
                std::this_thread::yield();
            }
        }
        m_graphs[static_cast<size_t>(target)] = next; // copy/move from builder thread
        m_activeIndex.store(target, std::memory_order_release);
    }

    // Non-RT access for initialization or inspection.
    AudioGraph& mutableInactiveGraph() {
        const int active = m_activeIndex.load(std::memory_order_relaxed);
        for (int i = 0; i < kGraphSlots; ++i) {
            if (i != active && m_readers[static_cast<size_t>(i)].load(std::memory_order_acquire) == 0) {
                return m_graphs[static_cast<size_t>(i)];
            }
        }
        return m_graphs[static_cast<size_t>((active + 1) % kGraphSlots)];
    }

private:
    static constexpr int kGraphSlots = 3;

    AudioGraph m_graphs[kGraphSlots];
    std::atomic<int> m_activeIndex{0};
    mutable std::atomic<uint32_t> m_readers[kGraphSlots]{};
    std::mutex m_swapMutex;
};

} // namespace Audio
} // namespace Aestra
