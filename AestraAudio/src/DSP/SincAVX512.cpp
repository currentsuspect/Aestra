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
 * Processes 16 taps per iteration (4 iterations for 64 taps).
 * Requires CPUDetection::hasAVX512F().
 */
void sincDotProductAVX512(const float* coeffs,
                          const float* samples, // Interleaved L/R stereo
                          float& sumL, float& sumR) {
    __m512 vSumL = _mm512_setzero_ps();
    __m512 vSumR = _mm512_setzero_ps();

    // Indices for de-interleaving:
    // We load 32 floats (16 pairs).
    // Evens go to L, Odds go to R.
    // _mm512_permutex2var_ps(a, idx, b) selects from concatenated pair (a, b).
    // Note: The intrinsic signature is _mm512_permutex2var_ps(a, idx, b).

    // Pattern to extract Even elements (0, 2, 4...) from two registers
    // Indices 0..15 select from 'a', 16..31 select from 'b'.
    // We want 0 from a, 2 from a ... 14 from a, 0 from b (index 16), 2 from b (index 18)...

    static const __m512i vIdxL =
        _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, // From second register (indices 16-31 map to 0-15 of b)
                         14, 12, 10, 8, 6, 4, 2, 0       // From first register
        );

    static const __m512i vIdxR = _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);

    for (int i = 0; i < 64; i += 16) {
        __m512 vCoeff = _mm512_loadu_ps(&coeffs[i]);

        // Load 32 floats (16 stereo pairs) -> 128 bytes
        // Address is 64-byte aligned ideally, but using loadu to be safe
        __m512 vRaw1 = _mm512_loadu_ps(&samples[i * 2]);
        __m512 vRaw2 = _mm512_loadu_ps(&samples[i * 2 + 16]); // Next 16 floats

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
 */
void sincDotProductAVX512_Reversed(const float* coeffs, const float* samples, float& sumL, float& sumR) {
    __m512 vSumL = _mm512_setzero_ps();
    __m512 vSumR = _mm512_setzero_ps();

    // De-interleave indices (same as above)
    static const __m512i vIdxL = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    static const __m512i vIdxR = _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);

    // Reverse indices for coefficients: 15, 14, ... 0
    static const __m512i vRev = _mm512_set_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    for (int i = 0; i < 64; i += 16) {
        // Load coeffs from the end backwards
        // i=0 -> want c[63]..c[48]. Load &c[48].
        const float* cPtr = &coeffs[64 - 16 - i];
        __m512 vCoeff = _mm512_loadu_ps(cPtr);

        // Reverse coefficients
        vCoeff = _mm512_permutexvar_ps(vRev, vCoeff);

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
