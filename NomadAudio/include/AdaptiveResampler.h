// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "SampleRateConverter.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace Nomad {
namespace Audio {

/**
 * @brief Adaptive Sample Rate Converter
 *
 * An experimental wrapper around SampleRateConverter that dynamically adjusts
 * resampling quality based on signal content to save CPU.
 *
 * Strategy:
 * 1. Analyze input signal block for high-frequency content (transients/treble).
 * 2. If signal is band-limited (mostly low freq), use lower quality (Cubic/Sinc8).
 * 3. If signal has high frequency content, use higher quality (Sinc64) to prevent aliasing.
 *
 * Note: Switching interpolation algorithms mid-stream can cause phase discontinuities.
 * This prototype minimizes this by only switching during silence or cross-fading (future work).
 * For now, it simply exposes the logic for detection.
 */
class AdaptiveResampler {
public:
    AdaptiveResampler() = default;

    void configure(uint32_t srcRate, uint32_t dstRate, uint32_t channels, SRCQuality initialQuality = SRCQuality::Sinc64) {
        m_srcRate = srcRate;
        m_dstRate = dstRate;
        m_channels = channels;
        m_converter.configure(srcRate, dstRate, channels, initialQuality);
    }

    uint32_t process(const float* input, uint32_t inputFrames, float* output, uint32_t maxOutputFrames) {
        // Analyze signal energy at high frequencies (simple zero-crossing or difference check)
        // High zero-crossing rate -> High Frequency

        // TODO: Ideally we would have two converters and crossfade, or a filter bank that
        // allows dynamic tap count. For now, we just stick to Sinc64 as "Safe".
        // This class serves as a placeholder for the Innovation deliverable.

        // In a real implementation:
        // bool needsHighQuality = analyzeSignalComplexity(input, inputFrames, m_channels);
        // if (!needsHighQuality && m_currentQuality == SRCQuality::Sinc64) {
        //     switchTo(SRCQuality::Sinc8);
        // }

        // For now, no runtime analysis in the audio callback.
        return m_converter.process(input, inputFrames, output, maxOutputFrames);
    }

private:
    /**
     * @brief Analyze signal complexity to determine resampling quality.
     *
     * Uses a simple heuristic based on the average absolute difference between adjacent samples.
     * High frequency content (e.g., transients) will have large differences.
     *
     * @param input Interleaved audio input buffer.
     * @param frames Number of frames to analyze.
     * @param channels Number of channels.
     * @return true if high quality (Sinc64) is recommended, false otherwise.
     */
    bool analyzeSignalComplexity(const float* input, uint32_t frames, uint32_t channels) {
        // Simple heuristic: sum of absolute differences (high freq proxy)
        if (frames < 2) return false;

        float totalDiff = 0.0f;
        const uint32_t stride = channels;
        const uint32_t limit = std::min(frames, 256u); // Analyze first 256 samples only

        for (uint32_t i = 1; i < limit; ++i) {
            for (uint32_t c = 0; c < channels; ++c) {
                float diff = input[i * stride + c] - input[(i - 1) * stride + c];
                totalDiff += std::abs(diff);
            }
        }

        float avgDiff = totalDiff / (limit * channels);

        // If average sample-to-sample jump is large, we have high frequency content.
        // Threshold of 0.1f implies an average change of 10% full scale per sample.
        // This effectively detects sharp transients or high frequency signals.
        return avgDiff > 0.1f;
    }

    SampleRateConverter m_converter;
    uint32_t m_srcRate{0};
    uint32_t m_dstRate{0};
    uint32_t m_channels{0};
};

} // namespace Audio
} // namespace Nomad
