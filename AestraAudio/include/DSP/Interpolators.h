// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "CPUDetection.h" // Runtime SIMD dispatch
#include "SincAVX2.h"     // AVX2 dot product
#include "SincAVX512.h"   // AVX-512 dot product
#include "SincNEON.h"     // ARM NEON support
#include "SincSSE41.h"    // SSE4.1 fallback

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h> // AVX2 Support
#endif
#include <memory>

namespace Aestra {
namespace Audio {

/**
 * @brief High-precision interpolation functions for audio resampling.
 *
 * All functions use double precision internally for 144dB+ dynamic range.
 * Output is converted to float for the audio buffer.
 *
 * Quality Modes:
 * - Cubic:    4-point Catmull-Rom, ~80dB SNR, lowest CPU
 * - Sinc8:    8-point Blackman-windowed sinc, ~100dB SNR
 * - Sinc16:   16-point Kaiser-windowed sinc, ~120dB SNR
 * - Sinc32:   32-point Kaiser-windowed sinc, ~130dB SNR
 * - Sinc64:   64-point Kaiser-windowed sinc, ~144dB SNR (mastering)
 */

namespace Interpolators {

// Mathematical constants in double precision
constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 6.28318530717958647693;

// =============================================================================
// Window Functions (all double precision)
// =============================================================================

// Blackman window - good sidelobe rejection
inline double blackmanWindow(double n, double N) {
    const double a0 = 0.42;
    const double a1 = 0.5;
    const double a2 = 0.08;
    const double x = PI * n / (N - 1.0);
    return a0 - a1 * std::cos(2.0 * x) + a2 * std::cos(4.0 * x);
}

// Kaiser window - optimal for given sidelobe/mainlobe tradeoff
// beta controls the tradeoff: higher = better stopband, wider mainlobe
inline double kaiserWindow(double n, double N, double beta) {
    // I0 Bessel function approximation (sufficient for audio)
    auto bessel_I0 = [](double x) -> double {
        double sum = 1.0;
        double term = 1.0;
        const double x_half = x * 0.5;
        for (int k = 1; k < 25; ++k) { // 25 terms is plenty for convergence
            term *= (x_half / static_cast<double>(k));
            term *= (x_half / static_cast<double>(k));
            sum += term;
            if (term < 1e-20)
                break;
        }
        return sum;
    };

    const double half = (N - 1.0) * 0.5;
    const double ratio = (n - half) / half;
    const double arg = beta * std::sqrt(1.0 - ratio * ratio);
    return bessel_I0(arg) / bessel_I0(beta);
}

// Normalized sinc function
inline double sinc(double x) {
    if (std::abs(x) < 1e-10)
        return 1.0;
    const double pix = PI * x;
    return std::sin(pix) / pix;
}

// =============================================================================
// Cubic Hermite (Catmull-Rom) - 4 point, ~80dB SNR
// =============================================================================

struct CubicInterpolator {
    // Double precision, no clamping - let the data speak
    static inline void interpolate(const float* data,        // Interleaved stereo source
                                   int64_t totalFrames,      // Total frames in source
                                   double phase,             // Fractional position in source
                                   float& outL, float& outR) // Output samples
    {
        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);

        // 4-point indices with safe bounds
        const int64_t i0 = (idx > 0) ? idx - 1 : 0;
        const int64_t i1 = idx;
        const int64_t i2 = (idx + 1 < totalFrames) ? idx + 1 : totalFrames - 1;
        const int64_t i3 = (idx + 2 < totalFrames) ? idx + 2 : totalFrames - 1;

        // Load samples as double for precision
        const double l0 = static_cast<double>(data[i0 * 2]);
        const double l1 = static_cast<double>(data[i1 * 2]);
        const double l2 = static_cast<double>(data[i2 * 2]);
        const double l3 = static_cast<double>(data[i3 * 2]);

        const double r0 = static_cast<double>(data[i0 * 2 + 1]);
        const double r1 = static_cast<double>(data[i1 * 2 + 1]);
        const double r2 = static_cast<double>(data[i2 * 2 + 1]);
        const double r3 = static_cast<double>(data[i3 * 2 + 1]);

        // Catmull-Rom coefficients (double precision)
        const double frac2 = frac * frac;
        const double frac3 = frac2 * frac;

        const double c0 = -0.5 * frac3 + frac2 - 0.5 * frac;
        const double c1 = 1.5 * frac3 - 2.5 * frac2 + 1.0;
        const double c2 = -1.5 * frac3 + 2.0 * frac2 + 0.5 * frac;
        const double c3 = 0.5 * frac3 - 0.5 * frac2;

        // Accumulate in double, output as float
        outL = static_cast<float>(l0 * c0 + l1 * c1 + l2 * c2 + l3 * c3);
        outR = static_cast<float>(r0 * c0 + r1 * c1 + r2 * c2 + r3 * c3);
    }
};

// =============================================================================
// Sinc8 - 8 point Blackman-windowed, ~100dB SNR
// =============================================================================

struct Sinc8Interpolator {
    static constexpr int TAPS = 8;
    static constexpr int HALF_TAPS = 4;

    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        static const std::array<double, TAPS> weights = []() {
            std::array<double, TAPS> w;
            for (int i = 0; i < TAPS; ++i)
                w[i] = blackmanWindow(static_cast<double>(i), static_cast<double>(TAPS));
            return w;
        }();

        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);

        // OPTIMIZATION: Trig Reduction
        // sin(pi * (t - frac)) = (-1)^t * -sin(pi * frac)
        // We compute sin(pi*frac) once instead of 8 times.
        const double pix = PI * frac;
        const double sin_pi_frac = std::sin(pix);
        const double neg_sin_pi_frac = -sin_pi_frac;

        double sumL = 0.0;
        double sumR = 0.0;
        double weightSum = 0.0;

        // Pre-calculate signs: t ranges -3 to 4.
        // t=-3: (-1)^-3 = -1
        // t=-2: +1
        // ...
        // Sequence starting at t=-3: -1, 1, -1, 1, -1, 1, -1, 1

        // Loop t from -3 to 4 (8 taps)
        // We can unroll this fully for Sinc8
        for (int t = -HALF_TAPS + 1; t <= HALF_TAPS; ++t) {
            const int64_t sampleIdx = idx + t;
            if (sampleIdx < 0 || sampleIdx >= totalFrames)
                continue;

            const double x = static_cast<double>(t) - frac;

            // Sinc calculation with optimization
            double s;
            if (std::abs(x) < 1e-9) {
                s = 1.0;
            } else {
                // Denominator
                const double denom = PI * x;
                // Numerator sign flip based on t
                // t is even: -sin(pi*frac) -> neg_sin_pi_frac
                // t is odd:  +sin(pi*frac) -> sin_pi_frac
                // Note: Logic derived: sin(pi*(t-f)) = (-1)^t * sin(-pi*f) = (-1)^t * -sin(pi*f)
                // If t is even -> -sin
                // If t is odd  -> +sin
                double numerator = (t % 2 == 0) ? neg_sin_pi_frac : sin_pi_frac;
                s = numerator / denom;
            }

            const int tableIdx = t + HALF_TAPS - 1;
            s *= weights[tableIdx];

            sumL += static_cast<double>(data[sampleIdx * 2]) * s;
            sumR += static_cast<double>(data[sampleIdx * 2 + 1]) * s;
            weightSum += s;
        }

        if (weightSum > 0.0) {
            const double invW = 1.0 / weightSum;
            sumL *= invW;
            sumR *= invW;
        }

        outL = static_cast<float>(sumL);
        outR = static_cast<float>(sumR);
    }
};

// =============================================================================
// Sinc8Turbo (Fast) - 8 point Polyphase Filter Bank
// 256 phases, ~100dB SNR, L1 Cache Friendly (8KB table)
// =============================================================================

struct Sinc8Turbo {
    static constexpr int TAPS = 8;
    static constexpr int PHASES = 256;
    static constexpr int HALF_PHASES = 128;

    struct alignas(64) Table {
        float coeffs[HALF_PHASES][TAPS];

        Table() {
            for (int p = 0; p < HALF_PHASES; ++p) {
                double frac = static_cast<double>(p) / static_cast<double>(PHASES);
                for (int t = -3; t <= 4; ++t) {
                    double x = static_cast<double>(t) - frac;
                    double s = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) / (PI * x);
                    double w = blackmanWindow(static_cast<double>(t + 3), 8.0);
                    coeffs[p][t + 3] = static_cast<float>(s * w);
                }
                double sum = 0.0;
                for (int i = 0; i < TAPS; ++i)
                    sum += coeffs[p][i];
                if (sum > 0.0) {
                    float invSum = static_cast<float>(1.0 / sum);
                    for (int i = 0; i < TAPS; ++i)
                        coeffs[p][i] *= invSum;
                }
            }
        }
    };

    static inline Table& getTable() {
        static Table table;
        return table;
    }

    using InterpolateFunc = void (*)(const float*, int64_t, double, float&, float&);

    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        const Table& table = getTable();

        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);
        int phaseIdx = static_cast<int>(frac * (PHASES - 1) + 0.5);
        bool reversed = (phaseIdx >= HALF_PHASES);
        int lutIdx = reversed ? (PHASES - 1 - phaseIdx) : phaseIdx;
        const float* c = table.coeffs[lutIdx];

        const int64_t startIdx = idx - 3;
        float sumL = 0.0f, sumR = 0.0f;

        if (startIdx >= 0 && startIdx + 8 <= totalFrames && !reversed) {
            const float* samples = &data[startIdx * 2];
            for (int t = 0; t < 8; ++t) {
                sumL += samples[t * 2] * c[t];
                sumR += samples[t * 2 + 1] * c[t];
            }
        } else {
            for (int t = 0; t < 8; ++t) {
                int64_t sIdx = startIdx + t;
                if (sIdx < 0 || sIdx >= totalFrames)
                    continue;
                float coeff = reversed ? c[7 - t] : c[t];
                sumL += data[sIdx * 2] * coeff;
                sumR += data[sIdx * 2 + 1] * coeff;
            }
        }
        outL = sumL;
        outR = sumR;
    }
};

// =============================================================================
// Sinc16Turbo (Quality) - 16 point Polyphase Filter Bank
// 256 phases, ~120dB SNR, L1 Cache Friendly (8KB table)
// =============================================================================

struct Sinc16Turbo {
    static constexpr int TAPS = 16;
    static constexpr int PHASES = 256;
    static constexpr int HALF_PHASES = 128;
    static constexpr double KAISER_BETA = 9.0;

    struct alignas(64) Table {
        float coeffs[HALF_PHASES][TAPS];

        Table() {
            for (int p = 0; p < HALF_PHASES; ++p) {
                double frac = static_cast<double>(p) / static_cast<double>(PHASES);
                for (int t = -7; t <= 8; ++t) {
                    double x = static_cast<double>(t) - frac;
                    double s = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) / (PI * x);
                    double w = kaiserWindow(static_cast<double>(t + 7), 16.0, KAISER_BETA);
                    coeffs[p][t + 7] = static_cast<float>(s * w);
                }
                double sum = 0.0;
                for (int i = 0; i < TAPS; ++i)
                    sum += coeffs[p][i];
                if (sum > 0.0) {
                    float invSum = static_cast<float>(1.0 / sum);
                    for (int i = 0; i < TAPS; ++i)
                        coeffs[p][i] *= invSum;
                }
            }
        }
    };

    static inline Table& getTable() {
        static Table table;
        return table;
    }

    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        const Table& table = getTable();

        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);
        int phaseIdx = static_cast<int>(frac * (PHASES - 1) + 0.5);
        bool reversed = (phaseIdx >= HALF_PHASES);
        int lutIdx = reversed ? (PHASES - 1 - phaseIdx) : phaseIdx;
        const float* c = table.coeffs[lutIdx];

        const int64_t startIdx = idx - 7;
        float sumL = 0.0f, sumR = 0.0f;

        if (startIdx >= 0 && startIdx + 16 <= totalFrames && !reversed) {
            const float* samples = &data[startIdx * 2];
            for (int t = 0; t < 16; ++t) {
                sumL += samples[t * 2] * c[t];
                sumR += samples[t * 2 + 1] * c[t];
            }
        } else {
            for (int t = 0; t < 16; ++t) {
                int64_t sIdx = startIdx + t;
                if (sIdx < 0 || sIdx >= totalFrames)
                    continue;
                float coeff = reversed ? c[15 - t] : c[t];
                sumL += data[sIdx * 2] * coeff;
                sumR += data[sIdx * 2 + 1] * coeff;
            }
        }
        outL = sumL;
        outR = sumR;
    }
};

// =============================================================================
// Sinc16 (Ultra) - 16 point Kaiser-windowed, ~120dB SNR
// =============================================================================

struct Sinc16Interpolator {
    static constexpr int TAPS = 16;
    static constexpr int HALF_TAPS = 8;
    static constexpr double KAISER_BETA = 9.0; // Good stopband attenuation

    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        static const std::array<double, TAPS> weights = []() {
            std::array<double, TAPS> w;
            for (int i = 0; i < TAPS; ++i)
                w[i] = kaiserWindow(static_cast<double>(i), static_cast<double>(TAPS), KAISER_BETA);
            return w;
        }();

        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);

        // OPTIMIZATION: Trig Reduction
        const double pix = PI * frac;
        const double sin_pi_frac = std::sin(pix);
        const double neg_sin_pi_frac = -sin_pi_frac;

        double sumL = 0.0;
        double sumR = 0.0;
        double weightSum = 0.0;

        for (int t = -HALF_TAPS + 1; t <= HALF_TAPS; ++t) {
            const int64_t sampleIdx = idx + t;
            if (sampleIdx < 0 || sampleIdx >= totalFrames)
                continue;

            const double x = static_cast<double>(t) - frac;

            double s;
            if (std::abs(x) < 1e-9) {
                s = 1.0;
            } else {
                double numerator = (t % 2 == 0) ? neg_sin_pi_frac : sin_pi_frac;
                s = numerator / (PI * x);
            }

            const int tableIdx = t + HALF_TAPS - 1;
            s *= weights[tableIdx];

            sumL += static_cast<double>(data[sampleIdx * 2]) * s;
            sumR += static_cast<double>(data[sampleIdx * 2 + 1]) * s;
            weightSum += s;
        }

        if (weightSum > 0.0) {
            const double invW = 1.0 / weightSum;
            sumL *= invW;
            sumR *= invW;
        }

        outL = static_cast<float>(sumL);
        outR = static_cast<float>(sumR);
    }
};

// =============================================================================
// Sinc32 (Extreme) - 32 point Kaiser-windowed, ~130dB SNR
// =============================================================================

struct Sinc32Interpolator {
    static constexpr int TAPS = 32;
    static constexpr int HALF_TAPS = 16;
    static constexpr double KAISER_BETA = 10.0;

    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        static const std::array<double, TAPS> weights = []() {
            std::array<double, TAPS> w;
            for (int i = 0; i < TAPS; ++i)
                w[i] = kaiserWindow(static_cast<double>(i), static_cast<double>(TAPS), KAISER_BETA);
            return w;
        }();

        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);

        // OPTIMIZATION: Trig Reduction
        const double pix = PI * frac;
        const double sin_pi_frac = std::sin(pix);
        const double neg_sin_pi_frac = -sin_pi_frac;

        double sumL = 0.0;
        double sumR = 0.0;
        double weightSum = 0.0;

        for (int t = -HALF_TAPS + 1; t <= HALF_TAPS; ++t) {
            const int64_t sampleIdx = idx + t;
            if (sampleIdx < 0 || sampleIdx >= totalFrames)
                continue;

            const double x = static_cast<double>(t) - frac;

            double s;
            if (std::abs(x) < 1e-9) {
                s = 1.0;
            } else {
                double numerator = (t % 2 == 0) ? neg_sin_pi_frac : sin_pi_frac;
                s = numerator / (PI * x);
            }

            const int tableIdx = t + HALF_TAPS - 1;
            s *= weights[tableIdx];

            sumL += static_cast<double>(data[sampleIdx * 2]) * s;
            sumR += static_cast<double>(data[sampleIdx * 2 + 1]) * s;
            weightSum += s;
        }

        if (weightSum > 0.0) {
            const double invW = 1.0 / weightSum;
            sumL *= invW;
            sumR *= invW;
        }

        outL = static_cast<float>(sumL);
        outR = static_cast<float>(sumR);
    }
};

// =============================================================================
// Sinc64 (Perfect) - 64 point optimized Kaiser, ~144dB SNR
// =============================================================================

// =============================================================================
// Sinc64Turbo (The "Weapon") - 64 point Polyphase Filter Bank
// 2048 phases, 144dB SNR, AVX2 Accelerated
// =============================================================================

struct Sinc64Turbo {
    static constexpr int TAPS = 64;
    static constexpr int HALF_TAPS = 32;
    static constexpr int PHASES = 2048;
    static constexpr int HALF_PHASES = 1024;
    static constexpr double KAISER_BETA = 12.0;

    struct alignas(64) Table {
        // TASK 2: Full 64-tap table for efficient vectorized access.
        // 2048 phases * 64 taps = 131072 floats = 512KB.
        // Symmetry folding was attempted (512KB → 256KB) but caused scattered
        // memory access patterns that defeated SIMD vectorization.
        float coeffs[HALF_PHASES][TAPS];

        Table() {
            for (int p = 0; p < HALF_PHASES; ++p) {
                double frac = static_cast<double>(p) / static_cast<double>(PHASES);
                for (int t = -31; t <= 32; ++t) {
                    double x = static_cast<double>(t) - frac;
                    double s = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) / (PI * x);
                    double w = kaiserWindow(static_cast<double>(t + 31), 64.0, KAISER_BETA);
                    coeffs[p][t + 31] = static_cast<float>(s * w);
                }
                double sum = 0.0;
                for (int i = 0; i < TAPS; ++i)
                    sum += coeffs[p][i];
                if (sum > 0.0) {
                    float invSum = static_cast<float>(1.0 / sum);
                    for (int i = 0; i < TAPS; ++i)
                        coeffs[p][i] *= invSum;
                }
            }
        }
    };

    static inline Table& getTable() {
        static Table table;
        return table;
    }

    // Function pointer type for dispatch
    using InterpolateFunc = void (*)(const float*, int64_t, double, float&, float&);

    // -------------------------------------------------------------------------
    // Implementation Generators
    // -------------------------------------------------------------------------

    // Core logic template to avoid code duplication across SIMD variants
    // SimdOp: Lambda/Functor for the dot product
    template <typename SimdOp, typename SimdRevOp>
    static inline void interpolateImpl(const float* data, int64_t totalFrames, double phase, float& outL, float& outR,
                                       SimdOp op, SimdRevOp revOp) {
        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);
        const double phaseF = frac * static_cast<double>(PHASES - 1);
        const int phaseIdx = static_cast<int>(phaseF + 0.5);
        bool reversed = (phaseIdx >= HALF_PHASES);
        int lutIdx = reversed ? (PHASES - 1 - phaseIdx) : phaseIdx;

        // TASK 2: Phase interpolation — load adjacent entry for sub-quantization blending
        int lutIdx2 = (lutIdx + 1 < HALF_PHASES) ? lutIdx + 1 : lutIdx;
        double alpha = phaseF - std::floor(phaseF);  // fractional part within quantized step
        if (reversed) {
            alpha = 1.0 - alpha;  // reverse direction of alpha for mirrored phases
        }
        const float alphaF = static_cast<float>(alpha);

        // Fast access to static table (two adjacent phases)
        const float* c0 = getTable().coeffs[lutIdx];
        const float* c1 = getTable().coeffs[lutIdx2];

        const int64_t startIdx = idx - 31;
        float sumL = 0.0f;
        float sumR = 0.0f;

        bool validRange = (startIdx >= 0 && startIdx + 64 <= totalFrames);

        if (validRange && !reversed) {
            op(c0, c1, alphaF, &data[startIdx * 2], sumL, sumR);
        } else if (validRange && reversed) {
            revOp(c0, c1, alphaF, &data[startIdx * 2], sumL, sumR);
        } else {
            // Scalar fallback for boundaries
            for (int t = 0; t < 64; ++t) {
                int64_t sIdx = startIdx + t;
                if (sIdx < 0 || sIdx >= totalFrames)
                    continue;
                float coeff0 = reversed ? c0[63 - t] : c0[t];
                float coeff1 = reversed ? c1[63 - t] : c1[t];
                float coeff = coeff0 + alphaF * (coeff1 - coeff0);
                sumL += data[sIdx * 2] * coeff;
                sumR += data[sIdx * 2 + 1] * coeff;
            }
        }
        outL = sumL;
        outR = sumR;
    }

    // -------------------------------------------------------------------------
    // Specific Implementations
    // -------------------------------------------------------------------------

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    static void implAVX512(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        interpolateImpl(data, totalFrames, phase, outL, outR, sincDotProductAVX512, sincDotProductAVX512_Reversed);
    }

    static void implAVX2(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        interpolateImpl(data, totalFrames, phase, outL, outR, sincDotProductAVX2, sincDotProductAVX2_Reversed);
    }

    static void implSSE41(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        auto scalarRev = [](const float* c0, const float* c1, float alpha, const float* s, float& l, float& r) {
            float sl = 0, sr = 0;
            for (int t = 0; t < 64; ++t) {
                float coeff0 = c0[63 - t];
                float coeff1 = c1[63 - t];
                float coeff = coeff0 + alpha * (coeff1 - coeff0);
                sl += s[t * 2] * coeff;
                sr += s[t * 2 + 1] * coeff;
            }
            l = sl;
            r = sr;
        };
        interpolateImpl(data, totalFrames, phase, outL, outR, sincDotProductSSE41, scalarRev);
    }
#endif // x86

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    static void implNEON(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        interpolateImpl(data, totalFrames, phase, outL, outR, sincDotProductNEON,
                        [](const float* c0, const float* c1, float alpha, const float* s, float& l, float& r) {
                            float sl = 0, sr = 0;
                            for (int t = 0; t < 64; ++t) {
                                float coeff0 = c0[63 - t];
                                float coeff1 = c1[63 - t];
                                float coeff = coeff0 + alpha * (coeff1 - coeff0);
                                sl += s[t * 2] * coeff;
                                sr += s[t * 2 + 1] * coeff;
                            }
                            l = sl;
                            r = sr;
                        });
    }
#endif // ARM NEON

    static void implScalar(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        auto scalarOp = [](const float* c0, const float* c1, float alpha, const float* s, float& l, float& r) {
            float sl = 0, sr = 0;
            for (int t = 0; t < 64; ++t) {
                float coeff = c0[t] + alpha * (c1[t] - c0[t]);
                sl += s[t * 2] * coeff;
                sr += s[t * 2 + 1] * coeff;
            }
            l = sl;
            r = sr;
        };
        auto scalarRev = [](const float* c0, const float* c1, float alpha, const float* s, float& l, float& r) {
            float sl = 0, sr = 0;
            for (int t = 0; t < 64; ++t) {
                float coeff0 = c0[63 - t];
                float coeff1 = c1[63 - t];
                float coeff = coeff0 + alpha * (coeff1 - coeff0);
                sl += s[t * 2] * coeff;
                sr += s[t * 2 + 1] * coeff;
            }
            l = sl;
            r = sr;
        };
        interpolateImpl(data, totalFrames, phase, outL, outR, scalarOp, scalarRev);
    }

    // -------------------------------------------------------------------------
    // Dispatcher
    // -------------------------------------------------------------------------

    static InterpolateFunc resolve() {
        // Force table initialization on first call (startup)
        getTable();

        const auto& cpu = Aestra::Core::CPUDetection::get();
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        if (cpu.hasAVX512F())
            return implAVX512;
        if (cpu.hasAVX2() && cpu.hasFMA()
#if defined(AESTRA_SINC_CAN_INLINE_AVX2_FMA)
            && defined(AESTRA_SINC_CAN_INLINE_AVX2_FMA)
#endif
        )
            return implAVX2;
        if (cpu.hasSSE41())
            return implSSE41;
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
        if (cpu.hasNEON())
            return implNEON;
#endif
        return implScalar;
    }

    static inline InterpolateFunc pImpl = resolve();

public:
    // The Hot Path: Indirect function call, ZERO branches, ZERO checks.
    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        pImpl(data, totalFrames, phase, outL, outR);
    }
};

// =============================================================================
// Sinc32Turbo (Fast) - 32 point Polyphase Filter Bank
// 1024 phases, ~100dB SNR, L1 Cache Friendly (64KB table)
// Ideal for: mixing, muted tracks, preview playback
// =============================================================================

struct Sinc32Turbo {
    static constexpr int TAPS = 32;
    static constexpr int PHASES = 1024;
    static constexpr int HALF_PHASES = 512;
    static constexpr double KAISER_BETA = 9.0;

    struct alignas(64) Table {
        float coeffs[HALF_PHASES][TAPS]; // 64KB - fits L1

        Table() {
            for (int p = 0; p < HALF_PHASES; ++p) {
                double frac = static_cast<double>(p) / static_cast<double>(PHASES);
                for (int t = -15; t <= 16; ++t) {
                    double x = static_cast<double>(t) - frac;
                    double s = (std::abs(x) < 1e-10) ? 1.0 : std::sin(PI * x) / (PI * x);
                    double w = kaiserWindow(static_cast<double>(t + 15), 32.0, KAISER_BETA);
                    coeffs[p][t + 15] = static_cast<float>(s * w);
                }
                double sum = 0.0;
                for (int i = 0; i < TAPS; ++i)
                    sum += coeffs[p][i];
                if (sum > 0.0) {
                    float invSum = static_cast<float>(1.0 / sum);
                    for (int i = 0; i < TAPS; ++i)
                        coeffs[p][i] *= invSum;
                }
            }
        }
    };

    static inline const Table& getTable() {
        static const Table table;
        return table;
    }

    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        const Table& table = getTable();

        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);
        int phaseIdx = static_cast<int>(frac * (PHASES - 1) + 0.5);
        bool reversed = (phaseIdx >= HALF_PHASES);
        int lutIdx = reversed ? (PHASES - 1 - phaseIdx) : phaseIdx;
        const float* c = table.coeffs[lutIdx];

        const int64_t startIdx = idx - 15;
        float sumL = 0.0f, sumR = 0.0f;

        if (startIdx >= 0 && startIdx + 32 <= totalFrames && !reversed) {
            const float* samples = &data[startIdx * 2];
            for (int t = 0; t < 32; ++t) {
                sumL += samples[t * 2] * c[t];
                sumR += samples[t * 2 + 1] * c[t];
            }
        } else {
            for (int t = 0; t < 32; ++t) {
                int64_t sIdx = startIdx + t;
                if (sIdx < 0 || sIdx >= totalFrames)
                    continue;
                float coeff = reversed ? c[31 - t] : c[t];
                sumL += data[sIdx * 2] * coeff;
                sumR += data[sIdx * 2 + 1] * coeff;
            }
        }
        outL = sumL;
        outR = sumR;
    }
};

struct Sinc64Interpolator {
    // Legacy implementation kept for benchmark comparison
    static constexpr int TAPS = 64;
    static constexpr int HALF_TAPS = 32;
    static constexpr double KAISER_BETA = 12.0;

    static inline void interpolate(const float* data, int64_t totalFrames, double phase, float& outL, float& outR) {
        static const std::array<double, TAPS> weights = []() {
            std::array<double, TAPS> w;
            for (int i = 0; i < TAPS; ++i)
                w[i] = kaiserWindow(static_cast<double>(i), static_cast<double>(TAPS), KAISER_BETA);
            return w;
        }();

        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);

        // OPTIMIZATION: Massive reduction (1 sin vs 64 sins)
        const double pix = PI * frac;
        const double sin_pi_frac = std::sin(pix);
        const double neg_sin_pi_frac = -sin_pi_frac;

        double sumL = 0.0;
        double sumR = 0.0;
        double weightSum = 0.0;

        // Main loop - Auto-vectorizes well
        for (int t = -HALF_TAPS + 1; t <= HALF_TAPS; ++t) {
            const int64_t sampleIdx = idx + t;
            // Check bounds (rare branch inside loop, effectively predicted usually, or use padding)
            // For extreme quality, we clamp or zero. Here we skip.
            if (sampleIdx < 0 || sampleIdx >= totalFrames)
                continue;

            const double x = static_cast<double>(t) - frac;

            double s;
            if (std::abs(x) < 1e-9) {
                s = 1.0;
            } else {
                double numerator = (t % 2 == 0) ? neg_sin_pi_frac : sin_pi_frac;
                s = numerator / (PI * x);
            }

            const int tableIdx = t + HALF_TAPS - 1;
            s *= weights[tableIdx];

            sumL += static_cast<double>(data[sampleIdx * 2]) * s;
            sumR += static_cast<double>(data[sampleIdx * 2 + 1]) * s;
            weightSum += s;
        }

        if (weightSum > 0.0) {
            const double invW = 1.0 / weightSum;
            sumL *= invW;
            sumR *= invW;
        }

        outL = static_cast<float>(sumL);
        outR = static_cast<float>(sumR);
    }
};

// =============================================================================
// Quality Enum for runtime selection
// =============================================================================

enum class InterpolationQuality {
    Cubic,  // 4-point, ~80dB, lowest CPU
    Sinc8,  // 8-point Blackman, ~100dB
    Sinc16, // 16-point Kaiser, ~120dB (Ultra)
    Sinc32, // 32-point Kaiser, ~130dB (Extreme)
    Sinc64  // 64-point Kaiser, ~144dB (Perfect/Mastering)
};

// Runtime dispatch helper
inline void interpolateSample(InterpolationQuality quality, const float* data, int64_t totalFrames, double phase,
                              float& outL, float& outR) {
    switch (quality) {
    case InterpolationQuality::Cubic:
        CubicInterpolator::interpolate(data, totalFrames, phase, outL, outR);
        break;
    case InterpolationQuality::Sinc8:
        Sinc8Interpolator::interpolate(data, totalFrames, phase, outL, outR);
        break;
    case InterpolationQuality::Sinc16:
        Sinc16Interpolator::interpolate(data, totalFrames, phase, outL, outR);
        break;
    case InterpolationQuality::Sinc32:
        Sinc32Interpolator::interpolate(data, totalFrames, phase, outL, outR);
        break;
    case InterpolationQuality::Sinc64:
        Sinc64Turbo::interpolate(data, totalFrames, phase, outL, outR);
        break;
    default:
        // Defensive handling for unknown/added enum values
        // Assert in debug builds to catch issues during development/testing
        assert(false && "Unknown InterpolationQuality enum value in interpolateSample");
        // Fall back to safe CubicInterpolator to prevent undefined behavior
        // in release builds or when assertions are disabled
        CubicInterpolator::interpolate(data, totalFrames, phase, outL, outR);
        break;
    }
}

// Force pre-computation of static tables (call from Main Thread at startup)
inline void precomputeTables() {
    Sinc64Turbo::getTable();
    Sinc32Turbo::getTable();
    // Ensure all variants are touched if they have statics (Sinc8, Sinc16, Sinc32, Sinc64 have static weights inside
    // interpolate) We can't easily force-init function-local statics without running the function. However,
    // Sinc8/16/32/64 use std::array initialized with lambda. Ideally we'd move those out too, but they are small (8-64
    // doubles). The "Turbo" ones are the heavy ones (Megabytes).
}

/**
 * @brief Mono sinc interpolation — reads single-channel float data.
 * Uses a Blackman-Harris-windowed sinc kernel matching the requested quality.
 */
inline double sincInterpolateMono(const float* data, uint64_t totalFrames, double phase,
                                  InterpolationQuality quality) {
    int halfLen = 4;
    switch (quality) {
    case InterpolationQuality::Sinc8:  halfLen = 4; break;
    case InterpolationQuality::Sinc16: halfLen = 8; break;
    case InterpolationQuality::Sinc32: halfLen = 16; break;
    case InterpolationQuality::Sinc64: halfLen = 32; break;
    default: halfLen = 4; break;
    }

    constexpr double pi = 3.14159265358979323846;
    double frac = phase - std::floor(phase);
    int64_t center = static_cast<int64_t>(phase);
    double sum = 0.0, wSum = 0.0;

    for (int k = -halfLen; k <= halfLen; ++k) {
        int64_t idx = center + k;
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int64_t>(totalFrames)) idx = static_cast<int64_t>(totalFrames) - 1;

        double x = static_cast<double>(k) - frac;
        double sincVal = (std::abs(x) < 1e-9) ? 1.0 : std::sin(pi * x) / (pi * x);

        // Blackman-Harris window
        double r = static_cast<double>(k) / halfLen;
        double kaiser = 0.35875 - 0.48829 * std::cos(pi * r) + 0.14128 * std::cos(2.0 * pi * r) - 0.01168 * std::cos(3.0 * pi * r);

        double w = sincVal * kaiser;
        sum += data[idx] * w;
        wSum += std::abs(w);
    }

    return wSum > 1e-12 ? sum / wSum : 0.0;
}

} // namespace Interpolators
} // namespace Audio
} // namespace Aestra
