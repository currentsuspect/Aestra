// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief AVX-512 Optimized Dot Product
 *
 * Computes the dot product of two float arrays using AVX-512 instructions.
 * Safe to call even if coefficients are not 64-byte aligned (uses unaligned loads).
 *
 * @param a Input array A (samples)
 * @param b Input array B (coefficients)
 * @param n Number of elements
 * @return Dot product result
 */
float dotProductAVX512(const float* a, const float* b, uint32_t n) noexcept;

} // namespace Audio
} // namespace Nomad
