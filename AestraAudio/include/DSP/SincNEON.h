// © 2025 Aestra Studios — All Rights Reserved.
// This file provides ARM NEON optimized Sinc interpolation for Apple Silicon and ARM Linux.
// It is only compiled and used on ARM platforms.
#pragma once

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>

namespace Aestra {
namespace Audio {

/**
 * @brief ARM NEON-optimized dot product for Sinc64 Turbo.
 *
 * This function is used on ARM platforms (Apple Silicon, ARM Linux, iOS).
 * Uses NEON SIMD instructions for efficient 4-wide vector operations.
 */
inline void sincDotProductNEON(const float* coeffs,
                               const float* samples, // Interleaved L/R stereo
                               float& sumL, float& sumR) {
    float32x4_t vSumL = vdupq_n_f32(0.0f);
    float32x4_t vSumR = vdupq_n_f32(0.0f);

    // Process 64 taps, 4 at a time
    for (int i = 0; i < 64; i += 4) {
        float32x4_t vCoeff = vld1q_f32(&coeffs[i]);

        // Gather left channel samples
        float32x4_t vL = {samples[i * 2], samples[(i + 1) * 2], samples[(i + 2) * 2], samples[(i + 3) * 2]};

        // Gather right channel samples
        float32x4_t vR = {samples[i * 2 + 1], samples[(i + 1) * 2 + 1], samples[(i + 2) * 2 + 1],
                          samples[(i + 3) * 2 + 1]};

        // Fused multiply-accumulate (FMA) - very fast on ARM
        vSumL = vfmaq_f32(vSumL, vL, vCoeff);
        vSumR = vfmaq_f32(vSumR, vR, vCoeff);
    }

    // Horizontal sum - ARM64 has efficient vaddvq_f32
#if defined(__aarch64__)
    sumL = vaddvq_f32(vSumL);
    sumR = vaddvq_f32(vSumR);
#else
    // ARM32 fallback: pairwise add
    float32x2_t pL = vadd_f32(vget_low_f32(vSumL), vget_high_f32(vSumL));
    pL = vpadd_f32(pL, pL);
    sumL = vget_lane_f32(pL, 0);

    float32x2_t pR = vadd_f32(vget_low_f32(vSumR), vget_high_f32(vSumR));
    pR = vpadd_f32(pR, pR);
    sumR = vget_lane_f32(pR, 0);
#endif
}

} // namespace Audio
} // namespace Aestra

#else
// Stub for non-ARM platforms (will never be called)
namespace Aestra {
namespace Audio {

inline void sincDotProductNEON(const float* /*coeffs*/, const float* /*samples*/, float& sumL, float& sumR) {
    sumL = sumR = 0.0f;
}

} // namespace Audio
} // namespace Aestra

#endif // ARM NEON check
