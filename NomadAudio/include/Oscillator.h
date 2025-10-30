// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file Oscillator.h
 * @brief DSP oscillators with anti-aliasing
 * 
 * Provides sine, saw, and square wave oscillators optimized for real-time audio.
 * Includes PolyBLEP anti-aliasing for saw and square waves to reduce aliasing artifacts.
 * 
 * @version 1.0.0
 * @license MIT
 */

#include <cmath>
#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief Oscillator waveform types
 */
enum class WaveformType {
    Sine,
    Saw,
    Square
};

/**
 * @brief Real-time oscillator with anti-aliasing
 * 
 * Features:
 * - Multiple waveform types (sine, saw, square)
 * - PolyBLEP anti-aliasing for non-bandlimited waveforms
 * - Phase-accurate frequency modulation
 * - Optimized for real-time audio processing
 * 
 * Usage:
 * @code
 * Oscillator osc(44100.0f);
 * osc.setFrequency(440.0f);  // A4
 * osc.setWaveform(WaveformType::Sine);
 * 
 * float sample = osc.process();  // Generate next sample
 * @endcode
 */
class Oscillator {
public:
    /**
     * @brief Construct oscillator with sample rate
     * @param sampleRate Audio sample rate in Hz
     */
    explicit Oscillator(float sampleRate);

    /**
     * @brief Set oscillator frequency
     * @param frequency Frequency in Hz (20 - 20000)
     */
    void setFrequency(float frequency);

    /**
     * @brief Set waveform type
     * @param type Waveform type (Sine, Saw, Square)
     */
    void setWaveform(WaveformType type);

    /**
     * @brief Set pulse width for square wave
     * @param width Pulse width (0.0 - 1.0, default 0.5)
     */
    void setPulseWidth(float width);

    /**
     * @brief Reset phase to zero
     */
    void reset();

    /**
     * @brief Process next sample
     * @return Audio sample in range [-1.0, 1.0]
     */
    float process();

    /**
     * @brief Get current frequency
     * @return Frequency in Hz
     */
    float getFrequency() const { return m_frequency; }

    /**
     * @brief Get current waveform type
     * @return Waveform type
     */
    WaveformType getWaveform() const { return m_waveform; }

private:
    // Waveform generators
    float generateSine();
    float generateSaw();
    float generateSquare();

    // PolyBLEP anti-aliasing
    float polyBLEP(float t);

    // State
    float m_sampleRate;
    float m_frequency;
    float m_phase;
    float m_phaseIncrement;
    float m_pulseWidth;
    WaveformType m_waveform;

    // Constants
    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace Audio
} // namespace Nomad
