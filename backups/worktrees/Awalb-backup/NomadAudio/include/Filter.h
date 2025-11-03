// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
ed#pragma once

/**
 * @file Filter.h
 * @brief DSP filters for audio processing
 * 
 * Provides various filter types optimized for real-time audio:
 * - Low-pass filter (removes high frequencies)
 * - High-pass filter (removes low frequencies)
 * - Band-pass filter (passes frequencies in a range)
 * 
 * Uses biquad filter topology for efficient computation and stability.
 * 
 * @version 1.0.0
 * @license MIT
 */

#include <cmath>
#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief Filter types
 */
enum class FilterType {
    LowPass,
    HighPass,
    BandPass
};

/**
 * @brief Real-time biquad filter
 * 
 * Features:
 * - Multiple filter types (low-pass, high-pass, band-pass)
 * - Resonance control for emphasis at cutoff frequency
 * - Stable biquad topology
 * - Optimized for real-time audio processing
 * 
 * Usage:
 * @code
 * Filter filter(44100.0f);
 * filter.setType(FilterType::LowPass);
 * filter.setCutoff(1000.0f);
 * filter.setResonance(0.7f);
 * 
 * float output = filter.process(input);
 * @endcode
 */
class Filter {
public:
    /**
     * @brief Construct filter with sample rate
     * @param sampleRate Audio sample rate in Hz
     */
    explicit Filter(float sampleRate);

    /**
     * @brief Set filter type
     * @param type Filter type (LowPass, HighPass, BandPass)
     */
    void setType(FilterType type);

    /**
     * @brief Set cutoff frequency
     * @param frequency Cutoff frequency in Hz (20 - 20000)
     */
    void setCutoff(float frequency);

    /**
     * @brief Set resonance (Q factor)
     * @param resonance Resonance amount (0.1 - 10.0, default 0.707)
     */
    void setResonance(float resonance);

    /**
     * @brief Reset filter state
     */
    void reset();

    /**
     * @brief Process single sample
     * @param input Input sample
     * @return Filtered output sample
     */
    float process(float input);

    /**
     * @brief Get current cutoff frequency
     * @return Cutoff frequency in Hz
     */
    float getCutoff() const { return m_cutoff; }

    /**
     * @brief Get current resonance
     * @return Resonance value
     */
    float getResonance() const { return m_resonance; }

    /**
     * @brief Get current filter type
     * @return Filter type
     */
    FilterType getType() const { return m_type; }

private:
    /**
     * @brief Update filter coefficients
     */
    void updateCoefficients();

    // Filter state
    float m_sampleRate;
    float m_cutoff;
    float m_resonance;
    FilterType m_type;

    // Biquad coefficients
    float m_b0, m_b1, m_b2;  // Numerator
    float m_a1, m_a2;        // Denominator (a0 is normalized to 1)

    // Filter memory (state variables)
    float m_x1, m_x2;  // Input history
    float m_y1, m_y2;  // Output history

    // Constants
    static constexpr float PI = 3.14159265359f;
    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace Audio
} // namespace Nomad
