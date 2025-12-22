// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NomadThreading.h"
#include <atomic>
#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief Command types exchanged between UI/engine and the audio thread.
 *
 * These are intentionally minimal POD types to keep the RT path allocation-free.
 * Extend cautiously; prefer fixed-size payloads and preallocated pools.
 */
enum class AudioQueueCommandType : uint8_t {
    None = 0,
    SetTransportState,   // value1: 1.0 = play, 0.0 = stop; samplePos used for seek
    SetTrackVolume,      // trackIndex, value1
    SetTrackPan,         // trackIndex, value1 (-1..1)
    SetTrackMute,        // trackIndex, value1 (0/1)
    SetTrackSolo,        // trackIndex, value1 (0/1)
    LoadProjectState,
    UpdateClipState,
    StartPreview,
    StopPreview,
};

// Ensure cache-friendly alignment for RT path
struct alignas(32) AudioQueueCommand {
    AudioQueueCommandType type{AudioQueueCommandType::None};
    uint32_t trackIndex{0};    // For track-scoped commands
    float value1{0.0f};        // Generic value (gain/pan/mute flag/etc.)
    float value2{0.0f};        // Optional secondary value
    uint64_t samplePos{0};     // For seeks / absolute positions
    uint32_t payloadIndex{0};  // Optional external payload reference
};

/**
 * @brief Single-producer/single-consumer command queue for UI → Audio.
 *
 * Uses the existing lock-free ring buffer from NomadCore. Capacity is fixed to
 * avoid allocations and keep RT guarantees.
 */
class AudioCommandQueue {
public:
    static constexpr size_t kQueueCapacity = 1024;

    bool push(const AudioQueueCommand& cmd) {
        const bool ok = m_queue.push(cmd);
        if (!ok) {
            // Drop-newest policy: keep audio thread deterministic; UI can observe drops via telemetry.
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const uint32_t depth = static_cast<uint32_t>(m_queue.size());
        uint32_t prev = m_maxDepth.load(std::memory_order_relaxed);
        while (depth > prev &&
               !m_maxDepth.compare_exchange_weak(prev, depth,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
        }
        return true;
    }

    bool pop(AudioQueueCommand& outCmd) {
        return m_queue.pop(outCmd);
    }

    bool empty() const {
        return m_queue.isEmpty();
    }

    uint32_t approxDepth() const noexcept {
        return static_cast<uint32_t>(m_queue.size());
    }

    uint32_t maxDepth() const noexcept {
        return m_maxDepth.load(std::memory_order_relaxed);
    }

    uint64_t droppedCount() const noexcept {
        return m_dropped.load(std::memory_order_relaxed);
    }

    static constexpr uint32_t capacity() noexcept {
        return static_cast<uint32_t>(Nomad::LockFreeRingBuffer<AudioQueueCommand, kQueueCapacity>::capacity());
    }

private:
    Nomad::LockFreeRingBuffer<AudioQueueCommand, kQueueCapacity> m_queue;
    std::atomic<uint64_t> m_dropped{0};
    std::atomic<uint32_t> m_maxDepth{0};
};

} // namespace Audio
} // namespace Nomad
