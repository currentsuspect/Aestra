// ============================================================================
// © 2025 Nomad Studios — Professional Audio DSP Filter Library
// 
// Features:
// - 8 filter types with proper analog modeling
// - Oversampling (2x/4x) for anti-aliasing
// - Smooth parameter interpolation
// - Drive/saturation stage
// - Zero-delay feedback (ZDF) for stability
// - Stereo processing support
// - Modulation inputs
// ============================================================================

#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <memory>

namespace Nomad {
namespace Audio {
namespace DSP {

// Constants
static constexpr float PI = 3.14159265358979323846f;
static constexpr float TWO_PI = 6.28318530717958647692f;
static constexpr float ONE_OVER_SQRT2 = 0.70710678118654752440f;
static constexpr float NYQUIST_MARGIN = 0.475f;  // Safe nyquist margin
static constexpr float DB_MIN = -96.0f;
static constexpr float DB_MAX = 24.0f;

/**
 * @brief Filter types with analog-style responses
 */
enum class FilterType {
    LowPass = 0,     // 12dB/octave resonant low-pass
    HighPass,        // 12dB/octave resonant high-pass
    BandPass,        // 12dB/octave band-pass
    Notch,           // Notch/band-reject filter
    LowShelf,        // Low-frequency shelving
    HighShelf,       // High-frequency shelving
    Peak,            // Peaking EQ (bell curve)
    AllPass,         // Phase shift all-pass
    
    Count
};

/**
 * @brief Filter slope/order options
 */
enum class FilterSlope {
    Slope12dB = 0,
    Slope24dB,
    Slope48dB
};

/**
 * @brief Oversampling factor for anti-aliasing
 */
enum class OversamplingFactor {
    None = 1,
    TwoX = 2,
    FourX = 4
};

/**
 * @brief Saturation types for drive stage
 */
enum class SaturationType {
    None = 0,
    SoftClip,
    HardClip,
    Tanh,
    Asymmetric
};

/**
 * @brief Parameter smoothing types
 */
enum class SmoothingType {
    Linear = 0,
    Exponential,
    Cosine
};

/**
 * @brief Professional-grade biquad filter for audio processing
 * 
 * Features:
 * - 8 filter types with proper analog modeling
 * - Zero-delay feedback topology for stability
 * - Parameter smoothing with multiple interpolation curves
 * - Oversampling for anti-aliasing at high frequencies/resonance
 * - Built-in drive/saturation stage
 * - Stereo operation with optional linking
 * - Modulation inputs for cutoff and resonance
 * - Sample-rate independent behavior
 * - Self-oscillation capability
 * 
 * Based on:
 * - RBJ Audio EQ Cookbook formulas
 * - Zavalishin's Vadim's VA filters
 * - Andy Simper's Cytomic SVF
 */
class Filter {
public:
    /**
     * @brief Construct filter with sample rate
     * @param sampleRate Audio sample rate in Hz
     */
    explicit Filter(float sampleRate);
    
    ~Filter() = default;
    
    // =========================================================================
    // Core processing
    // =========================================================================
    
    /**
     * @brief Process single mono sample
     * @param input Input sample
     * @return Filtered output sample
     */
    float process(float input);
    
    /**
     * @brief Process stereo pair
     * @param left Left channel input
     * @param right Right channel input
     * @return Stereo pair output
     */
    std::pair<float, float> processStereo(float left, float right);
    
    /**
     * @brief Process block of samples (optimized)
     * @param samples Pointer to sample buffer
     * @param numSamples Number of samples to process
     */
    void processBlock(float* samples, uint32_t numSamples);
    
    /**
     * @brief Process block of stereo samples
     * @param left Left channel buffer
     * @param right Right channel buffer
     * @param numSamples Number of stereo pairs
     */
    void processBlockStereo(float* left, float* right, uint32_t numSamples);
    
    // =========================================================================
    // Parameter setters with smoothing
    // =========================================================================
    
    /**
     * @brief Set filter type with optional transition smoothing
     * @param type New filter type
     * @param smooth Enable transition smoothing
     */
    void setType(FilterType type, bool smooth = true);
    
    /**
     * @brief Set cutoff frequency with smoothing
     * @param frequency Cutoff frequency in Hz (20 - Nyquist)
     * @param modulate Optional modulation value (-1 to 1)
     */
    void setCutoff(float frequency, float modulate = 0.0f);
    
    /**
     * @brief Set resonance (Q factor)
     * @param resonance Resonance amount (0.1 - 10.0, default 0.707)
     * @param modulate Optional modulation value (-1 to 1)
     */
    void setResonance(float resonance, float modulate = 0.0f);
    
    /**
     * @brief Set gain for shelf and peak filters
     * @param gain Gain in dB (-24 to +24 dB)
     */
    void setGain(float gain);
    
    /**
     * @brief Set filter slope (order)
     * @param slope Filter slope (12/24/48 dB per octave)
     */
    void setSlope(FilterSlope slope);
    
    /**
     * @brief Set drive amount with saturation
     * @param drive Drive amount (0.0 - 1.0)
     * @param saturation Saturation type
     */
    void setDrive(float drive, SaturationType saturation = SaturationType::SoftClip);
    
    /**
     * @brief Set oversampling factor for anti-aliasing
     * @param factor Oversampling factor (1x, 2x, 4x)
     */
    void setOversampling(OversamplingFactor factor);
    
    /**
     * @brief Set parameter smoothing time
     * @param time Smoothing time in milliseconds
     * @param type Smoothing interpolation type
     */
    void setSmoothing(float timeMs, SmoothingType type = SmoothingType::Exponential);
    
    /**
     * @brief Enable/disable stereo linking
     * @param linked True to link left/right channels
     */
    void setStereoLinked(bool linked);
    
    /**
     * @brief Enable/disable self-oscillation mode
     * @param enabled True to enable self-oscillation
     */
    void setSelfOscillation(bool enabled);
    
    // =========================================================================
    // Parameter getters
    // =========================================================================
    
    FilterType getType() const noexcept { return m_type; }
    float getCutoff() const noexcept { return m_targetCutoff; }
    float getResonance() const noexcept { return m_targetResonance; }
    float getGain() const noexcept { return m_targetGain; }
    float getDrive() const noexcept { return m_drive; }
    FilterSlope getSlope() const noexcept { return m_slope; }
    
    /**
     * @brief Get current sample rate
     */
    float getSampleRate() const noexcept { return m_sampleRate; }
    
    /**
     * @brief Get oversampling factor
     */
    OversamplingFactor getOversampling() const noexcept { return m_oversampling; }
    
    /**
     * @brief Check if filter is self-oscillating
     */
    bool isSelfOscillating() const noexcept;
    
    // =========================================================================
    // State management
    // =========================================================================
    
    /**
     * @brief Reset filter state (clears memory)
     */
    void reset();
    
    /**
     * @brief Reset filter state for specific channel
     * @param channel Channel index (0=left, 1=right)
     */
    void reset(int channel);
    
    /**
     * @brief Prepare filter for new sample rate
     * @param sampleRate New sample rate
     */
    void prepare(float sampleRate);
    
    /**
     * @brief Force update coefficients (bypass smoothing)
     */
    void updateCoefficients();
    
    // =========================================================================
    // Analysis
    // =========================================================================
    
    /**
     * @brief Calculate frequency response magnitude
     * @param frequency Frequency to analyze
     * @return Magnitude in dB
     */
    float getMagnitudeResponse(float frequency) const;
    
    /**
     * @brief Calculate frequency response phase
     * @param frequency Frequency to analyze
     * @return Phase in radians
     */
    float getPhaseResponse(float frequency) const;
    
    /**
     * @brief Get group delay at specific frequency
     * @param frequency Frequency to analyze
     * @return Group delay in seconds
     */
    float getGroupDelay(float frequency) const;
    
private:
    // =========================================================================
    // Internal structures
    // =========================================================================
    
    struct BiquadCoefficients {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        
        void normalize() noexcept;
    };
    
    struct FilterState {
        float x1 = 0.0f, x2 = 0.0f;  // Input history
        float y1 = 0.0f, y2 = 0.0f;  // Output history
        
        void reset() noexcept;
    };
    
    struct OversampledBuffer {
        std::unique_ptr<float[]> buffer;
        uint32_t size = 0;
        
        void resize(uint32_t newSize);
    };
    
    // =========================================================================
    // Private methods
    // =========================================================================
    
    void updateInternal();
    void updateCoefficientsInternal();
    void updateSmoothing();
    
    float applySaturation(float sample) const noexcept;
    float interpolateParameter(float start, float target, float alpha) const noexcept;
    
    void processSample(float& sample, FilterState& state, const BiquadCoefficients& coeffs);
    void processOversampled(float& sample, FilterState& state);
    
    // Calculate coefficients for different filter types
    void calculateLowPass(BiquadCoefficients& coeffs, float w0, float alpha, float A = 1.0f) const noexcept;
    void calculateHighPass(BiquadCoefficients& coeffs, float w0, float alpha, float A = 1.0f) const noexcept;
    void calculateBandPass(BiquadCoefficients& coeffs, float w0, float alpha) const noexcept;
    void calculateNotch(BiquadCoefficients& coeffs, float w0, float alpha) const noexcept;
    void calculateLowShelf(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept;
    void calculateHighShelf(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept;
    void calculatePeak(BiquadCoefficients& coeffs, float w0, float alpha, float A) const noexcept;
    void calculateAllPass(BiquadCoefficients& coeffs, float w0, float alpha) const noexcept;
    
    // Oversampling methods
    void upsample(const float* input, float* output, uint32_t numSamples) noexcept;
    void downsample(const float* input, float* output, uint32_t numSamples) noexcept;
    
    // =========================================================================
    // Member variables
    // =========================================================================
    
    // Core parameters
    float m_sampleRate = 44100.0f;
    FilterType m_type = FilterType::LowPass;
    FilterSlope m_slope = FilterSlope::Slope12dB;
    OversamplingFactor m_oversampling = OversamplingFactor::None;
    
    // Target parameters
    float m_targetCutoff = 1000.0f;
    float m_targetResonance = ONE_OVER_SQRT2;
    float m_targetGain = 0.0f;
    float m_drive = 0.0f;
    
    // Current (smoothed) parameters
    float m_currentCutoff = 1000.0f;
    float m_currentResonance = ONE_OVER_SQRT2;
    float m_currentGain = 0.0f;
    
    // Modulation
    float m_cutoffMod = 0.0f;
    float m_resonanceMod = 0.0f;
    
    // Coefficients
    BiquadCoefficients m_coeffs[2];  // For stereo
    BiquadCoefficients m_targetCoeffs[2];
    
    // State
    FilterState m_state[2];  // Stereo states
    bool m_stereoLinked = true;
    bool m_selfOscillation = false;
    
    // Smoothing
    float m_smoothingTimeMs = 10.0f;
    SmoothingType m_smoothingType = SmoothingType::Exponential;
    float m_smoothingAlpha = 0.99f;
    
    // Saturation
    SaturationType m_saturationType = SaturationType::None;
    
    // Oversampling buffers
    OversampledBuffer m_oversampleBuffer;
    float m_upsampleFilter[4] = {0.0f};
    float m_downsampleFilter[4] = {0.0f};
    
    // Pre-calculated values
    float m_nyquist = 0.0f;
    float m_inverseSampleRate = 0.0f;
    
    // Flags
    bool m_needsUpdate = true;
    bool m_parametersChanged = false;
};

} // namespace DSP
} // namespace Audio
} // namespace Nomad