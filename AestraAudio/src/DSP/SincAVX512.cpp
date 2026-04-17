#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
// © 2025 Aestra Studios — All Rights Reserved.
// This file uses AVX-512 intrinsics.
// It is compiled with /arch:AVX512 (MSVC) or -mavx512f -mavx512dq (GCC/Clang) via CMake.

#include "SincAVX512.h"

#include <immintrin.h>

namespace Aestra {
namespace Audio {

/**
 * @brief AVX-512 optimized dot product for Sinc64 Turbo.
 *
 * TASK 2: Phase interpolation — blend between c0 and c1 using alpha.
 * coeff[t] = c0[t] + alpha * (c1[t] - c0[t])
 * Then standard vectorized dot product with interleaved stereo de-interleaving.
 *
 * Processes 16 taps per iteration (4 iterations for 64 taps).
 * Requires CPUDetection::hasAVX512F().
 */
void sincDotProductAVX512(const float* c0,
                          const float* c1,
                          float alpha,
                          const float* samples, // Interleaved L/R stereo
                          float& sumL, float& sumR) {
    __m512 vSumL = _mm512_setzero_ps();
    __m512 vSumR = _mm512_setzero_ps();
    __m512 vAlpha = _mm512_set1_ps(alpha);

    static const __m512i vIdxL =
        _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16,
                         14, 12, 10, 8, 6, 4, 2, 0);

    static const __m512i vIdxR = _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);

    for (int i = 0; i < 64; i += 16) {
        __m512 vC0 = _mm512_loadu_ps(&c0[i]);
        __m512 vC1 = _mm512_loadu_ps(&c1[i]);

        // Phase interpolation: coeff = c0 + alpha * (c1 - c0)
        __m512 vCoeff = _mm512_fmadd_ps(_mm512_sub_ps(vC1, vC0), vAlpha, vC0);

        // Load 32 floats (16 stereo pairs)
        __m512 vRaw1 = _mm512_loadu_ps(&samples[i * 2]);
        __m512 vRaw2 = _mm512_loadu_ps(&samples[i * 2 + 16]);

        // De-interleave
        __m512 vL = _mm512_permutex2var_ps(vRaw1, vIdxL, vRaw2);
        __m512 vR = _mm512_permutex2var_ps(vRaw1, vIdxR, vRaw2);

        vSumL = _mm512_fmadd_ps(vL, vCoeff, vSumL);
        vSumR = _mm512_fmadd_ps(vR, vCoeff, vSumR);
    }

    sumL = _mm512_reduce_add_ps(vSumL);
    sumR = _mm512_reduce_add_ps(vSumR);
}

/**
 * @brief Reversed coefficient AVX-512 dot product.
 * TASK 2: Phase interpolation + reversed coefficient access.
 */
void sincDotProductAVX512_Reversed(const float* c0,
                                   const float* c1,
                                   float alpha,
                                   const float* samples, float& sumL, float& sumR) {
    __m512 vSumL = _mm512_setzero_ps();
    __m512 vSumR = _mm512_setzero_ps();
    __m512 vAlpha = _mm512_set1_ps(alpha);

    static const __m512i vIdxL = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    static const __m512i vIdxR = _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);

    // Reverse indices for coefficients: 15, 14, ... 0
    static const __m512i vRev = _mm512_set_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    for (int i = 0; i < 64; i += 16) {
        const float* c0Ptr = &c0[64 - 16 - i];
        const float* c1Ptr = &c1[64 - 16 - i];
        __m512 vC0 = _mm512_loadu_ps(c0Ptr);
        __m512 vC1 = _mm512_loadu_ps(c1Ptr);

        // Reverse coefficients
        vC0 = _mm512_permutexvar_ps(vRev, vC0);
        vC1 = _mm512_permutexvar_ps(vRev, vC1);

        // Phase interpolation
        __m512 vCoeff = _mm512_fmadd_ps(_mm512_sub_ps(vC1, vC0), vAlpha, vC0);

        __m512 vRaw1 = _mm512_loadu_ps(&samples[i * 2]);
        __m512 vRaw2 = _mm512_loadu_ps(&samples[i * 2 + 16]);

        __m512 vL = _mm512_permutex2var_ps(vRaw1, vIdxL, vRaw2);
        __m512 vR = _mm512_permutex2var_ps(vRaw1, vIdxR, vRaw2);

        vSumL = _mm512_fmadd_ps(vL, vCoeff, vSumL);
        vSumR = _mm512_fmadd_ps(vR, vCoeff, vSumR);
    }

    sumL = _mm512_reduce_add_ps(vSumL);
    sumR = _mm512_reduce_add_ps(vSumR);
}

} // namespace Audio
} // namespace Aestra

#endif // x86 guard
