// © 2025 Nomad Studios — All Rights Reserved.
// SIMD-optimized mixing operations for real-time audio.
#pragma once

#include <cstddef>
#include <cstdint>
#include "CPUDetection.h"

// Platform-specific SIMD headers
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
    #define NOMAD_HAS_X86_SIMD 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    #include <arm_neon.h>
    #define NOMAD_HAS_NEON 1
#endif

namespace Nomad {
namespace Audio {

/**
 * @brief SIMD-optimized mixing operations for PlaylistMixer
 * 
 * All functions process audio in bulk for maximum throughput.
 * Automatic dispatch to best available SIMD path.
 */
namespace MixerSIMD {

// =============================================================================
// AVX2 Implementations (256-bit, 8 floats per op)
// =============================================================================

#ifdef NOMAD_HAS_X86_SIMD

/**
 * @brief Mix source into destination with gain (AVX2)
 * dst[i] += src[i] * gain
 */
inline void mixWithGainAVX2(const float* src, float* dst, float gain, size_t count) {
    __m256 vGain = _mm256_set1_ps(gain);
    
    size_t i = 0;
    // Process 8 samples at a time
    for (; i + 8 <= count; i += 8) {
        __m256 vSrc = _mm256_loadu_ps(&src[i]);
        __m256 vDst = _mm256_loadu_ps(&dst[i]);
        __m256 vResult = _mm256_fmadd_ps(vSrc, vGain, vDst);
        _mm256_storeu_ps(&dst[i], vResult);
    }
    
    // Scalar remainder
    for (; i < count; ++i) {
        dst[i] += src[i] * gain;
    }
}

/**
 * @brief Mix stereo source into destination with L/R gains (AVX2)
 * dstL[i] += srcL[i] * gainL
 * dstR[i] += srcR[i] * gainR
 */
inline void mixStereoWithGainAVX2(
    const float* srcL, const float* srcR,
    float* dstL, float* dstR,
    float gainL, float gainR,
    size_t count)
{
    __m256 vGainL = _mm256_set1_ps(gainL);
    __m256 vGainR = _mm256_set1_ps(gainR);
    
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        // Left channel
        __m256 vSrcL = _mm256_loadu_ps(&srcL[i]);
        __m256 vDstL = _mm256_loadu_ps(&dstL[i]);
        __m256 vResultL = _mm256_fmadd_ps(vSrcL, vGainL, vDstL);
        _mm256_storeu_ps(&dstL[i], vResultL);
        
        // Right channel
        __m256 vSrcR = _mm256_loadu_ps(&srcR[i]);
        __m256 vDstR = _mm256_loadu_ps(&dstR[i]);
        __m256 vResultR = _mm256_fmadd_ps(vSrcR, vGainR, vDstR);
        _mm256_storeu_ps(&dstR[i], vResultR);
    }
    
    // Scalar remainder
    for (; i < count; ++i) {
        dstL[i] += srcL[i] * gainL;
        dstR[i] += srcR[i] * gainR;
    }
}

#endif // NOMAD_HAS_X86_SIMD

// =============================================================================
// SSE4.1 Implementations (128-bit, 4 floats per op)
// =============================================================================

#ifdef NOMAD_HAS_X86_SIMD

inline void mixWithGainSSE41(const float* src, float* dst, float gain, size_t count) {
    __m128 vGain = _mm_set1_ps(gain);
    
    size_t i = 0;
    // Process 4 samples at a time
    for (; i + 4 <= count; i += 4) {
        __m128 vSrc = _mm_loadu_ps(&src[i]);
        __m128 vDst = _mm_loadu_ps(&dst[i]);
        __m128 vResult = _mm_add_ps(vDst, _mm_mul_ps(vSrc, vGain));
        _mm_storeu_ps(&dst[i], vResult);
    }
    
    // Scalar remainder
    for (; i < count; ++i) {
        dst[i] += src[i] * gain;
    }
}

inline void mixStereoWithGainSSE41(
    const float* srcL, const float* srcR,
    float* dstL, float* dstR,
    float gainL, float gainR,
    size_t count)
{
    __m128 vGainL = _mm_set1_ps(gainL);
    __m128 vGainR = _mm_set1_ps(gainR);
    
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        // Left channel
        __m128 vSrcL = _mm_loadu_ps(&srcL[i]);
        __m128 vDstL = _mm_loadu_ps(&dstL[i]);
        __m128 vResultL = _mm_add_ps(vDstL, _mm_mul_ps(vSrcL, vGainL));
        _mm_storeu_ps(&dstL[i], vResultL);
        
        // Right channel
        __m128 vSrcR = _mm_loadu_ps(&srcR[i]);
        __m128 vDstR = _mm_loadu_ps(&dstR[i]);
        __m128 vResultR = _mm_add_ps(vDstR, _mm_mul_ps(vSrcR, vGainR));
        _mm_storeu_ps(&dstR[i], vResultR);
    }
    
    // Scalar remainder
    for (; i < count; ++i) {
        dstL[i] += srcL[i] * gainL;
        dstR[i] += srcR[i] * gainR;
    }
}

#endif // NOMAD_HAS_X86_SIMD

// =============================================================================
// ARM NEON Implementations (128-bit, 4 floats per op)
// =============================================================================

#ifdef NOMAD_HAS_NEON

inline void mixWithGainNEON(const float* src, float* dst, float gain, size_t count) {
    float32x4_t vGain = vdupq_n_f32(gain);
    
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t vSrc = vld1q_f32(&src[i]);
        float32x4_t vDst = vld1q_f32(&dst[i]);
        float32x4_t vResult = vfmaq_f32(vDst, vSrc, vGain);
        vst1q_f32(&dst[i], vResult);
    }
    
    // Scalar remainder
    for (; i < count; ++i) {
        dst[i] += src[i] * gain;
    }
}

inline void mixStereoWithGainNEON(
    const float* srcL, const float* srcR,
    float* dstL, float* dstR,
    float gainL, float gainR,
    size_t count)
{
    float32x4_t vGainL = vdupq_n_f32(gainL);
    float32x4_t vGainR = vdupq_n_f32(gainR);
    
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        // Left channel
        float32x4_t vSrcL = vld1q_f32(&srcL[i]);
        float32x4_t vDstL = vld1q_f32(&dstL[i]);
        float32x4_t vResultL = vfmaq_f32(vDstL, vSrcL, vGainL);
        vst1q_f32(&dstL[i], vResultL);
        
        // Right channel
        float32x4_t vSrcR = vld1q_f32(&srcR[i]);
        float32x4_t vDstR = vld1q_f32(&dstR[i]);
        float32x4_t vResultR = vfmaq_f32(vDstR, vSrcR, vGainR);
        vst1q_f32(&dstR[i], vResultR);
    }
    
    // Scalar remainder
    for (; i < count; ++i) {
        dstL[i] += srcL[i] * gainL;
        dstR[i] += srcR[i] * gainR;
    }
}

#endif // NOMAD_HAS_NEON

// =============================================================================
// Scalar Fallback
// =============================================================================

inline void mixWithGainScalar(const float* src, float* dst, float gain, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        dst[i] += src[i] * gain;
    }
}

inline void mixStereoWithGainScalar(
    const float* srcL, const float* srcR,
    float* dstL, float* dstR,
    float gainL, float gainR,
    size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        dstL[i] += srcL[i] * gainL;
        dstR[i] += srcR[i] * gainR;
    }
}

// =============================================================================
// Auto-Dispatch Functions
// =============================================================================

/**
 * @brief Mix source into destination with gain (auto-dispatched)
 * 
 * Automatically selects the best SIMD implementation based on CPU.
 */
inline void mixWithGain(const float* src, float* dst, float gain, size_t count) {
#ifdef NOMAD_HAS_X86_SIMD
    static const auto& cpu = Nomad::Core::CPUDetection::get();
    static const bool useAVX2 = cpu.hasAVX2() && cpu.hasFMA();
    static const bool useSSE41 = cpu.hasSSE41();
    
    if (useAVX2) {
        mixWithGainAVX2(src, dst, gain, count);
        return;
    }
    if (useSSE41) {
        mixWithGainSSE41(src, dst, gain, count);
        return;
    }
#endif

#ifdef NOMAD_HAS_NEON
    mixWithGainNEON(src, dst, gain, count);
    return;
#endif

    mixWithGainScalar(src, dst, gain, count);
}

/**
 * @brief Mix stereo into destination with L/R gains (auto-dispatched)
 */
inline void mixStereoWithGain(
    const float* srcL, const float* srcR,
    float* dstL, float* dstR,
    float gainL, float gainR,
    size_t count)
{
#ifdef NOMAD_HAS_X86_SIMD
    static const auto& cpu = Nomad::Core::CPUDetection::get();
    static const bool useAVX2 = cpu.hasAVX2() && cpu.hasFMA();
    static const bool useSSE41 = cpu.hasSSE41();
    
    if (useAVX2) {
        mixStereoWithGainAVX2(srcL, srcR, dstL, dstR, gainL, gainR, count);
        return;
    }
    if (useSSE41) {
        mixStereoWithGainSSE41(srcL, srcR, dstL, dstR, gainL, gainR, count);
        return;
    }
#endif

#ifdef NOMAD_HAS_NEON
    mixStereoWithGainNEON(srcL, srcR, dstL, dstR, gainL, gainR, count);
    return;
#endif

    mixStereoWithGainScalar(srcL, srcR, dstL, dstR, gainL, gainR, count);
}

/**
 * @brief Clear buffer with SIMD (faster than memset for large buffers)
 */
inline void clearBuffer(float* dst, size_t count) {
#ifdef NOMAD_HAS_X86_SIMD
    static const bool useAVX2 = Nomad::Core::CPUDetection::get().hasAVX2();
    
    if (useAVX2) {
        __m256 vZero = _mm256_setzero_ps();
        size_t i = 0;
        for (; i + 8 <= count; i += 8) {
            _mm256_storeu_ps(&dst[i], vZero);
        }
        for (; i < count; ++i) {
            dst[i] = 0.0f;
        }
        return;
    }
#endif

    // Fallback to regular memset
    for (size_t i = 0; i < count; ++i) {
        dst[i] = 0.0f;
    }
}

} // namespace MixerSIMD
} // namespace Audio
} // namespace Nomad
