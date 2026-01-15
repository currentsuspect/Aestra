// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Oscillator.h"
#include <algorithm>
#include <cmath>

namespace Nomad {
namespace Audio {

Oscillator::Oscillator(float sampleRate)
    : m_sampleRate(sampleRate)
    , m_frequency(440.0f)
    , m_phase(0.0f)
    , m_phaseIncrement(0.0f)
    , m_pulseWidth(0.5f)
    , m_waveform(WaveformType::Sine)
{
    setFrequency(m_frequency);
}

void Oscillator::setFrequency(float frequency) {
    // Clamp frequency to audible range
    m_frequency = std::max(20.0f, std::min(frequency, 20000.0f));
    m_phaseIncrement = m_frequency / m_sampleRate;
}

void Oscillator::setWaveform(WaveformType type) {
    m_waveform = type;
}

void Oscillator::setPulseWidth(float width) {
    m_pulseWidth = std::max(0.01f, std::min(width, 0.99f));
}

void Oscillator::reset() {
    m_phase = 0.0f;
}

float Oscillator::process() {
    float output = 0.0f;

    switch (m_waveform) {
        case WaveformType::Sine:
            output = generateSine();
            break;
        case WaveformType::Saw:
            output = generateSaw();
            break;
        case WaveformType::Square:
            output = generateSquare();
            break;
    }

    // Advance phase
    m_phase += m_phaseIncrement;
    if (m_phase >= 1.0f) {
        m_phase -= 1.0f;
    }

    return output;
}

float Oscillator::generateSine() {
    // Pure sine wave (already bandlimited)
    return std::sin(m_phase * TWO_PI);
}

float Oscillator::generateSaw() {
    // Naive sawtooth
    float naive = 2.0f * m_phase - 1.0f;
    
    // Apply PolyBLEP anti-aliasing at discontinuities
    naive -= polyBLEP(m_phase);
    
    return naive;
}

float Oscillator::generateSquare() {
    // Naive square wave
    float naive = (m_phase < m_pulseWidth) ? 1.0f : -1.0f;
    
    // Apply PolyBLEP at both edges
    naive += polyBLEP(m_phase);
    naive -= polyBLEP(std::fmod(m_phase + (1.0f - m_pulseWidth), 1.0f));
    
    return naive;
}

float Oscillator::polyBLEP(float t) {
    // PolyBLEP (Polynomial Bandlimited Step)
    // Reduces aliasing by smoothing discontinuities
    
    float dt = m_phaseIncrement;
    
    // 0 <= t < 1
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    // -1 < t < 0
    else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    
    return 0.0f;
}

} // namespace Audio
} // namespace Nomad
