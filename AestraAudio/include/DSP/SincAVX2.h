// © 2025 Nomad Studios — All Rights Reserved.
// This file is compiled with /arch:AVX2 to enable SIMD intrinsics.
// It is only called when CPUDetection confirms AVX2 support.
#pragma once

#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#define AESTRA_AVX2_TARGET __attribute__((target("avx2,fma")))
#else
#define AESTRA_AVX2_TARGET
#endif

namespace Aestra {
namespace Audio {

/**
 * @brief AVX2-optimized dot product for Sinc64 Turbo.
 * 
 * This function MUST ONLY be called if CPUDetection::hasAVX2() returns true.
 * The code is isolated in this TU to prevent VEX-encoded scalar ops from
 * polluting the main codebase.
 */
AESTRA_AVX2_TARGET
inline void sincDotProductAVX2(
    const float* coeffs,
    const float* samples, // Interleaved L/R stereo
    float& sumL, float& sumR)
{
    __m256 vSumL = _mm256_setzero_ps();
    __m256 vSumR = _mm256_setzero_ps();
    
    for (int i = 0; i < 64; i += 8) {
        __m256 vCoeff = _mm256_loadu_ps(&coeffs[i]);
        
        // Optimize: Load 16 samples (8 stereo pairs) using 2 vector loads
        __m256 ra = _mm256_loadu_ps(&samples[i*2]);
        __m256 rb = _mm256_loadu_ps(&samples[i*2 + 8]);

        // De-interleave L and R channels
        __m256 vMixL = _mm256_shuffle_ps(ra, rb, _MM_SHUFFLE(2,0,2,0));
        __m256 vMixR = _mm256_shuffle_ps(ra, rb, _MM_SHUFFLE(3,1,3,1));
        
        __m256 vL = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(vMixL), _MM_SHUFFLE(3,1,2,0)));
        __m256 vR = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(vMixR), _MM_SHUFFLE(3,1,2,0)));
                                 
        vSumL = _mm256_fmadd_ps(vL, vCoeff, vSumL);
        vSumR = _mm256_fmadd_ps(vR, vCoeff, vSumR);
    }
    
    alignas(32) float hL[8], hR[8];
    _mm256_store_ps(hL, vSumL);
    _mm256_store_ps(hR, vSumR);
    
    sumL = hL[0] + hL[1] + hL[2] + hL[3] + hL[4] + hL[5] + hL[6] + hL[7];
    sumR = hR[0] + hR[1] + hR[2] + hR[3] + hR[4] + hR[5] + hR[6] + hR[7];
}

/**
 * @brief Reversed coefficient AVX2 dot product.
 * Handles the symmetry: data is forward, coeffs are read backwards.
 */
AESTRA_AVX2_TARGET
inline void sincDotProductAVX2_Reversed(
    const float* coeffs,
    const float* samples,
    float& sumL, float& sumR)
{
    __m256 vSumL = _mm256_setzero_ps();
    __m256 vSumR = _mm256_setzero_ps();
    
    // Reverse mask: 7, 6, 5, 4, 3, 2, 1, 0
    const __m256i vRevMask = _mm256_set_epi32(0, 1, 2, 3, 4, 5, 6, 7);

    for (int i = 0; i < 64; i += 8) {
        // Load coeffs from the end backwards
        // i=0 -> want c[63]..c[56]. Load &c[56].
        const float* cPtr = &coeffs[64 - 8 - i];
        __m256 vCoeff = _mm256_loadu_ps(cPtr);
        
        // Reverse the coefficients in the register
        vCoeff = _mm256_permutevar8x32_ps(vCoeff, vRevMask);
        
        // Load pairs (same as forward)
        __m256 ra = _mm256_loadu_ps(&samples[i*2]);
        __m256 rb = _mm256_loadu_ps(&samples[i*2 + 8]);
        
        __m256 vMixL = _mm256_shuffle_ps(ra, rb, _MM_SHUFFLE(2,0,2,0));
        __m256 vMixR = _mm256_shuffle_ps(ra, rb, _MM_SHUFFLE(3,1,3,1));
        
        __m256 vL = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(vMixL), _MM_SHUFFLE(3,1,2,0)));
        __m256 vR = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(vMixR), _MM_SHUFFLE(3,1,2,0)));
        
        vSumL = _mm256_fmadd_ps(vL, vCoeff, vSumL);
        vSumR = _mm256_fmadd_ps(vR, vCoeff, vSumR);
    }
    
    alignas(32) float hL[8], hR[8];
    _mm256_store_ps(hL, vSumL);
    _mm256_store_ps(hR, vSumR);
    
    sumL = hL[0] + hL[1] + hL[2] + hL[3] + hL[4] + hL[5] + hL[6] + hL[7];
    sumR = hR[0] + hR[1] + hR[2] + hR[3] + hR[4] + hR[5] + hR[6] + hR[7];
}

} // namespace Audio
} // namespace Aestra