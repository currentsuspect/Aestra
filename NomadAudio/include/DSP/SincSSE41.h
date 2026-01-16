// © 2025 Nomad Studios — All Rights Reserved.
// This file is compiled with SSE4.1 intrinsics for older x86 CPUs.
// It is only called when CPUDetection confirms SSE4.1 support (and lacks AVX2).
#pragma once

#include <xmmintrin.h>  // SSE
#include <smmintrin.h>  // SSE4.1

namespace Nomad {
namespace Audio {

/**
 * @brief SSE4.1-optimized dot product for Sinc64 Turbo.
 * 
 * This function MUST ONLY be called if CPUDetection::hasSSE41() returns true
 * and CPUDetection::hasAVX2() returns false.
 * 
 * Uses SSE4.1 _mm_dp_ps for efficient dot products, processing 4 taps at a time.
 */
inline void sincDotProductSSE41(
    const float* coeffs,
    const float* samples,  // Interleaved L/R stereo
    float& sumL, float& sumR)
{
    __m128 vSumL = _mm_setzero_ps();
    __m128 vSumR = _mm_setzero_ps();
    
    // Process 64 taps, 4 at a time
    for (int i = 0; i < 64; i += 4) {
        __m128 vCoeff = _mm_loadu_ps(&coeffs[i]);
        
        // Gather left channel samples (every other sample starting at 0)
        __m128 vL = _mm_set_ps(
            samples[(i + 3) * 2],
            samples[(i + 2) * 2],
            samples[(i + 1) * 2],
            samples[i * 2]
        );
        
        // Gather right channel samples (every other sample starting at 1)
        __m128 vR = _mm_set_ps(
            samples[(i + 3) * 2 + 1],
            samples[(i + 2) * 2 + 1],
            samples[(i + 1) * 2 + 1],
            samples[i * 2 + 1]
        );
        
        // Multiply and accumulate
        vSumL = _mm_add_ps(vSumL, _mm_mul_ps(vL, vCoeff));
        vSumR = _mm_add_ps(vSumR, _mm_mul_ps(vR, vCoeff));
    }
    
    // Horizontal sum using SSE3 hadd or SSE fallback
    // SSE4.1 doesn't add horizontal sum, but we can use shuffle+add
    
    // Sum L: [a, b, c, d] -> a+b+c+d
    __m128 shufL = _mm_shuffle_ps(vSumL, vSumL, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sumsL = _mm_add_ps(vSumL, shufL);
    shufL = _mm_movehl_ps(shufL, sumsL);
    sumsL = _mm_add_ss(sumsL, shufL);
    
    // Sum R: same procedure
    __m128 shufR = _mm_shuffle_ps(vSumR, vSumR, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sumsR = _mm_add_ps(vSumR, shufR);
    shufR = _mm_movehl_ps(shufR, sumsR);
    sumsR = _mm_add_ss(sumsR, shufR);
    
    sumL = _mm_cvtss_f32(sumsL);
    sumR = _mm_cvtss_f32(sumsR);
}

} // namespace Audio
} // namespace Nomad
