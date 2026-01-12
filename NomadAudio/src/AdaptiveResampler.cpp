// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AdaptiveResampler.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Nomad {
namespace Audio {

AdaptiveResampler::AdaptiveResampler() {
    // Initialize default state
}

void AdaptiveResampler::configure(uint32_t srcRate, uint32_t dstRate, uint32_t channels, SRCQuality initialQuality) {
    m_srcRate = srcRate;
    m_dstRate = dstRate;
    m_channels = channels;

    // Configure both internal converters
    m_converterHigh.configure(srcRate, dstRate, channels, SRCQuality::Sinc64);
    // Use Sinc16 as the "Low" quality (good trade-off vs Cubic)
    m_converterLow.configure(srcRate, dstRate, channels, SRCQuality::Sinc16);

    m_usingHighQuality = (initialQuality >= SRCQuality::Sinc32);
    m_crossfadePos = m_usingHighQuality ? 1.0f : 0.0f;
    m_isCrossfading = false;

    // Initialize buffer for signal priming (store 128 frames)
    m_inputHistory.resize(128 * channels, 0.0f);
}

void AdaptiveResampler::setSensitivity(float sensitivity) {
    m_sensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
}

void AdaptiveResampler::setAdaptationEnabled(bool enabled) {
    m_adaptationEnabled = enabled;
    if (!enabled) {
        // If disabled, default to High quality
        m_usingHighQuality = true;
        m_isCrossfading = true; // Trigger fade to high
    }
}

void AdaptiveResampler::forceQuality(SRCQuality quality) {
    m_adaptationEnabled = false;
    m_usingHighQuality = (quality >= SRCQuality::Sinc32);
    m_isCrossfading = true;
}

void AdaptiveResampler::analyzeSignal(const float* input, uint32_t frames) {
    if (frames == 0) return;

    // Simple analysis: Zero Crossing Rate (ZCR) and High Frequency Energy
    // We scan a subset of samples to save CPU (stride of 4)

    float highFreqEnergy = 0.0f;
    float prevSample = 0.0f;
    uint32_t zcr = 0;

    const uint32_t stride = 4;
    uint32_t count = 0;

    for (uint32_t i = 0; i < frames; i += stride) {
        float sample = input[i * m_channels]; // Analyze first channel only

        // ZCR approximation
        if ((sample >= 0 && prevSample < 0) || (sample < 0 && prevSample >= 0)) {
            zcr++;
        }

        // High frequency proxy: large deltas between samples
        float delta = sample - prevSample;
        highFreqEnergy += delta * delta;

        prevSample = sample;
        count++;
    }

    if (count > 0) {
        float zcrNorm = static_cast<float>(zcr) / count;
        float energyNorm = highFreqEnergy / count;

        // Update history (exponential moving average)
        m_zcrHistory = 0.9f * m_zcrHistory + 0.1f * zcrNorm;
        m_energyHistory = 0.9f * m_energyHistory + 0.1f * energyNorm;

        // Decision Logic
        // Thresholds tuned empirically:
        // High ZCR (> 0.1) or High Delta Energy implies high frequency content -> Need Sinc64
        // Low ZCR (< 0.05) and Low Energy -> Sinc16 is sufficient

        float threshold = 0.1f * (1.1f - m_sensitivity); // Sensitivity adjusts threshold

        bool needsHighQuality = (m_zcrHistory > threshold) || (m_energyHistory > 0.01f);

        if (needsHighQuality != m_usingHighQuality) {
            m_usingHighQuality = needsHighQuality;
            m_isCrossfading = true;
        }
    }
}

uint32_t AdaptiveResampler::process(const float* input, uint32_t inputFrames,
                                    float* output, uint32_t maxOutputFrames) {
    if (m_adaptationEnabled) {
        analyzeSignal(input, inputFrames);
    }

    // Store input tail for priming next time if needed
    if (inputFrames > 0) {
        size_t keep = std::min(static_cast<size_t>(inputFrames * m_channels), m_inputHistory.size());
        size_t offset = (inputFrames * m_channels) - keep;
        std::memcpy(m_inputHistory.data(), input + offset, keep * sizeof(float));
    }

    if (!m_isCrossfading) {
        // Fast path: Single converter
        // IMPORTANT: We do NOT reset here. We rely on the fact that if we just switched,
        // we might have a discontinuity if we didn't crossfade.
        // But since we trigger crossfade on switch, we should be fine?
        // Wait, analyzeSignal triggers m_isCrossfading = true.
        // So we enter crossfade block below.

        if (m_usingHighQuality) {
            return m_converterHigh.process(input, inputFrames, output, maxOutputFrames);
        } else {
            return m_converterLow.process(input, inputFrames, output, maxOutputFrames);
        }
    } else {
        // Cross-fading path
        // We render BOTH and mix them.

        static std::vector<float> bufHigh;
        static std::vector<float> bufLow;

        if (bufHigh.size() < maxOutputFrames * m_channels) {
            bufHigh.resize(maxOutputFrames * m_channels);
            bufLow.resize(maxOutputFrames * m_channels);
        }

        // Prime the "cold" converter with history if needed?
        // SampleRateConverter doesn't have a public prime() method,
        // but it buffers input.
        // For now, we accept that the cold converter starts with zero history.
        // The crossfade duration (FADE_FRAMES) should hide the startup glitch.

        uint32_t nHigh = m_converterHigh.process(input, inputFrames, bufHigh.data(), maxOutputFrames);
        uint32_t nLow = m_converterLow.process(input, inputFrames, bufLow.data(), maxOutputFrames);

        uint32_t nOut = std::min(nHigh, nLow);

        // Mix: output = alpha * High + (1-alpha) * Low
        float targetAlpha = m_usingHighQuality ? 1.0f : 0.0f;
        float alphaStep = (targetAlpha - m_crossfadePos) / static_cast<float>(nOut);

        // Safety clamp on step to avoid overshooting in one block
        if (std::abs(alphaStep) > 1.0f / FADE_FRAMES) {
            alphaStep = (alphaStep > 0) ? (1.0f / FADE_FRAMES) : (-1.0f / FADE_FRAMES);
        }

        for (uint32_t i = 0; i < nOut; ++i) {
            m_crossfadePos += alphaStep;
            m_crossfadePos = std::clamp(m_crossfadePos, 0.0f, 1.0f);

            for (uint32_t ch = 0; ch < m_channels; ++ch) {
                float valHigh = bufHigh[i * m_channels + ch];
                float valLow = bufLow[i * m_channels + ch];
                output[i * m_channels + ch] = valHigh * m_crossfadePos + valLow * (1.0f - m_crossfadePos);
            }
        }

        // Check if fade complete
        if (std::abs(m_crossfadePos - targetAlpha) < 0.001f) {
            m_crossfadePos = targetAlpha;
            m_isCrossfading = false;
        }

        return nOut;
    }
}

} // namespace Audio
} // namespace Nomad
