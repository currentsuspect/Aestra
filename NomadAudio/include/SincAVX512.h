// © 2025 Nomad Studios — All Rights Reserved.
// This header declares AVX-512 optimized functions.
// The implementation is in src/SincAVX512.cpp to allow per-file compiler flags.
#pragma once

#include "CPUDetection.h"

namespace Nomad {
namespace Audio {

// These functions are only valid if CPUDetection::hasAVX512F() is true.

void sincDotProductAVX512(
    const float* coeffs,
    const float* samples,
    float& sumL, float& sumR);

void sincDotProductAVX512_Reversed(
    const float* coeffs,
    const float* samples,
    float& sumL, float& sumR);

} // namespace Audio
} // namespace Nomad
