// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "SampleRateConverter.h"
#include <atomic>
#include <vector>
#include <cmath>

namespace Nomad {
namespace Audio {

/**
 * @brief Adaptive Polyphase Resampler
 *
 * An optimized wrapper around SampleRateConverter that dynamically adjusts
 * resampling quality based on the input signal content to save CPU.
 *
 * Strategy:
 * - Detects signal frequency content (zero-crossing rate / flux).
 * - If signal is low-frequency dominant (e.g. Bass, Pad), switch to Sinc16 or Cubic.
 * - If signal has high-frequency transients (e.g. Cymbals, Snares), switch to Sinc64.
 * - Handles smooth cross-fading when quality changes to avoid clicks.
 */
class AdaptiveResampler {
public:
    AdaptiveResampler();
    ~AdaptiveResampler() = default;

    /**
     * @brief Configure the resampler.
     * @param srcRate Source sample rate
     * @param dstRate Destination sample rate
     * @param channels Number of channels
     * @param initialQuality Starting quality (default: Sinc64)
     */
    void configure(uint32_t srcRate, uint32_t dstRate, uint32_t channels,
                   SRCQuality initialQuality = SRCQuality::Sinc64);

    /**
     * @brief Process a block of audio.
     * @param input Input buffer (interleaved)
     * @param inputFrames Number of input frames
     * @param output Output buffer (interleaved)
     * @param maxOutputFrames Maximum size of output buffer
     * @return Number of output frames generated
     */
    uint32_t process(const float* input, uint32_t inputFrames,
                     float* output, uint32_t maxOutputFrames);

    /**
     * @brief Set sensitivity for quality switching.
     * @param sensitivity 0.0 to 1.0 (higher = more likely to use high quality)
     */
    void setSensitivity(float sensitivity);

    /**
     * @brief Force a specific quality mode (disables adaptation).
     */
    void forceQuality(SRCQuality quality);

    /**
     * @brief Enable/Disable adaptation.
     */
    void setAdaptationEnabled(bool enabled);

private:
    // Signal analysis helper
    void analyzeSignal(const float* input, uint32_t frames);

    // Internal state
    SampleRateConverter m_converterHigh; // High quality (Sinc64)
    SampleRateConverter m_converterLow;  // Low quality (Sinc16/Cubic)

    // Cross-fade state
    bool m_usingHighQuality{true};
    float m_crossfadePos{0.0f};
    bool m_isCrossfading{false};

    // Config
    uint32_t m_srcRate{48000};
    uint32_t m_dstRate{48000};
    uint32_t m_channels{2};
    bool m_adaptationEnabled{true};
    float m_sensitivity{0.5f};

    // Analysis state
    float m_energyHistory{0.0f};
    float m_zcrHistory{0.0f};

    // Input history for priming
    std::vector<float> m_inputHistory;

    // Constants
    static constexpr uint32_t FADE_FRAMES = 256;
};

} // namespace Audio
} // namespace Nomad
