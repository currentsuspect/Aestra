// © 2025 Nomad Studios — All Rights Reserved.
// High-quality clip resampling using Sinc64Turbo polyphase filter.
#pragma once

#include "Interpolators.h"
#include <cstdint>
#include <cmath>

namespace Nomad {
namespace Audio {

/**
 * @brief Resampling quality modes for clip playback
 */
enum class ClipResamplingQuality {
    Fast,       // Linear interpolation (low CPU, audible artifacts on pitch shift)
    Standard,   // Cubic Hermite (good balance)
    High        // Sinc64Turbo (mastering quality, highest CPU)
};

/**
 * @brief High-quality clip resampler using polyphase Sinc64
 * 
 * Provides sample-accurate resampling for clip playback with pitch shifting.
 * Uses the optimized Sinc64Turbo polyphase filter bank for mastering-grade quality.
 * 
 * Usage:
 * @code
 *   ClipResampler resampler;
 *   resampler.setQuality(ClipResamplingQuality::High);
 *   
 *   float sample = resampler.getSample(audioData, numFrames, 2, sourcePosition);
 * @endcode
 */
class ClipResampler {
public:
    ClipResampler() = default;
    
    /**
     * @brief Set resampling quality
     */
    void setQuality(ClipResamplingQuality quality) noexcept {
        m_quality = quality;
    }
    
    ClipResamplingQuality getQuality() const noexcept { return m_quality; }
    
    /**
     * @brief Get a resampled sample from interleaved audio data
     * 
     * @param data Interleaved audio data
     * @param numFrames Total frames in source
     * @param numChannels Number of channels (1=mono, 2=stereo)
     * @param position Fractional sample position
     * @param channel Channel to read (0=left, 1=right)
     * @return Interpolated sample value
     */
    inline float getSample(
        const float* data,
        int64_t numFrames,
        uint32_t numChannels,
        double position,
        uint32_t channel) const noexcept
    {
        if (position < 0 || position >= numFrames) {
            return 0.0f;
        }
        
        switch (m_quality) {
            case ClipResamplingQuality::Fast:
                return sampleLinear(data, numFrames, numChannels, position, channel);
                
            case ClipResamplingQuality::Standard:
                return sampleCubic(data, numFrames, numChannels, position, channel);
                
            case ClipResamplingQuality::High:
                return sampleSinc64(data, numFrames, numChannels, position, channel);
        }
        
        return sampleLinear(data, numFrames, numChannels, position, channel);
    }
    
    /**
     * @brief Get stereo sample pair (optimized for stereo clips)
     */
    inline void getSampleStereo(
        const float* data,
        int64_t numFrames,
        double position,
        float& outL, float& outR) const noexcept
    {
        if (position < 0 || position >= numFrames) {
            outL = outR = 0.0f;
            return;
        }
        
        switch (m_quality) {
            case ClipResamplingQuality::Fast:
                sampleLinearStereo(data, numFrames, position, outL, outR);
                break;
                
            case ClipResamplingQuality::Standard:
                sampleCubicStereo(data, numFrames, position, outL, outR);
                break;
                
            case ClipResamplingQuality::High:
                // Sinc64Turbo already handles stereo natively
                Interpolators::Sinc64Turbo::interpolate(data, numFrames, position, outL, outR);
                break;
        }
    }
    
private:
    ClipResamplingQuality m_quality = ClipResamplingQuality::Standard;
    
    // =========================================================================
    // Linear Interpolation (Fast)
    // =========================================================================
    
    inline float sampleLinear(
        const float* data,
        int64_t numFrames,
        uint32_t numChannels,
        double position,
        uint32_t channel) const noexcept
    {
        int64_t idx0 = static_cast<int64_t>(position);
        int64_t idx1 = idx0 + 1;
        
        if (idx1 >= numFrames) idx1 = numFrames - 1;
        
        float frac = static_cast<float>(position - idx0);
        
        float s0 = data[idx0 * numChannels + channel];
        float s1 = data[idx1 * numChannels + channel];
        
        return s0 + frac * (s1 - s0);
    }
    
    inline void sampleLinearStereo(
        const float* data,
        int64_t numFrames,
        double position,
        float& outL, float& outR) const noexcept
    {
        int64_t idx0 = static_cast<int64_t>(position);
        int64_t idx1 = idx0 + 1;
        
        if (idx1 >= numFrames) idx1 = numFrames - 1;
        
        float frac = static_cast<float>(position - idx0);
        
        float l0 = data[idx0 * 2];
        float l1 = data[idx1 * 2];
        float r0 = data[idx0 * 2 + 1];
        float r1 = data[idx1 * 2 + 1];
        
        outL = l0 + frac * (l1 - l0);
        outR = r0 + frac * (r1 - r0);
    }
    
    // =========================================================================
    // Cubic Hermite Interpolation (Standard)
    // =========================================================================
    
    inline float sampleCubic(
        const float* data,
        int64_t numFrames,
        uint32_t numChannels,
        double position,
        uint32_t channel) const noexcept
    {
        int64_t idx = static_cast<int64_t>(position);
        double frac = position - static_cast<double>(idx);
        
        // Get 4 sample points with bounds clamping
        int64_t i0 = (idx > 0) ? idx - 1 : 0;
        int64_t i1 = idx;
        int64_t i2 = (idx + 1 < numFrames) ? idx + 1 : numFrames - 1;
        int64_t i3 = (idx + 2 < numFrames) ? idx + 2 : numFrames - 1;
        
        float s0 = data[i0 * numChannels + channel];
        float s1 = data[i1 * numChannels + channel];
        float s2 = data[i2 * numChannels + channel];
        float s3 = data[i3 * numChannels + channel];
        
        // Catmull-Rom coefficients
        float frac2 = static_cast<float>(frac * frac);
        float frac3 = frac2 * static_cast<float>(frac);
        
        float c0 = -0.5f * frac3 + frac2 - 0.5f * static_cast<float>(frac);
        float c1 = 1.5f * frac3 - 2.5f * frac2 + 1.0f;
        float c2 = -1.5f * frac3 + 2.0f * frac2 + 0.5f * static_cast<float>(frac);
        float c3 = 0.5f * frac3 - 0.5f * frac2;
        
        return s0 * c0 + s1 * c1 + s2 * c2 + s3 * c3;
    }
    
    inline void sampleCubicStereo(
        const float* data,
        int64_t numFrames,
        double position,
        float& outL, float& outR) const noexcept
    {
        // Use the existing CubicInterpolator from Interpolators.h
        Interpolators::CubicInterpolator::interpolate(data, numFrames, position, outL, outR);
    }
    
    // =========================================================================
    // Sinc64 Turbo Interpolation (High Quality)
    // =========================================================================
    
    inline float sampleSinc64(
        const float* data,
        int64_t numFrames,
        uint32_t numChannels,
        double position,
        uint32_t channel) const noexcept
    {
        // For mono or channel extraction, we need to build a stereo view
        // This is less efficient than the stereo path, but maintains quality
        float outL, outR;
        
        if (numChannels >= 2) {
            Interpolators::Sinc64Turbo::interpolate(data, numFrames, position, outL, outR);
            return (channel == 0) ? outL : outR;
        } else {
            // Mono: duplicate to stereo for Sinc64
            // Note: This is a fallback path, stereo is more common
            return sampleCubic(data, numFrames, numChannels, position, channel);
        }
    }
};

} // namespace Audio
} // namespace Nomad
