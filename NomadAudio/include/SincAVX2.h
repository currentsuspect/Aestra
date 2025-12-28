// © 2025 Nomad Studios — All Rights Reserved.
// This file is compiled with /arch:AVX2 to enable SIMD intrinsics.
// It is only called when CPUDetection confirms AVX2 support.
#pragma once

#include <immintrin.h>

namespace Nomad {
namespace Audio {

/**
 * @brief AVX2-optimized dot product for Sinc64 Turbo.
 * 
 * This function MUST ONLY be called if CPUDetection::hasAVX2() returns true.
 * The code is isolated in this TU to prevent VEX-encoded scalar ops from
 * polluting the main codebase.
 */
inline void sincDotProductAVX2(
    const float* coeffs,
    const float* samples, // Interleaved L/R stereo
    float& sumL, float& sumR)
{
    __m256 vSumL = _mm256_setzero_ps();
    __m256 vSumR = _mm256_setzero_ps();
    
    for (int i = 0; i < 64; i += 8) {
        __m256 vCoeff = _mm256_loadu_ps(&coeffs[i]);
        
        __m256 vL = _mm256_set_ps(samples[(i+7)*2], samples[(i+6)*2], samples[(i+5)*2], samples[(i+4)*2],
                                 samples[(i+3)*2], samples[(i+2)*2], samples[(i+1)*2], samples[i*2]);
        __m256 vR = _mm256_set_ps(samples[(i+7)*2+1], samples[(i+6)*2+1], samples[(i+5)*2+1], samples[(i+4)*2+1],
                                 samples[(i+3)*2+1], samples[(i+2)*2+1], samples[(i+1)*2+1], samples[i*2+1]);
                                 
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
} // namespace Nomad
