// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NomadThreading.h"
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

/**
 * @brief Lightweight command envelope.
 *
 * payloadIndex can be used to point into a preallocated payload array if larger
 * data is required. The RT thread should never allocate when consuming commands.
 */
struct AudioQueueCommand {
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
        return m_queue.push(cmd);
    }

    bool pop(AudioQueueCommand& outCmd) {
        return m_queue.pop(outCmd);
    }

    bool empty() const {
        return m_queue.isEmpty();
    }

private:
    Nomad::LockFreeRingBuffer<AudioQueueCommand, kQueueCapacity> m_queue;
};

} // namespace Audio
} // namespace Nomad
