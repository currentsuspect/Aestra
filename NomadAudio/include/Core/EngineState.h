// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioGraph.h"
#include <atomic>

namespace Nomad {
namespace Audio {

/**
 * @brief Double-buffered engine state for safe UI → RT handoff.
 *
 * Build new graphs off the audio thread, then swap them in atomically so the
 * callback always reads an immutable snapshot without locking.
 */
class EngineState {
public:
    const AudioGraph& activeGraph() const noexcept {
        return m_graphs[m_activeIndex.load(std::memory_order_acquire)];
    }

    void swapGraph(const AudioGraph& next) {
        const int inactive = 1 - m_activeIndex.load(std::memory_order_relaxed);
        m_graphs[inactive] = next; // copy/move from builder thread
        m_activeIndex.store(inactive, std::memory_order_release);
    }

    // Non-RT access for initialization or inspection.
    AudioGraph& mutableInactiveGraph() {
        const int inactive = 1 - m_activeIndex.load(std::memory_order_relaxed);
        return m_graphs[inactive];
    }

private:
    AudioGraph m_graphs[2];
    std::atomic<int> m_activeIndex{0};
};

} // namespace Audio
} // namespace Nomad
