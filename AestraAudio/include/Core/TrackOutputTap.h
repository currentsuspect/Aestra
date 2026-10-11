// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Copies one mixer channel's output, as it leaves the strip and before
 *        it is routed to the master or a group (V8-W5, Bounce in Place).
 *
 * "Track output" is the channel's post-insert, post-delay-compensation,
 * post-fader/pan buffer: exactly what the channel contributes to the rest of
 * the graph. The offline renderer arms it for the duration of one render and
 * reads each block back after processBlock() on the same thread, so the audio
 * thread only writes into storage that was reserved up front.
 */
class TrackOutputTap {
public:
    static constexpr uint32_t kNoChannel = 0xFFFFFFFFu;

    /** @brief Control thread. Arms the tap with room for @p maxFrames stereo frames. */
    void arm(uint32_t mixerChannelId, uint32_t maxFrames) {
        m_block.assign(static_cast<size_t>(maxFrames) * 2u, 0.0f);
        m_frames = 0;
        m_channel.store(mixerChannelId, std::memory_order_release);
    }
    void disarm() { m_channel.store(kNoChannel, std::memory_order_release); }

    /** @brief True when @p mixerChannelId is the armed channel. */
    bool wants(uint32_t mixerChannelId) const {
        return m_channel.load(std::memory_order_acquire) == mixerChannelId;
    }

    /** @brief Audio/render thread. Takes interleaved stereo doubles; never allocates. */
    void capture(const double* interleaved, uint32_t frames) {
        const uint32_t n = frames * 2u <= m_block.size() ? frames : static_cast<uint32_t>(m_block.size() / 2u);
        for (uint32_t i = 0; i < n * 2u; ++i) {
            m_block[i] = static_cast<float>(interleaved[i]);
        }
        m_frames = n;
    }

    /** @brief The last captured block (interleaved stereo); frames == 0 if the channel did not render. */
    const float* block() const { return m_block.data(); }
    uint32_t frames() const { return m_frames; }
    void clearBlock() { m_frames = 0; }

private:
    std::atomic<uint32_t> m_channel{kNoChannel};
    std::vector<float> m_block;
    uint32_t m_frames{0};
};

} // namespace Audio
} // namespace Aestra
