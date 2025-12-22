// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioEngine.h"

namespace Nomad {
namespace Audio {

/**
 * @brief Minimal offline render harness for validation/testing.
 *
 * Provides a convenience wrapper to process a fixed number of blocks without
 * audio hardware. Useful for regression tests and sanitizers.
 */
class OfflineRenderHarness {
public:
    OfflineRenderHarness(AudioEngine& engine, uint32_t bufferFrames, uint32_t channels = 2)
        : m_engine(engine)
        , m_bufferFrames(bufferFrames)
        , m_channels(channels)
        , m_buffer(static_cast<size_t>(bufferFrames) * channels, 0.0f) {
        m_engine.setBufferConfig(bufferFrames, channels);
    }

    /**
     * @brief Process N blocks offline.
     *
     * @param blocks Number of blocks to process
     */
    void processBlocks(uint32_t blocks) {
        for (uint32_t i = 0; i < blocks; ++i) {
            m_engine.processBlock(m_buffer.data(), nullptr, m_bufferFrames, 0.0);
        }
    }

    const std::vector<float>& buffer() const { return m_buffer; }

private:
    AudioEngine& m_engine;
    uint32_t m_bufferFrames;
    uint32_t m_channels;
    std::vector<float> m_buffer;
};

} // namespace Audio
} // namespace Nomad
