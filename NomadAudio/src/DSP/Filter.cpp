// ============================================================================
// © 2025 Nomad Studios — Audio DSP Filter Library
// ============================================================================

#include "Filter.h"
#include <cstring>
#include <complex>

#ifdef Filter
#undef Filter
#endif

namespace Nomad {
namespace Audio {
namespace DSP {

// ============================================================================
// Constructor & initialization
// ============================================================================

Filter::Filter(float sampleRate)
    : m_sampleRate(sampleRate)
{
    prepare(sampleRate);
    reset();
}

void Filter::prepare(float sampleRate)
{
    m_sampleRate = sampleRate;
    m_nyquist = sampleRate * NYQUIST_MARGIN;
    m_inverseSampleRate = 1.0f / sampleRate;
    
    // Reset states
    reset();
    
    // Resize oversampling buffer if needed
    if (m_oversampling != OversamplingFactor::None)
    {
        uint32_t bufferSize = 1024 * static_cast<int>(m_oversampling);
        m_oversampleBuffer.resize(bufferSize);
    }
    
    // Initialize coefficients
    updateCoefficients();
}

// ============================================================================
// Parameter setters
// ============================================================================

void Filter::setType(FilterType type, bool smooth)
{
    if (m_type != type)
    {
        m_type = type;
        if (!smooth)
        {
            // Immediate update for type changes (optional smoothing)
            updateCoefficients();
        }
        m_needsUpdate = true;
    }
}

void Filter::setCutoff(float frequency, float modulate)
{
    // Clamp with modulation range
    float minCutoff = 20.0f;
    float maxCutoff = m_nyquist;
    
    // Apply modulation (-1 to 1 gives ±2 octaves)
    float modulatedFreq = frequency * std::pow(2.0f, modulate * 2.0f);
    
    // Convert to log scale for smoother interpolation
    float logFreq = std::log10(std::max(modulatedFreq, 0.1f));
    float logMin = std::log10(minCutoff);
    float logMax = std::log10(maxCutoff);
    
    float newCutoff = std::clamp(std::pow(10.0f, logFreq), minCutoff, maxCutoff);
    
    if (std::abs(newCutoff - m_targetCutoff) > 0.01f)
    {
        m_targetCutoff = newCutoff;
        m_cutoffMod = modulate;
        m_parametersChanged = true;
    }
}

void Filter::setResonance(float resonance, float modulate)
{
    float newResonance = std::clamp(resonance * (1.0f + modulate * 0.5f), 0.1f, 10.0f);
    
    if (std::abs(newResonance - m_targetResonance) > 0.001f)
    {
        m_targetResonance = newResonance;
        m_resonanceMod = modulate;
        m_parametersChanged = true;
    }
}

void Filter::setGain(float gain)
{
    float newGain = std::clamp(gain, DB_MIN, DB_MAX);
    
    if (std::abs(newGain - m_targetGain) > 0.01f)
    {
        m_targetGain = newGain;
        m_parametersChanged = true;
    }
}

void Filter::setSlope(FilterSlope slope)
{
    if (m_slope != slope)
    {
        m_slope = slope;
        m_needsUpdate = true;
    }
}

void Filter::setDrive(float drive, SaturationType saturation)
{
    m_drive = std::clamp(drive, 0.0f, 1.0f);
    m_saturationType = saturation;
}

void Filter::setOversampling(OversamplingFactor factor)
{
    if (m_oversampling != factor)
    {
        m_oversampling = factor;
        
        if (factor != OversamplingFactor::None)
        {
            uint32_t bufferSize = 1024 * static_cast<int>(factor);
            m_oversampleBuffer.resize(bufferSize);
        }
        
        // Reset filter states for oversampling
        std::memset(m_upsampleFilter, 0, sizeof(m_upsampleFilter));
        std::memset(m_downsampleFilter, 0, sizeof(m_downsampleFilter));
    }
}

void Filter::setSmoothing(float timeMs, SmoothingType type)
{
    m_smoothingTimeMs = std::max(0.0f, timeMs);
    m_smoothingType = type;
    
    // Calculate smoothing coefficient
    float timeConstant = m_smoothingTimeMs * 0.001f;
    float samples = timeConstant * m_sampleRate;
    m_smoothingAlpha = std::exp(-1.0f / samples);
}

void Filter::setStereoLinked(bool linked)
{
    m_stereoLinked = linked;
}

void Filter::setSelfOscillation(bool enabled)
{
    m_selfOscillation = enabled;
}

// ============================================================================
// Core processing
// ============================================================================

float Filter::process(float input)
{
    // Update parameters with smoothing
    updateInternal();
    
    // Apply drive/saturation before filtering
    float processed = input;
    if (m_drive > 0.0f && m_saturationType != SaturationType::None)
    {
        processed = applySaturation(processed * (1.0f + m_drive * 3.0f));
    }
    
    // Process with oversampling if enabled
    if (m_oversampling != OversamplingFactor::None)
    {
        processOversampled(processed, m_state[0]);
    }
    else
    {
        processSample(processed, m_state[0], m_coeffs[0]);
    }
    
    return processed;
}

std::pair<float, float> Filter::processStereo(float left, float right)
{
    updateInternal();
    
    // Apply drive to both channels
    if (m_drive > 0.0f && m_saturationType != SaturationType::None)
    {
        float driveFactor = 1.0f + m_drive * 3.0f;
        left = applySaturation(left * driveFactor);
        right = applySaturation(right * driveFactor);
    }
    
    // Process left channel
    float outLeft = left;
    if (m_oversampling != OversamplingFactor::None)
    {
        processOversampled(outLeft, m_state[0]);
    }
    else
    {
        processSample(outLeft, m_state[0], m_coeffs[0]);
    }
    
    // Process right channel (use same coefficients if linked)
    float outRight = right;
    const auto& rightCoeffs = m_stereoLinked ? m_coeffs[0] : m_coeffs[1];
    if (m_oversampling != OversamplingFactor::None)
    {
        // For simplicity in stereo oversampling, we'll process normally
        processSample(outRight, m_state[1], rightCoeffs);
    }
    else
    {
        processSample(outRight, m_state[1], rightCoeffs);
    }
    
    return {outLeft, outRight};
}

void Filter::processBlock(float* samples, uint32_t numSamples)
{
    if (numSamples == 0) return;
    
    // Update parameters once per block (optimization)
    updateInternal();
    
    for (uint32_t i = 0; i < numSamples; ++i)
    {
        float sample = samples[i];
        
        // Apply drive
        if (m_drive > 0.0f && m_saturationType != SaturationType::None)
        {
            sample = applySaturation(sample * (1.0f + m_drive * 3.0f));
        }
        
        // Process
        if (m_oversampling != OversamplingFactor::None)
        {
            processOversampled(sample, m_state[0]);
        }
        else
        {
            processSample(sample, m_state[0], m_coeffs[0]);
        }
        
        samples[i] = sample;
    }
}

void Filter::processBlockStereo(float* left, float* right, uint32_t numSamples)
{
    if (numSamples == 0) return;
    
    updateInternal();
    
    const auto& rightCoeffs = m_stereoLinked ? m_coeffs[0] : m_coeffs[1];
    
    for (uint32_t i = 0; i < numSamples; ++i)
    {
        // Process left
        float leftSample = left[i];
        if (m_drive > 0.0f && m_saturationType != SaturationType::None)
        {
            leftSample = applySaturation(leftSample * (1.0f + m_drive * 3.0f));
        }
        
        if (m_oversampling != OversamplingFactor::None)
        {
            processOversampled(leftSample, m_state[0]);
        }
        else
        {
            processSample(leftSample, m_state[0], m_coeffs[0]);
        }
        
        // Process right
        float rightSample = right[i];
        if (m_drive > 0.0f && m_saturationType != SaturationType::None)
        {
            rightSample = applySaturation(rightSample * (1.0f + m_drive * 3.0f));
        }
        
        if (m_oversampling != OversamplingFactor::None)
        {
            processSample(rightSample, m_state[1], rightCoeffs);
        }
        else
        {
            processSample(rightSample, m_state[1], rightCoeffs);
        }
        
        left[i] = leftSample;
        right[i] = rightSample;
    }
}

// ============================================================================
// Internal processing methods
// ============================================================================

void Filter::processSample(float& sample, FilterState& state, const BiquadCoefficients& coeffs)
{
    // Direct Form II transposed implementation (more efficient)
    float output = coeffs.b0 * sample + state.x1;
    state.x1 = coeffs.b1 * sample - coeffs.a1 * output + state.x2;
    state.x2 = coeffs.b2 * sample - coeffs.a2 * output;
    
    sample = output;
}

void Filter::processOversampled(float& sample, FilterState& state)
{
    // Simple 2x oversampling implementation
    if (m_oversampling == OversamplingFactor::TwoX)
    {
        // Upsample: insert zero and filter
        float upsampled[2];
        
        // First sample
        upsampled[0] = sample;
        processSample(upsampled[0], state, m_coeffs[0]);
        
        // Interpolated sample
        upsampled[1] = sample;  // Simple hold (better interpolation can be added)
        processSample(upsampled[1], state, m_coeffs[0]);
        
        // Downsample: average (simple low-pass)
        sample = (upsampled[0] + upsampled[1]) * 0.5f;
    }
}

// ============================================================================
// Coefficient calculation methods
// ============================================================================

void Filter::updateCoefficients()
{
    updateCoefficientsInternal();
    m_needsUpdate = false;
}

void Filter::updateCoefficientsInternal()
{
    // Calculate normalized frequency
    float w0 = TWO_PI * m_currentCutoff * m_inverseSampleRate;
    float sinW0 = std::sin(w0);
    float cosW0 = std::cos(w0);
    
    // Clamp resonance based on self-oscillation
    float Q = m_selfOscillation ? 
        std::min(m_currentResonance, 0.99f) : 
        std::clamp(m_currentResonance, 0.1f, 10.0f);
    
    float alpha = sinW0 / (2.0f * Q);
    
    // Convert gain from dB to linear
    float A = std::pow(10.0f, m_currentGain / 40.0f);
    
    // Calculate coefficients based on filter type
    switch (m_type)
    {
    case FilterType::LowPass:
        calculateLowPass(m_targetCoeffs[0], w0, alpha, A);
        break;
        
    case FilterType::HighPass:
        calculateHighPass(m_targetCoeffs[0], w0, alpha, A);
        break;
        
    case FilterType::BandPass:
        calculateBandPass(m_targetCoeffs[0], w0, alpha);
        break;
        
    case FilterType::Notch:
        calculateNotch(m_targetCoeffs[0], w0, alpha);
        break;
        
    case FilterType::LowShelf:
        calculateLowShelf(m_targetCoeffs[0], w0, alpha, A);
        break;
        
    case FilterType::HighShelf:
        calculateHighShelf(m_targetCoeffs[0], w0, alpha, A);
        break;
        
    case FilterType::Peak:
        calculatePeak(m_targetCoeffs[0], w0, alpha, A);
        break;
        
    case FilterType::AllPass:
        calculateAllPass(m_targetCoeffs[0], w0, alpha);
        break;
    }
    
    // Copy to right channel if linked
    if (m_stereoLinked)
    {
        m_targetCoeffs[1] = m_targetCoeffs[0];
    }
    
    // Apply coefficient smoothing
    updateSmoothing();
}

void Filter::calculateLowPass(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept
{
    float b0 = (1.0f - std::cos(w0)) / 2.0f;
    float b1 = 1.0f - std::cos(w0);
    float b2 = b0;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * std::cos(w0);
    float a2 = 1.0f - alpha;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

void Filter::calculateHighPass(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept
{
    float b0 = (1.0f + std::cos(w0)) / 2.0f;
    float b1 = -(1.0f + std::cos(w0));
    float b2 = b0;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * std::cos(w0);
    float a2 = 1.0f - alpha;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

void Filter::calculateBandPass(BiquadCoefficients& coeffs, float w0, float alpha) const noexcept
{
    // Constant skirt gain (peak gain = Q)
    float b0 = alpha;
    float b1 = 0.0f;
    float b2 = -alpha;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * std::cos(w0);
    float a2 = 1.0f - alpha;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

void Filter::calculateNotch(BiquadCoefficients& coeffs, float w0, float alpha) const noexcept
{
    float b0 = 1.0f;
    float b1 = -2.0f * std::cos(w0);
    float b2 = 1.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * std::cos(w0);
    float a2 = 1.0f - alpha;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

void Filter::calculateLowShelf(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept
{
    float sqrtA = std::sqrt(A);
    float aPlus1 = A + 1.0f;
    float aMinus1 = A - 1.0f;
    float cosW0 = std::cos(w0);
    
    float b0 = A * (aPlus1 - aMinus1 * cosW0 + 2.0f * sqrtA * alpha);
    float b1 = 2.0f * A * (aMinus1 - aPlus1 * cosW0);
    float b2 = A * (aPlus1 - aMinus1 * cosW0 - 2.0f * sqrtA * alpha);
    float a0 = aPlus1 + aMinus1 * cosW0 + 2.0f * sqrtA * alpha;
    float a1 = -2.0f * (aMinus1 + aPlus1 * cosW0);
    float a2 = aPlus1 + aMinus1 * cosW0 - 2.0f * sqrtA * alpha;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

void Filter::calculateHighShelf(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept
{
    float sqrtA = std::sqrt(A);
    float aPlus1 = A + 1.0f;
    float aMinus1 = A - 1.0f;
    float cosW0 = std::cos(w0);
    
    float b0 = A * (aPlus1 + aMinus1 * cosW0 + 2.0f * sqrtA * alpha);
    float b1 = -2.0f * A * (aMinus1 + aPlus1 * cosW0);
    float b2 = A * (aPlus1 + aMinus1 * cosW0 - 2.0f * sqrtA * alpha);
    float a0 = aPlus1 - aMinus1 * cosW0 + 2.0f * sqrtA * alpha;
    float a1 = 2.0f * (aMinus1 - aPlus1 * cosW0);
    float a2 = aPlus1 - aMinus1 * cosW0 - 2.0f * sqrtA * alpha;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

void Filter::calculatePeak(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept
{
    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * std::cos(w0);
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * std::cos(w0);
    float a2 = 1.0f - alpha / A;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

void Filter::calculateAllPass(BiquadCoefficients& coeffs, float w0, float alpha) const noexcept
{
    float b0 = 1.0f - alpha;
    float b1 = -2.0f * std::cos(w0);
    float b2 = 1.0f + alpha;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * std::cos(w0);
    float a2 = 1.0f - alpha;
    
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
}

// ============================================================================
// Parameter smoothing
// ============================================================================

void Filter::updateInternal()
{
    bool needsCoefficientUpdate = false;
    
    // Smooth cutoff
    float newCutoff = interpolateParameter(m_currentCutoff, m_targetCutoff, m_smoothingAlpha);
    if (std::abs(newCutoff - m_currentCutoff) > 0.1f)
    {
        m_currentCutoff = newCutoff;
        needsCoefficientUpdate = true;
    }
    
    // Smooth resonance
    float newResonance = interpolateParameter(m_currentResonance, m_targetResonance, m_smoothingAlpha);
    if (std::abs(newResonance - m_currentResonance) > 0.001f)
    {
        m_currentResonance = newResonance;
        needsCoefficientUpdate = true;
    }
    
    // Smooth gain
    float newGain = interpolateParameter(m_currentGain, m_targetGain, m_smoothingAlpha);
    if (std::abs(newGain - m_currentGain) > 0.01f)
    {
        m_currentGain = newGain;
        needsCoefficientUpdate = true;
    }
    
    // Update coefficients if needed
    if (needsCoefficientUpdate || m_needsUpdate)
    {
        updateCoefficientsInternal();
    }
}

void Filter::updateSmoothing()
{
    // Smooth coefficients towards target
    for (int i = 0; i < 2; ++i)
    {
        m_coeffs[i].b0 = interpolateParameter(m_coeffs[i].b0, m_targetCoeffs[i].b0, m_smoothingAlpha);
        m_coeffs[i].b1 = interpolateParameter(m_coeffs[i].b1, m_targetCoeffs[i].b1, m_smoothingAlpha);
        m_coeffs[i].b2 = interpolateParameter(m_coeffs[i].b2, m_targetCoeffs[i].b2, m_smoothingAlpha);
        m_coeffs[i].a1 = interpolateParameter(m_coeffs[i].a1, m_targetCoeffs[i].a1, m_smoothingAlpha);
        m_coeffs[i].a2 = interpolateParameter(m_coeffs[i].a2, m_targetCoeffs[i].a2, m_smoothingAlpha);
    }
}

float Filter::interpolateParameter(float start, float target, float alpha) const noexcept
{
    switch (m_smoothingType)
    {
    case SmoothingType::Linear:
        return start + (target - start) * (1.0f - alpha);
        
    case SmoothingType::Exponential:
        return target + (start - target) * alpha;
        
    case SmoothingType::Cosine:
    {
        float t = 1.0f - alpha;
        float cosine = (1.0f - std::cos(t * PI)) * 0.5f;
        return start + (target - start) * cosine;
    }
        
    default:
        return target;  // No smoothing
    }
}

// ============================================================================
// Saturation
// ============================================================================

float Filter::applySaturation(float sample) const noexcept
{
    switch (m_saturationType)
    {
    case SaturationType::SoftClip:
    {
        // Cubic soft clipper
        if (sample > 1.0f)
            return 2.0f / 3.0f;
        else if (sample < -1.0f)
            return -2.0f / 3.0f;
        else
            return sample - sample * sample * sample / 3.0f;
    }
        
    case SaturationType::HardClip:
        return std::clamp(sample, -1.0f, 1.0f);
        
    case SaturationType::Tanh:
        return std::tanh(sample);
        
    case SaturationType::Asymmetric:
        // Gentle asymmetric saturation
        if (sample > 0.0f)
            return 1.0f - std::exp(-sample);
        else
            return -1.0f + std::exp(sample);
        
    default:
        return sample;
    }
}

// ============================================================================
// State management
// ============================================================================

void Filter::reset()
{
    for (auto& state : m_state)
    {
        state.reset();
    }
    
    // Reset oversampling filters
    std::memset(m_upsampleFilter, 0, sizeof(m_upsampleFilter));
    std::memset(m_downsampleFilter, 0, sizeof(m_downsampleFilter));
}

void Filter::reset(int channel)
{
    if (channel >= 0 && channel < 2)
    {
        m_state[channel].reset();
    }
}

// ============================================================================
// Analysis methods
// ============================================================================

float Filter::getMagnitudeResponse(float frequency) const
{
    // Convert to normalized frequency
    float w = TWO_PI * frequency * m_inverseSampleRate;
    std::complex<float> z = std::exp(std::complex<float>(0.0f, w));
    
    // Evaluate transfer function H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
    std::complex<float> numerator = m_coeffs[0].b0 + m_coeffs[0].b1 / z + m_coeffs[0].b2 / (z * z);
    std::complex<float> denominator = 1.0f + m_coeffs[0].a1 / z + m_coeffs[0].a2 / (z * z);
    std::complex<float> H = numerator / denominator;
    
    return 20.0f * std::log10(std::abs(H));
}

float Filter::getPhaseResponse(float frequency) const
{
    float w = TWO_PI * frequency * m_inverseSampleRate;
    std::complex<float> z = std::exp(std::complex<float>(0.0f, w));
    
    std::complex<float> numerator = m_coeffs[0].b0 + m_coeffs[0].b1 / z + m_coeffs[0].b2 / (z * z);
    std::complex<float> denominator = 1.0f + m_coeffs[0].a1 / z + m_coeffs[0].a2 / (z * z);
    std::complex<float> H = numerator / denominator;
    
    return std::arg(H);
}

float Filter::getGroupDelay(float frequency) const
{
    // Numerical approximation of group delay
    const float delta = 1.0f;  // 1 Hz difference
    
    float phase1 = getPhaseResponse(frequency);
    float phase2 = getPhaseResponse(frequency + delta);
    
    float phaseDiff = phase2 - phase1;
    
    // Unwrap phase difference
    while (phaseDiff > PI) phaseDiff -= TWO_PI;
    while (phaseDiff < -PI) phaseDiff += TWO_PI;
    
    return -phaseDiff / (TWO_PI * delta);
}

bool Filter::isSelfOscillating() const noexcept
{
    return m_selfOscillation && m_currentResonance >= 0.99f;
}

// ============================================================================
// Helper methods
// ============================================================================

void Filter::BiquadCoefficients::normalize() noexcept
{
    // Ensure stability by checking pole radius
    float poleRadius = std::sqrt(a1 * a1 + a2 * a2);
    if (poleRadius > 0.999f)
    {
        // Scale back poles to unit circle
        float scale = 0.999f / poleRadius;
        a1 *= scale;
        a2 *= scale * scale;
    }
}

void Filter::FilterState::reset() noexcept
{
    x1 = x2 = y1 = y2 = 0.0f;
}

void Filter::OversampledBuffer::resize(uint32_t newSize)
{
    if (newSize != size)
    {
        buffer = std::make_unique<float[]>(newSize);
        size = newSize;
    }
}

} // namespace DSP
} // namespace Audio
} // namespace Nomad