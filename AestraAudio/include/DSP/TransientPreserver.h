// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * @brief Fast envelope follower with asymmetric attack/release
 *
 * Uses different time constants for attack (fast) and release (slow)
 * to accurately track signal transients.
 */
struct EnvelopeFollower {
    float envelope = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float sampleRate = 48000.0f;

    /**
     * @brief Configure time constants
     *
     * @param attackMs Attack time in milliseconds (typically 0.1-5ms)
     * @param releaseMs Release time in milliseconds (typically 20-100ms)
     * @param sr Sample rate in Hz
     */
    void configure(float attackMs, float releaseMs, float sr) noexcept {
        sampleRate = sr;
        // Convert ms to coefficient: coeff = 1 - exp(-2.2 / (time * sr))
        // 2.2 is for 10% settling time
        attackCoeff = 1.0f - std::exp(-2.2f / (attackMs * 0.001f * sr));
        releaseCoeff = 1.0f - std::exp(-2.2f / (releaseMs * 0.001f * sr));
    }

    /**
     * @brief Process a single sample and update envelope
     *
     * @param sample Absolute sample value
     * @return Current envelope value
     */
    inline float process(float sample) noexcept {
        float absSample = std::abs(sample);

        if (absSample > envelope) {
            // Attack: fast rise
            envelope += attackCoeff * (absSample - envelope);
        } else {
            // Release: slow decay
            envelope += releaseCoeff * (absSample - envelope);
        }

        return envelope;
    }

    void reset() noexcept { envelope = 0.0f; }
};

/**
 * @brief Transient detector using derivative of envelope
 *
 * Detects transients by monitoring the rate of change of the envelope.
 * When the derivative exceeds a threshold, a transient is flagged.
 */
struct TransientDetector {
    EnvelopeFollower envFollower;
    float prevEnvelope = 0.0f;
    float derivativeSmooth = 0.0f;
    float sampleRate = 48000.0f;

    // Detection parameters
    float threshold = 0.1f;   // Sensitivity (0.05-0.5)
    float holdSamples = 0.0f; // Remaining samples in transient region
    float holdTimeMs = 10.0f; // How long to hold transient flag after detection

    /**
     * @brief Configure the transient detector
     *
     * @param attackMs Envelope attack time (fast, ~1ms)
     * @param releaseMs Envelope release time (slower, ~50ms)
     * @param holdMs How long to sustain transient flag
     * @param sensitivity Threshold for derivative detection
     * @param sr Sample rate
     */
    void configure(float attackMs, float releaseMs, float holdMs, float sensitivity, float sr) noexcept {
        sampleRate = sr;
        holdTimeMs = holdMs;
        threshold = sensitivity;
        envFollower.configure(attackMs, releaseMs, sr);
    }

    /**
     * @brief Process mono sample and check for transient
     *
     * @param sample Input sample
     * @return true if currently in transient region
     */
    inline bool process(float sample) noexcept {
        float env = envFollower.process(sample);

        // Compute envelope derivative (rate of change)
        float derivative = env - prevEnvelope;
        prevEnvelope = env;

        // Smooth the derivative slightly
        derivativeSmooth = 0.9f * derivativeSmooth + 0.1f * derivative;

        // Detect transient: positive derivative above threshold
        if (derivativeSmooth > threshold) {
            holdSamples = holdTimeMs * 0.001f * sampleRate;
        }

        // Decay hold counter
        if (holdSamples > 0.0f) {
            holdSamples -= 1.0f;
            return true;
        }

        return false;
    }

    /**
     * @brief Process stereo sample and check for transient
     */
    inline bool processStereo(float sampleL, float sampleR) noexcept {
        // Use max of both channels
        return process(std::max(std::abs(sampleL), std::abs(sampleR)));
    }

    void reset() noexcept {
        envFollower.reset();
        prevEnvelope = 0.0f;
        derivativeSmooth = 0.0f;
        holdSamples = 0.0f;
    }
};

/**
 * @brief Transient preserver for integration with SampleRateConverter
 *
 * Analyzes audio to detect transients and provides an API for the
 * sample rate converter to query whether the current position is
 * in a transient region, allowing it to reduce time-stretch smoothing.
 *
 * Usage:
 * @code
 *   TransientPreserver preserver;
 *   preserver.configure(48000.0f);
 *
 *   // Pre-analyze the audio (or analyze in real-time)
 *   preserver.analyzeBlock(audioData, numFrames);
 *
 *   // During time-stretch:
 *   if (preserver.isTransientRegion(currentPosition)) {
 *       // Reduce ratio smoothing to preserve attack
 *   }
 * @endcode
 */
class TransientPreserver {
public:
    TransientPreserver() = default;

    /**
     * @brief Configure the transient preserver
     *
     * @param sampleRate Audio sample rate
     * @param sensitivity Detection sensitivity (0.0-1.0, default 0.3)
     */
    void configure(float sampleRate, float sensitivity = 0.3f) noexcept {
        m_sampleRate = sampleRate;
        m_sensitivity = sensitivity;

        // Configure detector with typical values:
        // Fast attack (1ms), medium release (50ms), short hold (10ms)
        m_detector.configure(1.0f, 50.0f, 10.0f, sensitivity * 0.2f, sampleRate);
    }

    /**
     * @brief Enable/disable transient preservation
     */
    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    bool isEnabled() const noexcept { return m_enabled; }

    /**
     * @brief Set detection sensitivity
     *
     * @param sensitivity 0.0 = less sensitive (fewer transients detected)
     *                    1.0 = more sensitive (more transients detected)
     */
    void setSensitivity(float sensitivity) noexcept {
        m_sensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
        m_detector.threshold = m_sensitivity * 0.2f;
    }

    float getSensitivity() const noexcept { return m_sensitivity; }

    /**
     * @brief Process a single stereo frame in real-time
     *
     * Call this for each frame to update transient detection state.
     *
     * @param sampleL Left channel sample
     * @param sampleR Right channel sample
     * @return true if currently in transient region
     */
    inline bool processFrame(float sampleL, float sampleR) noexcept {
        if (!m_enabled)
            return false;

        m_inTransient = m_detector.processStereo(sampleL, sampleR);
        return m_inTransient;
    }

    /**
     * @brief Check if we're currently in a transient region
     *
     * Use this from SampleRateConverter to adjust smoothing behavior.
     */
    bool isInTransientRegion() const noexcept { return m_enabled && m_inTransient; }

    /**
     * @brief Get ratio smoothing multiplier for current state
     *
     * When in a transient region, returns a value < 1.0 to reduce smoothing.
     * When not in transient, returns 1.0 for normal smoothing.
     *
     * @return Smoothing multiplier [0.1, 1.0]
     */
    float getSmoothingMultiplier() const noexcept {
        if (!m_enabled)
            return 1.0f;
        return m_inTransient ? 0.1f : 1.0f;
    }

    /**
     * @brief Process a block of interleaved stereo audio
     *
     * @param data Interleaved stereo data [L0, R0, L1, R1, ...]
     * @param numFrames Number of stereo frames
     */
    void processBlock(const float* data, uint32_t numFrames) noexcept {
        if (!m_enabled)
            return;

        for (uint32_t i = 0; i < numFrames; ++i) {
            m_inTransient = m_detector.processStereo(data[i * 2], data[i * 2 + 1]);
        }
    }

    /**
     * @brief Reset internal state
     */
    void reset() noexcept {
        m_detector.reset();
        m_inTransient = false;
    }

    /**
     * @brief Get current envelope level (for visualization)
     */
    float getEnvelopeLevel() const noexcept { return m_detector.envFollower.envelope; }

private:
    TransientDetector m_detector;
    float m_sampleRate = 48000.0f;
    float m_sensitivity = 0.3f;
    bool m_enabled = true;
    bool m_inTransient = false;
};

} // namespace Audio
} // namespace Aestra
