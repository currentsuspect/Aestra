// © 2025 Aestra Studios — All Rights Reserved.
// SIMD-optimized waveform peak generation.
#pragma once

#include <cstdint>
#include <algorithm>
#include <cfloat>
#include "CPUDetection.h"

// Platform-specific SIMD headers
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
    #define AESTRA_WAVEFORM_HAS_SSE 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    #include <arm_neon.h>
    #define AESTRA_WAVEFORM_HAS_NEON 1
#endif

namespace Aestra {
namespace Audio {

/**
 * @brief SIMD-optimized waveform peak reduction
 * 
 * Provides fast min/max computation for waveform display generation.
 * Processes 8 samples per iteration (AVX2) or 4 samples (SSE/NEON).
 */
namespace WaveformSIMD {

#ifdef AESTRA_WAVEFORM_HAS_SSE

/**
 * @brief Compute min/max of float array using AVX2
 * 
 * Processes 8 floats per iteration for maximum throughput.
 */
inline void minMaxAVX2(const float* data, size_t count, float& outMin, float& outMax) noexcept {
    __m256 vMin = _mm256_set1_ps(FLT_MAX);
    __m256 vMax = _mm256_set1_ps(-FLT_MAX);
    
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256 v = _mm256_loadu_ps(&data[i]);
        vMin = _mm256_min_ps(vMin, v);
        vMax = _mm256_max_ps(vMax, v);
    }
    
    // Horizontal reduction
    __m128 minLo = _mm256_castps256_ps128(vMin);
    __m128 minHi = _mm256_extractf128_ps(vMin, 1);
    __m128 minVec = _mm_min_ps(minLo, minHi);
    
    __m128 maxLo = _mm256_castps256_ps128(vMax);
    __m128 maxHi = _mm256_extractf128_ps(vMax, 1);
    __m128 maxVec = _mm_max_ps(maxLo, maxHi);
    
    // Reduce 4 -> 1
    alignas(16) float minArr[4], maxArr[4];
    _mm_store_ps(minArr, minVec);
    _mm_store_ps(maxArr, maxVec);
    
    float min = std::min({minArr[0], minArr[1], minArr[2], minArr[3]});
    float max = std::max({maxArr[0], maxArr[1], maxArr[2], maxArr[3]});
    
    // Scalar remainder
    for (; i < count; ++i) {
        min = std::min(min, data[i]);
        max = std::max(max, data[i]);
    }
    
    outMin = min;
    outMax = max;
}

/**
 * @brief Compute min/max of float array using SSE4.1
 */
inline void minMaxSSE(const float* data, size_t count, float& outMin, float& outMax) noexcept {
    __m128 vMin = _mm_set1_ps(FLT_MAX);
    __m128 vMax = _mm_set1_ps(-FLT_MAX);
    
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m128 v = _mm_loadu_ps(&data[i]);
        vMin = _mm_min_ps(vMin, v);
        vMax = _mm_max_ps(vMax, v);
    }
    
    // Horizontal reduction
    alignas(16) float minArr[4], maxArr[4];
    _mm_store_ps(minArr, vMin);
    _mm_store_ps(maxArr, vMax);
    
    float min = std::min({minArr[0], minArr[1], minArr[2], minArr[3]});
    float max = std::max({maxArr[0], maxArr[1], maxArr[2], maxArr[3]});
    
    // Scalar remainder
    for (; i < count; ++i) {
        min = std::min(min, data[i]);
        max = std::max(max, data[i]);
    }
    
    outMin = min;
    outMax = max;
}

#endif // AESTRA_WAVEFORM_HAS_SSE

#ifdef AESTRA_WAVEFORM_HAS_NEON

inline void minMaxNEON(const float* data, size_t count, float& outMin, float& outMax) noexcept {
    float32x4_t vMin = vdupq_n_f32(FLT_MAX);
    float32x4_t vMax = vdupq_n_f32(-FLT_MAX);
    
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t v = vld1q_f32(&data[i]);
        vMin = vminq_f32(vMin, v);
        vMax = vmaxq_f32(vMax, v);
    }
    
    // Horizontal reduction
    float min = vminvq_f32(vMin);
    float max = vmaxvq_f32(vMax);
    
    // Scalar remainder
    for (; i < count; ++i) {
        min = std::min(min, data[i]);
        max = std::max(max, data[i]);
    }
    
    outMin = min;
    outMax = max;
}

#endif // AESTRA_WAVEFORM_HAS_NEON

/**
 * @brief Scalar fallback for min/max
 */
inline void minMaxScalar(const float* data, size_t count, float& outMin, float& outMax) noexcept {
    float min = FLT_MAX;
    float max = -FLT_MAX;
    
    for (size_t i = 0; i < count; ++i) {
        min = std::min(min, data[i]);
        max = std::max(max, data[i]);
    }
    
    outMin = min;
    outMax = max;
}

/**
 * @brief Auto-dispatched min/max computation
 */
inline void minMax(const float* data, size_t count, float& outMin, float& outMax) noexcept {
    if (count == 0) {
        outMin = outMax = 0.0f;
        return;
    }

#ifdef AESTRA_WAVEFORM_HAS_SSE
    static const auto& cpu = Aestra::Core::CPUDetection::get();
    static const bool useAVX2 = cpu.hasAVX2();
    static const bool useSSE = cpu.hasSSE41();
    
    if (useAVX2) {
        minMaxAVX2(data, count, outMin, outMax);
        return;
    }
    if (useSSE) {
        minMaxSSE(data, count, outMin, outMax);
        return;
    }
#endif

#ifdef AESTRA_WAVEFORM_HAS_NEON
    minMaxNEON(data, count, outMin, outMax);
    return;
#endif

    minMaxScalar(data, count, outMin, outMax);
}

/**
 * @brief Min/max for interleaved multichannel data (single channel extraction)
 * 
 * @param data Interleaved audio data
 * @param numFrames Number of frames
 * @param numChannels Number of channels
 * @param channel Channel to extract
 * @param startFrame Starting frame
 * @param endFrame Ending frame (exclusive)
 */
inline void minMaxChannel(
    const float* data,
    size_t numFrames,
    uint32_t numChannels,
    uint32_t channel,
    size_t startFrame,
    size_t endFrame,
    float& outMin, float& outMax) noexcept
{
    // For interleaved data, fall back to scalar with stride
    // This is harder to SIMD but still faster with bounds checking
    float min = FLT_MAX;
    float max = -FLT_MAX;
    
    for (size_t frame = startFrame; frame < endFrame && frame < numFrames; ++frame) {
        float sample = data[frame * numChannels + channel];
        min = std::min(min, sample);
        max = std::max(max, sample);
    }
    
    outMin = min;
    outMax = max;
}

} // namespace WaveformSIMD
} // namespace Audio
} // namespace Aestra
