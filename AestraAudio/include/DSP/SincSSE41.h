// © 2025 Aestra Studios — All Rights Reserved.
// This file is compiled with SSE4.1 intrinsics for older x86 CPUs.
// It is only called when CPUDetection confirms SSE4.1 support (and lacks AVX2).
#pragma once

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)

#include <smmintrin.h> // SSE4.1
#include <xmmintrin.h> // SSE

namespace Aestra {
namespace Audio {

/**
 * @brief SSE4.1-optimized dot product for Sinc64 Turbo.
 *
 * TASK 2: Phase interpolation — blend between c0 and c1 using alpha.
 * coeff[t] = c0[t] + alpha * (c1[t] - c0[t])
 * Then standard vectorized dot product.
 *
 * This function MUST ONLY be called if CPUDetection::hasSSE41() returns true
 * and CPUDetection::hasAVX2() returns false.
 */
inline void sincDotProductSSE41(const float* c0,
                                const float* c1,
                                float alpha,
                                const float* samples, // Interleaved L/R stereo
                                float& sumL, float& sumR) {
    __m128 vSumL = _mm_setzero_ps();
    __m128 vSumR = _mm_setzero_ps();
    __m128 vAlpha = _mm_set1_ps(alpha);

    // Process 64 taps, 4 at a time
    for (int i = 0; i < 64; i += 4) {
        __m128 vC0 = _mm_loadu_ps(&c0[i]);
        __m128 vC1 = _mm_loadu_ps(&c1[i]);

        // Phase interpolation: coeff = c0 + alpha * (c1 - c0)
        __m128 vCoeff = _mm_add_ps(vC0, _mm_mul_ps(_mm_sub_ps(vC1, vC0), vAlpha));

        // Gather left channel samples (every other sample starting at 0)
        __m128 vL = _mm_set_ps(samples[(i + 3) * 2], samples[(i + 2) * 2], samples[(i + 1) * 2], samples[i * 2]);

        // Gather right channel samples (every other sample starting at 1)
        __m128 vR = _mm_set_ps(samples[(i + 3) * 2 + 1], samples[(i + 2) * 2 + 1], samples[(i + 1) * 2 + 1],
                               samples[i * 2 + 1]);

        // Multiply and accumulate
        vSumL = _mm_add_ps(vSumL, _mm_mul_ps(vL, vCoeff));
        vSumR = _mm_add_ps(vSumR, _mm_mul_ps(vR, vCoeff));
    }

    // Horizontal sum
    __m128 shufL = _mm_shuffle_ps(vSumL, vSumL, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sumsL = _mm_add_ps(vSumL, shufL);
    shufL = _mm_movehl_ps(shufL, sumsL);
    sumsL = _mm_add_ss(sumsL, shufL);

    __m128 shufR = _mm_shuffle_ps(vSumR, vSumR, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sumsR = _mm_add_ps(vSumR, shufR);
    shufR = _mm_movehl_ps(shufR, sumsR);
    sumsR = _mm_add_ss(sumsR, shufR);

    sumL = _mm_cvtss_f32(sumsL);
    sumR = _mm_cvtss_f32(sumsR);
}

} // namespace Audio
} // namespace Aestra

#endif // x86 guard
