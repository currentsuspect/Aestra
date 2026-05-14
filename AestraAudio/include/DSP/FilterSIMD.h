// © 2025 Aestra Studios — All Rights Reserved.
// SIMD-optimized biquad filter processing for stereo audio.
#pragma once

#include "CPUDetection.h"

#include <cstdint>

// Platform-specific SIMD headers
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <emmintrin.h> // SSE2
#include <xmmintrin.h> // SSE
#define AESTRA_FILTER_HAS_SSE 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>
#define AESTRA_FILTER_HAS_NEON 1
#endif

namespace Aestra {
namespace Audio {
namespace DSP {

/**
 * @brief SIMD-optimized biquad filter state for stereo processing
 *
 * Stores L+R state interleaved for SIMD efficiency.
 */
struct alignas(16) StereoFilterState {
    // State variables: [L_x1, R_x1, L_x2, R_x2] for SSE __m128
    float x1[2] = {0.0f, 0.0f}; // [L, R]
    float x2[2] = {0.0f, 0.0f}; // [L, R]

    void reset() noexcept {
        x1[0] = x1[1] = 0.0f;
        x2[0] = x2[1] = 0.0f;
    }
};

/**
 * @brief SIMD-optimized biquad coefficient storage
 *
 * Coefficients duplicated for stereo SIMD operations.
 */
struct alignas(16) StereoFilterCoeffs {
    float b0[2]; // [L, R] - can be same if linked
    float b1[2];
    float b2[2];
    float a1[2];
    float a2[2];

    // Set from single-channel coefficients (linked stereo)
    void setLinked(float _b0, float _b1, float _b2, float _a1, float _a2) noexcept {
        b0[0] = b0[1] = _b0;
        b1[0] = b1[1] = _b1;
        b2[0] = b2[1] = _b2;
        a1[0] = a1[1] = _a1;
        a2[0] = a2[1] = _a2;
    }
};

/**
 * @brief SIMD-optimized stereo biquad processing
 *
 * Processes both L and R channels in parallel using SIMD.
 * Direct Form II Transposed implementation.
 */
namespace FilterSIMD {

#ifdef AESTRA_FILTER_HAS_SSE

/**
 * @brief Process stereo biquad filter block using SSE
 *
 * Processes L and R channels in parallel using 4-wide SSE.
 * State update is still sequential per-sample, but L+R are parallel.
 */
inline void processBlockStereoSSE(float* left, float* right, uint32_t numSamples, StereoFilterState& state,
                                  const StereoFilterCoeffs& coeffs) noexcept {
    // Load coefficients via 64-bit load (avoids _mm_set_ps scalar reconstruction)
    __m128 vB0 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(&coeffs.b0[0])));
    __m128 vB1 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(&coeffs.b1[0])));
    __m128 vB2 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(&coeffs.b2[0])));
    __m128 vA1 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(&coeffs.a1[0])));
    __m128 vA2 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(&coeffs.a2[0])));

    // Load state
    __m128 vX1 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(state.x1)));
    __m128 vX2 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(state.x2)));

    for (uint32_t i = 0; i < numSamples; ++i) {
        // Pack L+R into a 64-bit temp for single-load efficiency
        float lr[2] = { left[i], right[i] };
        __m128 vIn = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(lr)));

        // Direct Form II Transposed:
        // output = b0 * input + x1
        // x1 = b1 * input - a1 * output + x2
        // x2 = b2 * input - a2 * output

        __m128 vOut = _mm_add_ps(_mm_mul_ps(vB0, vIn), vX1);

        __m128 vNewX1 = _mm_add_ps(_mm_sub_ps(_mm_mul_ps(vB1, vIn), _mm_mul_ps(vA1, vOut)), vX2);

        __m128 vNewX2 = _mm_sub_ps(_mm_mul_ps(vB2, vIn), _mm_mul_ps(vA2, vOut));

        vX1 = vNewX1;
        vX2 = vNewX2;

        // Extract and store L+R from low 64 bits of vOut
        alignas(16) float out[4];
        _mm_store_ps(out, vOut);
        left[i] = out[0];
        right[i] = out[1];
    }

    // Store state back
    _mm_storel_epi64(reinterpret_cast<__m128i*>(state.x1), _mm_castps_si128(vX1));
    _mm_storel_epi64(reinterpret_cast<__m128i*>(state.x2), _mm_castps_si128(vX2));
}

#endif // AESTRA_FILTER_HAS_SSE

#ifdef AESTRA_FILTER_HAS_NEON

/**
 * @brief Process stereo biquad filter block using NEON
 */
inline void processBlockStereoNEON(float* left, float* right, uint32_t numSamples, StereoFilterState& state,
                                   const StereoFilterCoeffs& coeffs) noexcept {
    // Load coefficients
    float32x2_t vB0 = vld1_f32(coeffs.b0);
    float32x2_t vB1 = vld1_f32(coeffs.b1);
    float32x2_t vB2 = vld1_f32(coeffs.b2);
    float32x2_t vA1 = vld1_f32(coeffs.a1);
    float32x2_t vA2 = vld1_f32(coeffs.a2);

    // Load state
    float32x2_t vX1 = vld1_f32(state.x1);
    float32x2_t vX2 = vld1_f32(state.x2);

    for (uint32_t i = 0; i < numSamples; ++i) {
        // Load L+R input
        float inLR[2] = {left[i], right[i]};
        float32x2_t vIn = vld1_f32(inLR);

        // Direct Form II Transposed
        float32x2_t vOut = vfma_f32(vX1, vB0, vIn); // b0*in + x1

        float32x2_t vNewX1 = vadd_f32(vsub_f32(vmul_f32(vB1, vIn), vmul_f32(vA1, vOut)), vX2);

        float32x2_t vNewX2 = vsub_f32(vmul_f32(vB2, vIn), vmul_f32(vA2, vOut));

        vX1 = vNewX1;
        vX2 = vNewX2;

        // Store output
        float outLR[2];
        vst1_f32(outLR, vOut);
        left[i] = outLR[0];
        right[i] = outLR[1];
    }

    // Store state back
    vst1_f32(state.x1, vX1);
    vst1_f32(state.x2, vX2);
}

#endif // AESTRA_FILTER_HAS_NEON

/**
 * @brief Scalar fallback for stereo biquad processing
 */
inline void processBlockStereoScalar(float* left, float* right, uint32_t numSamples, StereoFilterState& state,
                                     const StereoFilterCoeffs& coeffs) noexcept {
    for (uint32_t i = 0; i < numSamples; ++i) {
        // Left channel
        float outL = coeffs.b0[0] * left[i] + state.x1[0];
        state.x1[0] = coeffs.b1[0] * left[i] - coeffs.a1[0] * outL + state.x2[0];
        state.x2[0] = coeffs.b2[0] * left[i] - coeffs.a2[0] * outL;
        left[i] = outL;

        // Right channel
        float outR = coeffs.b0[1] * right[i] + state.x1[1];
        state.x1[1] = coeffs.b1[1] * right[i] - coeffs.a1[1] * outR + state.x2[1];
        state.x2[1] = coeffs.b2[1] * right[i] - coeffs.a2[1] * outR;
        right[i] = outR;
    }
}

/**
 * @brief Auto-dispatched stereo biquad block processing
 */
inline void processBlockStereo(float* left, float* right, uint32_t numSamples, StereoFilterState& state,
                               const StereoFilterCoeffs& coeffs) noexcept {
#ifdef AESTRA_FILTER_HAS_SSE
    static const bool useSSE = Aestra::Core::CPUDetection::get().hasSSE41();
    if (useSSE) {
        processBlockStereoSSE(left, right, numSamples, state, coeffs);
        return;
    }
#endif

#ifdef AESTRA_FILTER_HAS_NEON
    processBlockStereoNEON(left, right, numSamples, state, coeffs);
    return;
#endif

    processBlockStereoScalar(left, right, numSamples, state, coeffs);
}

} // namespace FilterSIMD
} // namespace DSP
} // namespace Audio
} // namespace Aestra
