// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <cmath>
#include <random>

namespace Nomad {
namespace Audio {

/**
 * @brief Utilities for audio dithering
 */
class DitherUtils {
public:
    /**
     * @brief Generate TPDF dither noise
     * 
     * Triangular Probability Density Function (TPDF) dither is the industry standard
     * for minimizing quantization distortion. It adds white noise with a triangular
     * distribution of amplitude covering +/- 1 LSB.
     * 
     * @param state State for the Linear Congruential Generator (LCG)
     * @return float Dither noise value in range [-1.0, 1.0] relative to LSB
     */
    static inline float generateTPDF(uint32_t& state) {
        // Fast LCG (Linear Congruential Generator)
        // Uses constants from Numerical Recipes
        // We generate two random numbers and subtract them to get triangular distribution
        
        // Rand 1
        state = state * 1664525 + 1013904223;
        float r1 = static_cast<float>(state) / 4294967296.0f; // [0, 1)
        
        // Rand 2
        state = state * 1664525 + 1013904223;
        float r2 = static_cast<float>(state) / 4294967296.0f; // [0, 1)
        
        // TPDF = R1 - R2 (Range: (-1, 1))
        return r1 - r2;
    }
};

} // namespace Audio
} // namespace Nomad
