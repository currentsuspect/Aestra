// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Interleaved audio FIFO owned by one real-time thread.
 *
 * The sandbox exchanges audio with its helper in fixed-size chunks while the
 * engine calls it with whatever block it is rendering (a loop wrap splits a
 * callback in two; an oversized driver block is split into capacity chunks).
 * These FIFOs sit between the two so the transport delay stays one constant
 * chunk however the callback is cut. reset() allocates (non-RT); every other
 * call is allocation-free and bounded.
 */
class InterleavedFifo {
public:
    void reset(size_t capacityFrames, uint32_t channels) {
        m_channels = std::max<uint32_t>(1, channels);
        m_capacity = std::max<size_t>(1, capacityFrames);
        m_data.assign(m_capacity * m_channels, 0.0f);
        clear();
    }
    void clear() noexcept {
        m_read = 0;
        m_frames = 0;
    }
    size_t frames() const noexcept { return m_frames; }
    size_t space() const noexcept { return m_capacity - m_frames; }

    /** @brief Append planar channels (missing channels read as silence). Returns frames written. */
    size_t pushPlanar(const float* const* channels, uint32_t numChannels, size_t frames) noexcept {
        frames = std::min(frames, space());
        for (size_t f = 0; f < frames; ++f) {
            float* dst = slot(m_frames + f);
            for (uint32_t c = 0; c < m_channels; ++c) {
                dst[c] = (channels && c < numChannels && channels[c]) ? channels[c][f] : 0.0f;
            }
        }
        m_frames += frames;
        return frames;
    }
    /** @brief Append interleaved frames with this FIFO's channel count. Returns frames written. */
    size_t pushInterleaved(const float* data, size_t frames) noexcept {
        frames = std::min(frames, space());
        for (size_t f = 0; f < frames; ++f) {
            std::memcpy(slot(m_frames + f), data + f * m_channels, m_channels * sizeof(float));
        }
        m_frames += frames;
        return frames;
    }
    /** @brief Append silence. Returns frames written. */
    size_t pushSilence(size_t frames) noexcept {
        frames = std::min(frames, space());
        for (size_t f = 0; f < frames; ++f) {
            std::memset(slot(m_frames + f), 0, m_channels * sizeof(float));
        }
        m_frames += frames;
        return frames;
    }
    /** @brief Remove frames into interleaved storage. */
    void popInterleaved(float* dst, size_t frames) noexcept {
        frames = std::min(frames, m_frames);
        for (size_t f = 0; f < frames; ++f) {
            std::memcpy(dst + f * m_channels, slot(f), m_channels * sizeof(float));
        }
        discard(frames);
    }
    /** @brief Remove frames into planar outputs; outputs past this FIFO's channels are silenced. */
    void popPlanar(float* const* outputs, uint32_t numOutputs, size_t frames) noexcept {
        frames = std::min(frames, m_frames);
        for (uint32_t c = 0; c < numOutputs; ++c) {
            if (!outputs || !outputs[c]) {
                continue;
            }
            for (size_t f = 0; f < frames; ++f) {
                outputs[c][f] = c < m_channels ? slot(f)[c] : 0.0f;
            }
        }
        discard(frames);
    }
    void discard(size_t frames) noexcept {
        frames = std::min(frames, m_frames);
        m_read = (m_read + frames) % m_capacity;
        m_frames -= frames;
    }

private:
    float* slot(size_t offsetFromRead) noexcept {
        return m_data.data() + ((m_read + offsetFromRead) % m_capacity) * m_channels;
    }
    std::vector<float> m_data;
    size_t m_capacity{1};
    uint32_t m_channels{1};
    size_t m_read{0};
    size_t m_frames{0};
};

/**
 * @brief MIDI events waiting to ride the next sandbox chunk, with offsets in FIFO time.
 *
 * Events arrive per callback with offsets into that callback; they leave per chunk
 * with offsets into that chunk. Fixed capacity; an event that does not fit is dropped
 * and counted by the caller.
 */
class ChunkMidiQueue {
public:
    struct Event {
        uint64_t frame; ///< position in the input FIFO's timeline
        uint8_t data[3];
    };
    static constexpr size_t kCapacity = 1024;

    bool push(uint64_t frame, const uint8_t* data) noexcept {
        if (m_count == kCapacity) {
            return false;
        }
        Event& e = m_events[m_count++];
        e.frame = frame;
        std::memcpy(e.data, data, 3);
        return true;
    }
    /** @brief Move the events before @p chunkEnd out, as {offset, size, data} records relative to @p chunkStart. */
    size_t takeChunk(uint64_t chunkStart, uint64_t chunkEnd, uint8_t* out, size_t outCapacityBytes) noexcept {
        size_t bytes = 0;
        size_t kept = 0;
        for (size_t i = 0; i < m_count; ++i) {
            const Event& e = m_events[i];
            if (e.frame < chunkEnd) {
                if (bytes + 8 <= outCapacityBytes) {
                    const uint32_t offset = static_cast<uint32_t>(e.frame > chunkStart ? e.frame - chunkStart : 0);
                    std::memcpy(out + bytes, &offset, sizeof(offset));
                    out[bytes + 4] = 3;
                    std::memcpy(out + bytes + 5, e.data, 3);
                    bytes += 8;
                }
            } else {
                m_events[kept++] = e;
            }
        }
        m_count = kept;
        return bytes;
    }
    void clear() noexcept { m_count = 0; }

private:
    Event m_events[kCapacity]{};
    size_t m_count{0};
};

} // namespace Audio
} // namespace Aestra
