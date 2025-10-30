// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "Filter.h"
#include <algorithm>

namespace Nomad {
namespace Audio {

Filter::Filter(float sampleRate)
    : m_sampleRate(sampleRate)
    , m_cutoff(1000.0f)
    , m_resonance(0.707f)  // Butterworth response
    , m_type(FilterType::LowPass)
    , m_b0(1.0f), m_b1(0.0f), m_b2(0.0f)
    , m_a1(0.0f), m_a2(0.0f)
    , m_x1(0.0f), m_x2(0.0f)
    , m_y1(0.0f), m_y2(0.0f)
{
    updateCoefficients();
}

void Filter::setType(FilterType type) {
    if (m_type != type) {
        m_type = type;
        updateCoefficients();
    }
}

void Filter::setCutoff(float frequency) {
    // Clamp to valid range
    float newCutoff = std::max(20.0f, std::min(frequency, m_sampleRate * 0.49f));
    
    if (std::abs(newCutoff - m_cutoff) > 0.1f) {
        m_cutoff = newCutoff;
        updateCoefficients();
    }
}

void Filter::setResonance(float resonance) {
    // Clamp to valid range
    float newResonance = std::max(0.1f, std::min(resonance, 10.0f));
    
    if (std::abs(newResonance - m_resonance) > 0.01f) {
        m_resonance = newResonance;
        updateCoefficients();
    }
}

void Filter::reset() {
    m_x1 = m_x2 = 0.0f;
    m_y1 = m_y2 = 0.0f;
}

float Filter::process(float input) {
    // Biquad difference equation:
    // y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    
    float output = m_b0 * input + m_b1 * m_x1 + m_b2 * m_x2
                   - m_a1 * m_y1 - m_a2 * m_y2;
    
    // Update state
    m_x2 = m_x1;
    m_x1 = input;
    m_y2 = m_y1;
    m_y1 = output;
    
    return output;
}

void Filter::updateCoefficients() {
    // Calculate normalized frequency
    float omega = TWO_PI * m_cutoff / m_sampleRate;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    
    // Calculate alpha (bandwidth parameter)
    float alpha = sinOmega / (2.0f * m_resonance);
    
    // Calculate coefficients based on filter type
    switch (m_type) {
        case FilterType::LowPass: {
            // Low-pass filter coefficients
            float b0 = (1.0f - cosOmega) / 2.0f;
            float b1 = 1.0f - cosOmega;
            float b2 = (1.0f - cosOmega) / 2.0f;
            float a0 = 1.0f + alpha;
            float a1 = -2.0f * cosOmega;
            float a2 = 1.0f - alpha;
            
            // Normalize by a0
            m_b0 = b0 / a0;
            m_b1 = b1 / a0;
            m_b2 = b2 / a0;
            m_a1 = a1 / a0;
            m_a2 = a2 / a0;
            break;
        }
        
        case FilterType::HighPass: {
            // High-pass filter coefficients
            float b0 = (1.0f + cosOmega) / 2.0f;
            float b1 = -(1.0f + cosOmega);
            float b2 = (1.0f + cosOmega) / 2.0f;
            float a0 = 1.0f + alpha;
            float a1 = -2.0f * cosOmega;
            float a2 = 1.0f - alpha;
            
            // Normalize by a0
            m_b0 = b0 / a0;
            m_b1 = b1 / a0;
            m_b2 = b2 / a0;
            m_a1 = a1 / a0;
            m_a2 = a2 / a0;
            break;
        }
        
        case FilterType::BandPass: {
            // Band-pass filter coefficients (constant skirt gain)
            float b0 = alpha;
            float b1 = 0.0f;
            float b2 = -alpha;
            float a0 = 1.0f + alpha;
            float a1 = -2.0f * cosOmega;
            float a2 = 1.0f - alpha;
            
            // Normalize by a0
            m_b0 = b0 / a0;
            m_b1 = b1 / a0;
            m_b2 = b2 / a0;
            m_a1 = a1 / a0;
            m_a2 = a2 / a0;
            break;
        }
    }
}

} // namespace Audio
} // namespace Nomad
