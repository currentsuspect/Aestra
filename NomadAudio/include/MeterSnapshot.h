// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>

namespace Nomad {
namespace Audio {

/**
 * @brief Bitcast utilities for atomic float storage.
 *
 * Floats are stored as uint32_t bitcast to ensure atomic operations are well-defined.
 * Audio thread writes LINEAR peaks (0..1), UI converts to dB.
 */
namespace MeterBitcast {

inline uint32_t floatToU32(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

inline float u32ToFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

} // namespace MeterBitcast

/**
 * @brief Single channel meter snapshot using double-buffer design.
 *
 * Cache-line aligned to prevent false sharing between channels.
 * Audio writes to [writeIndex], UI reads from [1-writeIndex].
 */
struct alignas(64) ChannelMeterSnapshot {
    struct Buffer {
        uint32_t peakL_bits{0};  // float as uint32_t bitcast (LINEAR 0..1)
        uint32_t peakR_bits{0};  // float as uint32_t bitcast (LINEAR 0..1)
        uint8_t clipFlags{0};    // bit 0 = L clip, bit 1 = R clip
    };

    Buffer buffers[2];
    std::atomic<uint8_t> writeIndex{0};  // 0 or 1

    static constexpr uint8_t CLIP_L = 0x01;
    static constexpr uint8_t CLIP_R = 0x02;
};

/**
 * @brief Lock-free meter snapshot buffer for RT-safe metering.
 *
 * Uses double-buffering with atomic index swap for safe cross-thread access.
 * Audio thread writes peaks during mix loop, UI thread reads for display.
 *
 * Memory ordering:
 * - Audio writes with release semantics on index swap
 * - UI reads with acquire semantics to see audio writes
 */
class MeterSnapshotBuffer {
public:
    static constexpr size_t MAX_CHANNELS = 128;

    /**
     * @brief Write peak levels from audio thread.
     *
     * Called during mix loop with LINEAR peak values (0..1).
     * Uses release semantics on index swap to ensure buffer writes are visible.
     *
     * @param slotIndex Dense slot index (0..MAX_CHANNELS-1)
     * @param peakL Left channel peak (LINEAR 0..1)
     * @param peakR Right channel peak (LINEAR 0..1)
     */
    void writePeak(uint32_t slotIndex, float peakL, float peakR) {
        if (slotIndex >= MAX_CHANNELS) return;
        auto& snap = m_snapshots[slotIndex];
        uint8_t writeIdx = snap.writeIndex.load(std::memory_order_relaxed);
        auto& buf = snap.buffers[writeIdx];
        buf.peakL_bits = MeterBitcast::floatToU32(peakL);
        buf.peakR_bits = MeterBitcast::floatToU32(peakR);
        // Swap buffer index with release semantics
        snap.writeIndex.store(1 - writeIdx, std::memory_order_release);
    }

    /**
     * @brief Set clip flags when linear peak >= 1.0.
     *
     * Called from audio thread when clipping is detected.
     * Flags persist until cleared by UI via clearClip().
     *
     * @param slotIndex Dense slot index
     * @param clipL True if left channel clipped
     * @param clipR True if right channel clipped
     */
    void setClip(uint32_t slotIndex, bool clipL, bool clipR) {
        if (slotIndex >= MAX_CHANNELS) return;
        auto& snap = m_snapshots[slotIndex];
        // Set clip flags on both buffers to ensure UI sees them
        if (clipL) {
            snap.buffers[0].clipFlags |= ChannelMeterSnapshot::CLIP_L;
            snap.buffers[1].clipFlags |= ChannelMeterSnapshot::CLIP_L;
        }
        if (clipR) {
            snap.buffers[0].clipFlags |= ChannelMeterSnapshot::CLIP_R;
            snap.buffers[1].clipFlags |= ChannelMeterSnapshot::CLIP_R;
        }
    }

    /**
     * @brief Read meter snapshot from UI thread.
     *
     * Uses acquire semantics to see audio thread writes.
     * Returns LINEAR peak values - UI should convert to dB for display.
     *
     * @param slotIndex Dense slot index
     * @param[out] peakL Left channel peak (LINEAR 0..1)
     * @param[out] peakR Right channel peak (LINEAR 0..1)
     * @param[out] clipL True if left channel has clipped
     * @param[out] clipR True if right channel has clipped
     */
    void readSnapshot(uint32_t slotIndex, float& peakL, float& peakR,
                      bool& clipL, bool& clipR) const {
        if (slotIndex >= MAX_CHANNELS) {
            peakL = peakR = 0.0f;
            clipL = clipR = false;
            return;
        }
        const auto& snap = m_snapshots[slotIndex];
        // Read from opposite buffer (the one audio isn't writing to)
        uint8_t readIdx = 1 - snap.writeIndex.load(std::memory_order_acquire);
        const auto& buf = snap.buffers[readIdx];
        peakL = MeterBitcast::u32ToFloat(buf.peakL_bits);
        peakR = MeterBitcast::u32ToFloat(buf.peakR_bits);
        clipL = (buf.clipFlags & ChannelMeterSnapshot::CLIP_L) != 0;
        clipR = (buf.clipFlags & ChannelMeterSnapshot::CLIP_R) != 0;
    }

    /**
     * @brief Clear clip latch for a channel (UI thread).
     *
     * Clears clip flags on both buffers to ensure clean state.
     *
     * @param slotIndex Dense slot index
     */
    void clearClip(uint32_t slotIndex) {
        if (slotIndex >= MAX_CHANNELS) return;
        auto& snap = m_snapshots[slotIndex];
        snap.buffers[0].clipFlags = 0;
        snap.buffers[1].clipFlags = 0;
    }

    /**
     * @brief Reset all meters to zero (UI thread).
     *
     * Useful when stopping playback or resetting the mixer.
     */
    void resetAll() {
        for (auto& snap : m_snapshots) {
            snap.buffers[0].peakL_bits = 0;
            snap.buffers[0].peakR_bits = 0;
            snap.buffers[0].clipFlags = 0;
            snap.buffers[1].peakL_bits = 0;
            snap.buffers[1].peakR_bits = 0;
            snap.buffers[1].clipFlags = 0;
            snap.writeIndex.store(0, std::memory_order_relaxed);
        }
    }

private:
    std::array<ChannelMeterSnapshot, MAX_CHANNELS> m_snapshots;
};

} // namespace Audio
} // namespace Nomad
