// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../include/SampleRateConverterAVX512.h"

// Ensure we compile this function with AVX-512 flags regardless of global build settings
// GCC/Clang specific attribute
#if defined(__GNUC__) || defined(__clang__)
    #define AVX512_TARGET __attribute__((target("avx512f,avx512dq")))
#else
    #define AVX512_TARGET
#endif

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

namespace Nomad {
namespace Audio {

AVX512_TARGET
float dotProductAVX512(const float* a, const float* b, uint32_t n) noexcept {
#if defined(__AVX512F__) && defined(__AVX512DQ__)
    __m512 sum = _mm512_setzero_ps();
    uint32_t i = 0;

    // Process 32 floats at a time (2x unrolling)
    for (; i + 32 <= n; i += 32) {
        __m512 va1 = _mm512_loadu_ps(a + i);
        __m512 vb1 = _mm512_loadu_ps(b + i); // Use unaligned load for safety
        sum = _mm512_fmadd_ps(va1, vb1, sum);

        __m512 va2 = _mm512_loadu_ps(a + i + 16);
        __m512 vb2 = _mm512_loadu_ps(b + i + 16);
        sum = _mm512_fmadd_ps(va2, vb2, sum);
    }

    // Process remaining 16-block chunks
    for (; i + 16 <= n; i += 16) {
        __m512 va = _mm512_loadu_ps(a + i);
        __m512 vb = _mm512_loadu_ps(b + i);
        sum = _mm512_fmadd_ps(va, vb, sum);
    }

    // Reduction
    float result = _mm512_reduce_add_ps(sum);

    // Handle leftovers
    for (; i < n; ++i) {
        result += a[i] * b[i];
    }

    return result;
#else
    // Fallback if compiler doesn't support intrinsics even with attributes
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
#endif
}

} // namespace Audio
} // namespace Nomad
