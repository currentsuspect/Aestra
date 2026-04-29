// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ReverbSIMD — SIMD-optimized core routines for AestraVerb FDN reverb.
#pragma once

#include "CPUDetection.h"

#include <array>
#include <cmath>
#include <cstdint>

#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define AESTRA_REVERB_HAS_AVX2 1
#define AESTRA_REVERB_HAS_SSE 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define AESTRA_AVX2_TARGET __attribute__((target("avx2,fma")))
#else
#define AESTRA_AVX2_TARGET
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>
#define AESTRA_REVERB_HAS_NEON 1
#endif

namespace Aestra {
namespace Audio {
namespace DSP {
namespace ReverbSIMD {

static constexpr size_t kFDNLineCount = 8;
static constexpr size_t kEarlyTapCount = 12;

// Test-only hooks: set to true to force specific paths for benchmarking
extern bool g_forceScalarFallback;
extern bool g_forceLinearInterpolation;

inline float sanitizeFeedbackValue(float value) noexcept {
    if (value != value || value <= -32.0f || value >= 32.0f ||
        (value > -1.0e-20f && value < 1.0e-20f)) {
        return 0.0f;
    }
    return value;
}

// ============================================================================
// Quality improvement: Cubic Hermite interpolation for delay line reads.
// Much better high-frequency preservation than linear. Uses 4 neighbouring
// samples to compute a smooth curve. SIMD-ready formulation.
// ============================================================================
inline float cubicHermite(float y0, float y1, float y2, float y3, float t) noexcept {
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// ============================================================================
// AVX2 — 8-wide FDN feedback + output mixing (256-bit)
// ============================================================================
#ifdef AESTRA_REVERB_HAS_AVX2

/**
 * @brief Process one sample of the 8-line FDN matrix with AVX2.
 *
 * Vectorizes: Householder mean, damping, low-damping, feedback write,
 * and wet output accumulation.
 *
 * @param lineOut    8 delay-line outputs (read)
 * @param delayedL   Left input for injection
 * @param delayedR   Right input for injection
 * @param dampingState  8 damping states (read/write)
 * @param lowDampState  8 low-damp states (read/write)
 * @param delayLines    8 delay-line buffers (write)
 * @param delayPos      8 write positions (read/write)
 * @param delaySizes    8 buffer sizes
 * @param feedbackGains 8 feedback gains
 * @param injectL       8 injection coeffs L
 * @param injectR       8 injection coeffs R
 * @param outputL       8 output coeffs L
 * @param outputR       8 output coeffs R
 * @param dampingCoeff  single damping coefficient
 * @param lowDampCoeff  single low-damping coefficient
 * @param wetL          output wet left accumulator (read/write)
 * @param wetR          output wet right accumulator (read/write)
 */
AESTRA_AVX2_TARGET
inline void processFDNSampleAVX2(
    const float* lineOut,
    float delayedL,
    float delayedR,
    float* dampingState,
    float* lowDampState,
    float* const* delayLines,
    int* delayPos,
    const int* delayMasks,
    const float* feedbackGains,
    const float* injectL,
    const float* injectR,
    const float* outputL,
    const float* outputR,
    float dampingCoeff,
    float lowDampCoeff,
    float lowDampAmount,
    float& wetL,
    float& wetR) noexcept {

    // Load 8-wide vectors
    __m256 vLineOut = _mm256_loadu_ps(lineOut);
    __m256 vDampState = _mm256_loadu_ps(dampingState);
    __m256 vLowDampState = _mm256_loadu_ps(lowDampState);
    __m256 vFeedbackGains = _mm256_loadu_ps(feedbackGains);
    __m256 vInjectL = _mm256_loadu_ps(injectL);
    __m256 vInjectR = _mm256_loadu_ps(injectR);
    __m256 vOutputL = _mm256_loadu_ps(outputL);
    __m256 vOutputR = _mm256_loadu_ps(outputR);
    __m256 vDampCoeff = _mm256_set1_ps(dampingCoeff);
    __m256 vLowDampCoeff = _mm256_set1_ps(lowDampCoeff);

    // Householder mean: sum * (2/8) = sum * 0.25f
    __m256 vSum = _mm256_add_ps(vLineOut, _mm256_permute2f128_ps(vLineOut, vLineOut, 1));
    vSum = _mm256_hadd_ps(vSum, vSum);
    vSum = _mm256_hadd_ps(vSum, vSum);
    __m256 vMean = _mm256_mul_ps(vSum, _mm256_set1_ps(0.25f));

    // matrixOut = lineOut - mean
    __m256 vMatrixOut = _mm256_sub_ps(vLineOut, vMean);

    // dampingState += dampingCoeff * (matrixOut - dampingState)
    __m256 vNewDampState = _mm256_fmadd_ps(vDampCoeff, _mm256_sub_ps(vMatrixOut, vDampState), vDampState);
    _mm256_storeu_ps(dampingState, vNewDampState);

    // injected = (delayedL * injectL + delayedR * injectR) * 0.115f
    __m256 vInjected = _mm256_mul_ps(
        _mm256_fmadd_ps(vInjectL, _mm256_set1_ps(delayedL), _mm256_mul_ps(vInjectR, _mm256_set1_ps(delayedR))),
        _mm256_set1_ps(0.115f));

    // feedback = dampingState * feedbackGains
    __m256 vFeedback = _mm256_mul_ps(vNewDampState, vFeedbackGains);

    // lowDampState += (feedback - lowDampState) * lowDampCoeff
    __m256 vNewLowDampState = _mm256_fmadd_ps(vLowDampCoeff, _mm256_sub_ps(vFeedback, vLowDampState), vLowDampState);
    _mm256_storeu_ps(lowDampState, vNewLowDampState);

    // Mode-aware low trim. Keep enough low/low-mid energy for body without letting sub buildup dominate.
    __m256 vFinalFeedback = _mm256_fnmadd_ps(vNewLowDampState, _mm256_set1_ps(lowDampAmount), vFeedback);

    // delayLines[line][pos] = injected + feedback
    __m256 vWrite = _mm256_add_ps(vInjected, vFinalFeedback);

    // Scalar writeback to delay lines (random access, can't vectorize stores across buffers)
    alignas(32) float writeVals[8];
    _mm256_store_ps(writeVals, vWrite);
    for (size_t line = 0; line < kFDNLineCount; ++line) {
        delayLines[line][delayPos[line]] = sanitizeFeedbackValue(writeVals[line]);
        delayPos[line] = (delayPos[line] + 1) & delayMasks[line];
    }

    // wetL += dot(lineOut, outputL), wetR += dot(lineOut, outputR)
    __m256 vDotL = _mm256_mul_ps(vLineOut, vOutputL);
    __m256 vDotR = _mm256_mul_ps(vLineOut, vOutputR);

    // Horizontal sum both dot products simultaneously using hadd
    __m256 vSumLR = _mm256_hadd_ps(vDotL, vDotR);
    vSumLR = _mm256_hadd_ps(vSumLR, vSumLR);
    alignas(32) float sumLR[8];
    _mm256_store_ps(sumLR, vSumLR);
    wetL += sumLR[0] + sumLR[4];
    wetR += sumLR[1] + sumLR[5];
}

/**
 * @brief Update 8 LFO quadrature oscillators with AVX2.
 *
 * Computes next sin/cos for all 8 lines in parallel.
 */
AESTRA_AVX2_TARGET
inline void updateLFOsAVX2(
    float* lfoSin, float* lfoCos,
    float* lfoSin2, float* lfoCos2,
    const float* lfoSinInc, const float* lfoCosInc,
    const float* lfoSinInc2, const float* lfoCosInc2) noexcept {

    __m256 vSin = _mm256_loadu_ps(lfoSin);
    __m256 vCos = _mm256_loadu_ps(lfoCos);
    __m256 vSinInc = _mm256_loadu_ps(lfoSinInc);
    __m256 vCosInc = _mm256_loadu_ps(lfoCosInc);

    __m256 vSin2 = _mm256_loadu_ps(lfoSin2);
    __m256 vCos2 = _mm256_loadu_ps(lfoCos2);
    __m256 vSinInc2 = _mm256_loadu_ps(lfoSinInc2);
    __m256 vCosInc2 = _mm256_loadu_ps(lfoCosInc2);

    // nextSin = sin*cosInc + cos*sinInc
    __m256 vNextSin = _mm256_fmadd_ps(vSin, vCosInc, _mm256_mul_ps(vCos, vSinInc));
    __m256 vNextCos = _mm256_fmsub_ps(vCos, vCosInc, _mm256_mul_ps(vSin, vSinInc));

    __m256 vNextSin2 = _mm256_fmadd_ps(vSin2, vCosInc2, _mm256_mul_ps(vCos2, vSinInc2));
    __m256 vNextCos2 = _mm256_fmsub_ps(vCos2, vCosInc2, _mm256_mul_ps(vSin2, vSinInc2));

    _mm256_storeu_ps(lfoSin, vNextSin);
    _mm256_storeu_ps(lfoCos, vNextCos);
    _mm256_storeu_ps(lfoSin2, vNextSin2);
    _mm256_storeu_ps(lfoCos2, vNextCos2);
}

/**
 * @brief Normalize 8 quadrature oscillators with AVX2.
 * Fast vectorized renormalization using approximate reciprocal sqrt.
 */
AESTRA_AVX2_TARGET
inline void normalizeLFOsAVX2(float* lfoSin, float* lfoCos) noexcept {
    __m256 vSin = _mm256_loadu_ps(lfoSin);
    __m256 vCos = _mm256_loadu_ps(lfoCos);
    __m256 vMag2 = _mm256_fmadd_ps(vSin, vSin, _mm256_mul_ps(vCos, vCos));

    // mag2 in (0.25, 4) ? use rsqrt : reset to (0, 1)
    __m256 vMask = _mm256_and_ps(
        _mm256_cmp_ps(vMag2, _mm256_set1_ps(0.25f), _CMP_GT_OQ),
        _mm256_cmp_ps(vMag2, _mm256_set1_ps(4.0f), _CMP_LT_OQ));

    __m256 vScale = _mm256_blendv_ps(_mm256_set1_ps(1.0f),
                                     _mm256_rsqrt_ps(vMag2), vMask);
    __m256 vZero = _mm256_set1_ps(0.0f);
    __m256 vOne = _mm256_set1_ps(1.0f);

    _mm256_storeu_ps(lfoSin, _mm256_blendv_ps(vZero, _mm256_mul_ps(vSin, vScale), vMask));
    _mm256_storeu_ps(lfoCos, _mm256_blendv_ps(vOne,  _mm256_mul_ps(vCos, vScale), vMask));
}

#endif // AESTRA_REVERB_HAS_AVX2

// ============================================================================
// SSE4.1 — 4-wide FDN feedback (process 8 lines as two 4-wide blocks)
// ============================================================================
#ifdef AESTRA_REVERB_HAS_SSE

inline void processFDNSampleSSE(
    const float* lineOut,
    float delayedL,
    float delayedR,
    float* dampingState,
    float* lowDampState,
    float* const* delayLines,
    int* delayPos,
    const int* delayMasks,
    const float* feedbackGains,
    const float* injectL,
    const float* injectR,
    const float* outputL,
    const float* outputR,
    float dampingCoeff,
    float lowDampCoeff,
    float lowDampAmount,
    float& wetL,
    float& wetR) noexcept {

    const __m128 vLineOut0 = _mm_loadu_ps(lineOut);
    const __m128 vLineOut1 = _mm_loadu_ps(lineOut + 4);
    __m128 vSum = _mm_add_ps(vLineOut0, vLineOut1);
    vSum = _mm_add_ps(vSum, _mm_movehl_ps(vSum, vSum));
    vSum = _mm_add_ps(vSum, _mm_shuffle_ps(vSum, vSum, _MM_SHUFFLE(1, 1, 1, 1)));
    const __m128 vHouseholderMean = _mm_set1_ps(_mm_cvtss_f32(vSum) * 0.25f);

    for (size_t block = 0; block < 2; ++block) {
        size_t b = block * 4;
        __m128 vLineOut = _mm_loadu_ps(&lineOut[b]);
        __m128 vDampState = _mm_loadu_ps(&dampingState[b]);
        __m128 vLowDampState = _mm_loadu_ps(&lowDampState[b]);
        __m128 vFeedbackGains = _mm_loadu_ps(&feedbackGains[b]);
        __m128 vInjectL = _mm_loadu_ps(&injectL[b]);
        __m128 vInjectR = _mm_loadu_ps(&injectR[b]);
        __m128 vOutputL = _mm_loadu_ps(&outputL[b]);
        __m128 vOutputR = _mm_loadu_ps(&outputR[b]);

        __m128 vMatrixOut = _mm_sub_ps(vLineOut, vHouseholderMean);

        __m128 vDampCoeff = _mm_set1_ps(dampingCoeff);
        __m128 vNewDampState = _mm_add_ps(_mm_mul_ps(vDampCoeff, _mm_sub_ps(vMatrixOut, vDampState)), vDampState);
        _mm_storeu_ps(&dampingState[b], vNewDampState);

        __m128 vInjected = _mm_mul_ps(
            _mm_add_ps(_mm_mul_ps(vInjectL, _mm_set1_ps(delayedL)), _mm_mul_ps(vInjectR, _mm_set1_ps(delayedR))),
            _mm_set1_ps(0.115f));

        __m128 vFeedback = _mm_mul_ps(vNewDampState, vFeedbackGains);
        __m128 vLowDampCoeff = _mm_set1_ps(lowDampCoeff);
        __m128 vNewLowDampState = _mm_add_ps(_mm_mul_ps(vLowDampCoeff, _mm_sub_ps(vFeedback, vLowDampState)), vLowDampState);
        _mm_storeu_ps(&lowDampState[b], vNewLowDampState);

        __m128 vFinalFeedback = _mm_sub_ps(vFeedback, _mm_mul_ps(vNewLowDampState, _mm_set1_ps(lowDampAmount)));
        __m128 vWrite = _mm_add_ps(vInjected, vFinalFeedback);

        alignas(16) float writeVals[4];
        _mm_store_ps(writeVals, vWrite);
        for (size_t i = 0; i < 4; ++i) {
            size_t line = b + i;
            delayLines[line][delayPos[line]] = sanitizeFeedbackValue(writeVals[i]);
            delayPos[line] = (delayPos[line] + 1) & delayMasks[line];
        }

        __m128 vDotL = _mm_mul_ps(vLineOut, vOutputL);
        __m128 vDotR = _mm_mul_ps(vLineOut, vOutputR);
        // Horizontal sum (SSE2)
        vDotL = _mm_add_ps(vDotL, _mm_movehl_ps(vDotL, vDotL));
        vDotL = _mm_add_ps(vDotL, _mm_shuffle_ps(vDotL, vDotL, _MM_SHUFFLE(1, 1, 1, 1)));
        vDotR = _mm_add_ps(vDotR, _mm_movehl_ps(vDotR, vDotR));
        vDotR = _mm_add_ps(vDotR, _mm_shuffle_ps(vDotR, vDotR, _MM_SHUFFLE(1, 1, 1, 1)));
        wetL += _mm_cvtss_f32(vDotL);
        wetR += _mm_cvtss_f32(vDotR);
    }
}

inline void updateLFOsSSE(float* lfoSin, float* lfoCos,
                          const float* lfoSinInc, const float* lfoCosInc) noexcept {
    __m128 vSin = _mm_loadu_ps(lfoSin);
    __m128 vCos = _mm_loadu_ps(lfoCos);
    __m128 vSinInc = _mm_loadu_ps(lfoSinInc);
    __m128 vCosInc = _mm_loadu_ps(lfoCosInc);
    __m128 vNextSin = _mm_add_ps(_mm_mul_ps(vSin, vCosInc), _mm_mul_ps(vCos, vSinInc));
    __m128 vNextCos = _mm_sub_ps(_mm_mul_ps(vCos, vCosInc), _mm_mul_ps(vSin, vSinInc));
    _mm_storeu_ps(lfoSin, vNextSin);
    _mm_storeu_ps(lfoCos, vNextCos);
}

#endif // AESTRA_REVERB_HAS_SSE

// ============================================================================
// NEON — 4-wide FDN feedback (ARM)
// ============================================================================
#ifdef AESTRA_REVERB_HAS_NEON

inline void processFDNSampleNEON(
    const float* lineOut,
    float delayedL,
    float delayedR,
    float* dampingState,
    float* lowDampState,
    float* const* delayLines,
    int* delayPos,
    const int* delayMasks,
    const float* feedbackGains,
    const float* injectL,
    const float* injectR,
    const float* outputL,
    const float* outputR,
    float dampingCoeff,
    float lowDampCoeff,
    float lowDampAmount,
    float& wetL,
    float& wetR) noexcept {

    for (size_t block = 0; block < 2; ++block) {
        size_t b = block * 4;
        float32x4_t vLineOut = vld1q_f32(&lineOut[b]);
        float32x4_t vDampState = vld1q_f32(&dampingState[b]);
        float32x4_t vLowDampState = vld1q_f32(&lowDampState[b]);
        float32x4_t vFeedbackGains = vld1q_f32(&feedbackGains[b]);
        float32x4_t vInjectL = vld1q_f32(&injectL[b]);
        float32x4_t vInjectR = vld1q_f32(&injectR[b]);
        float32x4_t vOutputL = vld1q_f32(&outputL[b]);
        float32x4_t vOutputR = vld1q_f32(&outputR[b]);

        // Horizontal sum for mean
        float32x4_t vSum = vaddq_f32(vLineOut, vrev64q_f32(vLineOut));
        float sumL = vgetq_lane_f32(vSum, 0) + vgetq_lane_f32(vSum, 2);
        float sumR = vgetq_lane_f32(vSum, 1) + vgetq_lane_f32(vSum, 3);
        float mean = (sumL + sumR) * 0.25f;
        float32x4_t vMean = vdupq_n_f32(mean);

        float32x4_t vMatrixOut = vsubq_f32(vLineOut, vMean);

        float32x4_t vDampCoeff = vdupq_n_f32(dampingCoeff);
        float32x4_t vNewDampState = vmlaq_f32(vDampState, vDampCoeff, vsubq_f32(vMatrixOut, vDampState));
        vst1q_f32(&dampingState[b], vNewDampState);

        float32x4_t vInjected = vmulq_f32(
            vaddq_f32(vmulq_f32(vInjectL, vdupq_n_f32(delayedL)), vmulq_f32(vInjectR, vdupq_n_f32(delayedR))),
            vdupq_n_f32(0.115f));

        float32x4_t vFeedback = vmulq_f32(vNewDampState, vFeedbackGains);
        float32x4_t vLowDampCoeff = vdupq_n_f32(lowDampCoeff);
        float32x4_t vNewLowDampState = vmlaq_f32(vLowDampState, vLowDampCoeff, vsubq_f32(vFeedback, vLowDampState));
        vst1q_f32(&lowDampState[b], vNewLowDampState);

        float32x4_t vFinalFeedback = vsubq_f32(vFeedback, vmulq_f32(vNewLowDampState, vdupq_n_f32(lowDampAmount)));
        float32x4_t vWrite = vaddq_f32(vInjected, vFinalFeedback);

        alignas(16) float writeVals[4];
        vst1q_f32(writeVals, vWrite);
        for (size_t i = 0; i < 4; ++i) {
            size_t line = b + i;
            delayLines[line][delayPos[line]] = sanitizeFeedbackValue(writeVals[i]);
            delayPos[line] = (delayPos[line] + 1) & delayMasks[line];
        }

        float32x4_t vDotL = vmulq_f32(vLineOut, vOutputL);
        float32x4_t vDotR = vmulq_f32(vLineOut, vOutputR);
        wetL += vgetq_lane_f32(vDotL, 0) + vgetq_lane_f32(vDotL, 1) + vgetq_lane_f32(vDotL, 2) + vgetq_lane_f32(vDotL, 3);
        wetR += vgetq_lane_f32(vDotR, 0) + vgetq_lane_f32(vDotR, 1) + vgetq_lane_f32(vDotR, 2) + vgetq_lane_f32(vDotR, 3);
    }
}

#endif // AESTRA_REVERB_HAS_NEON

// ============================================================================
// ============================================================================
// SSE Stereo Diffuser (2-wide L+R per stage)
// ============================================================================

#ifdef AESTRA_REVERB_HAS_SSE

inline void processDiffusersSSE(float& left, float& right,
                                float diffusionG,
                                float* const* diffuserBuffersL,
                                float* const* diffuserBuffersR,
                                int* diffuserPos,
                                const int* diffuserMasks,
                                const int* diffuserLengths,
                                size_t stageCount) noexcept {
    if (diffusionG <= 0.0001f) return;
    const __m128 vG = _mm_set1_ps(diffusionG);
    for (size_t stage = 0; stage < stageCount; ++stage) {
        const float g = diffusionG;

        float* bufL = diffuserBuffersL[stage];
        float* bufR = diffuserBuffersR[stage];
        const int mask = diffuserMasks[stage];
        const int len = diffuserLengths[stage];
        int p = diffuserPos[stage] & mask;
        const int readP = (p - len) & mask;

        __m128 vIn = _mm_set_ps(0.0f, 0.0f, right, left);
        __m128 vG = _mm_set1_ps(g);
        __m128 vDelayed = _mm_set_ps(0.0f, 0.0f, bufR[readP], bufL[readP]);

        __m128 vY = _mm_sub_ps(vDelayed, _mm_mul_ps(vG, vIn));
        __m128 vWrite = _mm_add_ps(vIn, _mm_mul_ps(vG, vY));

        alignas(16) float writeVals[4];
        _mm_store_ps(writeVals, vWrite);
        bufL[p] = writeVals[0];
        bufR[p] = writeVals[1];

        alignas(16) float yVals[4];
        _mm_store_ps(yVals, vY);
        left = yVals[0];
        right = yVals[1];

        diffuserPos[stage] = (p + 1) & mask;
    }
}

#endif // AESTRA_REVERB_HAS_SSE

// ============================================================================
// Auto-dispatch wrappers
// ============================================================================

inline void processFDNSample(
    const float* lineOut,
    float delayedL,
    float delayedR,
    float* dampingState,
    float* lowDampState,
    float* const* delayLines,
    int* delayPos,
    const int* delayMasks,
    const float* feedbackGains,
    const float* injectL,
    const float* injectR,
    const float* outputL,
    const float* outputR,
    float dampingCoeff,
    float lowDampCoeff,
    float lowDampAmount,
    float& wetL,
    float& wetR) noexcept {

    if (!g_forceScalarFallback) {
#ifdef AESTRA_REVERB_HAS_AVX2
        static const bool useAVX2 = Aestra::Core::CPUDetection::get().hasAVX2();
        if (useAVX2) {
            processFDNSampleAVX2(lineOut, delayedL, delayedR, dampingState, lowDampState,
                                 delayLines, delayPos, delayMasks, feedbackGains,
                                 injectL, injectR, outputL, outputR,
                                 dampingCoeff, lowDampCoeff, lowDampAmount, wetL, wetR);
            return;
        }
#endif

#ifdef AESTRA_REVERB_HAS_SSE
        static const bool useSSE = Aestra::Core::CPUDetection::get().hasSSE41();
        if (useSSE) {
            processFDNSampleSSE(lineOut, delayedL, delayedR, dampingState, lowDampState,
                                delayLines, delayPos, delayMasks, feedbackGains,
                                injectL, injectR, outputL, outputR,
                                dampingCoeff, lowDampCoeff, lowDampAmount, wetL, wetR);
            return;
        }
#endif

#ifdef AESTRA_REVERB_HAS_NEON
        processFDNSampleNEON(lineOut, delayedL, delayedR, dampingState, lowDampState,
                             delayLines, delayPos, delayMasks, feedbackGains,
                             injectL, injectR, outputL, outputR,
                             dampingCoeff, lowDampCoeff, lowDampAmount, wetL, wetR);
        return;
#endif
    }

    // Scalar fallback (identical to original logic)
    float sum = 0.0f;
    for (size_t line = 0; line < kFDNLineCount; ++line) sum += lineOut[line];
    const float householderMean = sum * (2.0f / static_cast<float>(kFDNLineCount));

    for (size_t line = 0; line < kFDNLineCount; ++line) {
        float matrixOut = lineOut[line] - householderMean;
        dampingState[line] += dampingCoeff * (matrixOut - dampingState[line]);
        const float injected = (delayedL * injectL[line] + delayedR * injectR[line]) * 0.115f;
        float feedback = dampingState[line] * feedbackGains[line];
        lowDampState[line] += (feedback - lowDampState[line]) * lowDampCoeff;
        feedback -= lowDampState[line] * lowDampAmount;
        delayLines[line][delayPos[line]] = sanitizeFeedbackValue(injected + feedback);
        delayPos[line] = (delayPos[line] + 1) & delayMasks[line];
        wetL += lineOut[line] * outputL[line];
        wetR += lineOut[line] * outputR[line];
    }
}

} // namespace ReverbSIMD
} // namespace DSP
} // namespace Audio
} // namespace Aestra
