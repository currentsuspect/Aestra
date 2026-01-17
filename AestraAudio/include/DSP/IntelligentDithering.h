// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Aestra {
namespace Audio {

/**
 * @brief Bit depth modes for dithering output
 */
enum class DitheringMode {
    CD_16bit,       // 16-bit CD quality (1 LSB = 1/32768)
    Pro_24bit,      // 24-bit professional (1 LSB = 1/8388608)
    Float_32bit     // 32-bit float internal (no dithering needed, passthrough)
};

/**
 * @brief Noise shaping filter coefficients based on psychoacoustic masking
 * 
 * Uses 2nd-order IIR filter tuned to approximate Zwicker masking curves.
 * Attenuates dither noise in the 2-5kHz range where hearing is most sensitive.
 */
struct NoiseShaper {
    // Filter state
    float z1L = 0.0f, z2L = 0.0f;
    float z1R = 0.0f, z2R = 0.0f;
    
    // F-weighted noise shaping coefficients (optimized for 44.1/48kHz)
    // These attenuate mid-frequencies where ear is most sensitive
    static constexpr float a1 = -1.5f;
    static constexpr float a2 = 0.56f;
    static constexpr float b0 = 1.0f;
    static constexpr float b1 = -1.0f;
    static constexpr float b2 = 0.0f;
    
    /**
     * @brief Apply noise shaping to error signal
     * 
     * @param errorL Left channel quantization error
     * @param errorR Right channel quantization error
     * @param shapedL Output shaped left error
     * @param shapedR Output shaped right error
     */
    inline void process(float errorL, float errorR, float& shapedL, float& shapedR) noexcept {
        // 2nd order IIR: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
        shapedL = b0 * errorL + b1 * z1L + b2 * z2L - a1 * z1L - a2 * z2L;
        shapedR = b0 * errorR + b1 * z1R + b2 * z2R - a1 * z1R - a2 * z2R;
        
        // Update state
        z2L = z1L; z1L = errorL;
        z2R = z1R; z1R = errorR;
    }
    
    void reset() noexcept {
        z1L = z2L = z1R = z2R = 0.0f;
    }
};

/**
 * @brief Content analyzer for adaptive dithering
 * 
 * Tracks signal RMS to determine when dithering is necessary.
 * Dithering is applied only when signal level is above threshold
 * where quantization artifacts would be audible.
 */
struct ContentAnalyzer {
    // Moving average for RMS estimation
    float rmsL = 0.0f;
    float rmsR = 0.0f;
    
    // Smoothing coefficient (exponential moving average)
    static constexpr float kAlpha = 0.001f;  // ~10ms at 48kHz
    
    // Threshold below which dithering is skipped (signal too quiet)
    static constexpr float kMinThreshold = 1e-6f;  // ~-120dBFS
    
    // Threshold above which full dithering is applied
    static constexpr float kMaxThreshold = 1e-4f;  // ~-80dBFS
    
    /**
     * @brief Update RMS estimate and return dither gain factor
     * 
     * @param sampleL Current left sample
     * @param sampleR Current right sample
     * @return float Dither gain [0, 1] based on content energy
     */
    inline float update(float sampleL, float sampleR) noexcept {
        // Update RMS estimate
        float powerL = sampleL * sampleL;
        float powerR = sampleR * sampleR;
        
        rmsL = rmsL + kAlpha * (powerL - rmsL);
        rmsR = rmsR + kAlpha * (powerR - rmsR);
        
        float maxRms = std::max(rmsL, rmsR);
        
        // Content-aware gain: ramp from 0 to 1 between thresholds
        if (maxRms < kMinThreshold) {
            return 0.0f;  // Too quiet, skip dithering
        } else if (maxRms > kMaxThreshold) {
            return 1.0f;  // Full dithering needed
        } else {
            // Linear interpolation
            float t = (maxRms - kMinThreshold) / (kMaxThreshold - kMinThreshold);
            return t;
        }
    }
    
    void reset() noexcept {
        rmsL = rmsR = 0.0f;
    }
};

/**
 * @brief Intelligent dithering with noise shaping and content awareness
 * 
 * Combines:
 * - TPDF (Triangular Probability Density Function) dither for optimal linearization
 * - Noise shaping based on psychoacoustic masking curves
 * - Content-aware analysis to avoid unnecessary dithering on silent passages
 * 
 * Usage:
 * @code
 *   IntelligentDitherer ditherer(DitheringMode::CD_16bit);
 *   
 *   // In audio callback:
 *   for (int i = 0; i < numFrames; ++i) {
 *       float l = buffer[i * 2];
 *       float r = buffer[i * 2 + 1];
 *       ditherer.process(l, r, buffer[i * 2], buffer[i * 2 + 1]);
 *   }
 * @endcode
 */
class IntelligentDitherer {
public:
    explicit IntelligentDitherer(DitheringMode mode = DitheringMode::CD_16bit)
        : m_mode(mode)
    {
        updateLSB();
    }
    
    void setMode(DitheringMode mode) noexcept {
        m_mode = mode;
        updateLSB();
    }
    
    DitheringMode getMode() const noexcept { return m_mode; }
    
    /**
     * @brief Enable/disable noise shaping
     */
    void setNoiseShapingEnabled(bool enabled) noexcept { m_noiseShapingEnabled = enabled; }
    bool isNoiseShapingEnabled() const noexcept { return m_noiseShapingEnabled; }
    
    /**
     * @brief Enable/disable content-aware dithering
     */
    void setContentAwareEnabled(bool enabled) noexcept { m_contentAwareEnabled = enabled; }
    bool isContentAwareEnabled() const noexcept { return m_contentAwareEnabled; }
    
    /**
     * @brief Process a stereo sample pair with intelligent dithering
     * 
     * @param inL Input left sample [-1, 1]
     * @param inR Input right sample [-1, 1]
     * @param outL Output dithered + quantized left sample
     * @param outR Output dithered + quantized right sample
     */
    inline void process(float inL, float inR, float& outL, float& outR) noexcept {
        // 32-bit float mode: passthrough (no quantization)
        if (m_mode == DitheringMode::Float_32bit) {
            outL = inL;
            outR = inR;
            return;
        }
        
        // Content-aware gain factor
        float ditherGain = 1.0f;
        if (m_contentAwareEnabled) {
            ditherGain = m_analyzer.update(inL, inR);
        }
        
        // Generate TPDF dither
        float ditherL = generateTPDF() * m_lsb * ditherGain;
        float ditherR = generateTPDF() * m_lsb * ditherGain;
        
        // Apply noise shaping to previous quantization error
        if (m_noiseShapingEnabled) {
            float shapedL, shapedR;
            m_shaper.process(m_errorL, m_errorR, shapedL, shapedR);
            ditherL -= shapedL * m_lsb;
            ditherR -= shapedR * m_lsb;
        }
        
        // Add dither and quantize
        float ditheredL = inL + ditherL;
        float ditheredR = inR + ditherR;
        
        // Quantize to target bit depth
        float quantizedL = quantize(ditheredL);
        float quantizedR = quantize(ditheredR);
        
        // Store quantization error for noise shaping
        m_errorL = ditheredL - quantizedL;
        m_errorR = ditheredR - quantizedR;
        
        outL = quantizedL;
        outR = quantizedR;
    }
    
    /**
     * @brief Process a block of interleaved stereo samples
     * 
     * @param buffer Interleaved stereo buffer [L0, R0, L1, R1, ...]
     * @param numFrames Number of stereo frames
     */
    void processBlock(float* buffer, uint32_t numFrames) noexcept {
        for (uint32_t i = 0; i < numFrames; ++i) {
            float l = buffer[i * 2];
            float r = buffer[i * 2 + 1];
            process(l, r, buffer[i * 2], buffer[i * 2 + 1]);
        }
    }
    
    /**
     * @brief Reset internal state (call when seeking or switching sources)
     */
    void reset() noexcept {
        m_shaper.reset();
        m_analyzer.reset();
        m_errorL = m_errorR = 0.0f;
        m_rngState = 0x12345678;
    }
    
private:
    DitheringMode m_mode;
    float m_lsb = 1.0f / 32768.0f;  // LSB magnitude for quantization
    float m_levels = 32768.0f;       // Number of quantization levels
    
    bool m_noiseShapingEnabled = true;
    bool m_contentAwareEnabled = true;
    
    NoiseShaper m_shaper;
    ContentAnalyzer m_analyzer;
    
    float m_errorL = 0.0f;
    float m_errorR = 0.0f;
    
    uint32_t m_rngState = 0x12345678;  // LCG state
    
    void updateLSB() noexcept {
        switch (m_mode) {
            case DitheringMode::CD_16bit:
                m_levels = 32768.0f;
                m_lsb = 1.0f / m_levels;
                break;
            case DitheringMode::Pro_24bit:
                m_levels = 8388608.0f;
                m_lsb = 1.0f / m_levels;
                break;
            case DitheringMode::Float_32bit:
                m_levels = 1.0f;
                m_lsb = 0.0f;  // No dithering
                break;
        }
    }
    
    /**
     * @brief Generate TPDF dither noise in range [-1, 1]
     */
    inline float generateTPDF() noexcept {
        // Fast LCG
        m_rngState = m_rngState * 1664525 + 1013904223;
        float r1 = static_cast<float>(m_rngState) / 4294967296.0f;
        
        m_rngState = m_rngState * 1664525 + 1013904223;
        float r2 = static_cast<float>(m_rngState) / 4294967296.0f;
        
        return r1 - r2;  // TPDF: triangular distribution
    }
    
    /**
     * @brief Quantize sample to target bit depth
     */
    inline float quantize(float sample) const noexcept {
        // Clamp to valid range
        sample = std::clamp(sample, -1.0f, 1.0f - m_lsb);
        
        // Quantize: round to nearest level
        float scaled = sample * m_levels;
        float quantized = std::floor(scaled + 0.5f);
        
        return quantized / m_levels;
    }
};

} // namespace Audio
} // namespace Aestra
