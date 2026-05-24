// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @file AudioExportQuantization.h
 * @brief PCM quantization with optional triangular-pdf dither for offline export.
 */

namespace Aestra {
namespace Audio {
namespace ExportQuantization {

/// Scale factor for 16-bit PCM (2^15).
static constexpr double kPcm16Scale = 32768.0;
/// Least-significant bit weight for 16-bit PCM.
static constexpr double kPcm16Lsb = 1.0 / kPcm16Scale;
/// Scale factor for 24-bit PCM (2^23).
static constexpr double kPcm24Scale = 8388608.0;
/// Least-significant bit weight for 24-bit PCM.
static constexpr double kPcm24Lsb = 1.0 / kPcm24Scale;

/**
 * @brief Xorshift32 PRNG producing triangular-pdf (TPDF) dither samples.
 *
 * Two uniform random values are differenced to produce a triangular
 * distribution, which is the optimal noise-shaping shape for linear
 * quantization at a given bit depth.
 */
struct TpdfDither {
    uint32_t state{0xA357A11Du};

    /** @brief Reseed the PRNG. Zero is remapped to a non-zero default. */
    void setSeed(uint32_t seed) noexcept { state = seed == 0 ? 0xA357A11Du : seed; }

    /** @brief Advance the xorshift32 state and return the raw 32-bit output. */
    uint32_t next() noexcept {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    /** @brief Return a uniform double in [0, 1) from the low 24 bits. */
    double nextFloat() noexcept { return static_cast<double>(next() & 0x00FFFFFFu) * (1.0 / 16777216.0); }

    /** @brief Return a TPDF-scaled dither sample in the range [-lsb, +lsb]. */
    double nextTpdf(double lsb) noexcept { return (nextFloat() - nextFloat()) * lsb; }
};

/**
 * @brief Sanitize a float sample: replace NaN/Inf with 0 and clamp to [-1, 1].
 * @param sample Input sample.
 * @return Clamped double in [-1, 1].
 */
inline double sanitizeAndClamp(float sample) noexcept {
    if (!std::isfinite(sample)) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(sample), -1.0, 1.0);
}

/** @brief Quantize a float sample to 16-bit PCM with an explicit dither offset. */
inline int16_t quantizePcm16(float sample, double dither) noexcept {
    const double scaled = (sanitizeAndClamp(sample) + dither) * kPcm16Scale;
    const int64_t value = std::lrint(scaled);
    const int64_t clamped = std::clamp(value, static_cast<int64_t>(-32768), static_cast<int64_t>(32767));
    return static_cast<int16_t>(clamped);
}

/** @brief Quantize a float sample to 16-bit PCM with auto-generated TPDF dither. */
inline int16_t quantizePcm16Dithered(float sample, TpdfDither& dither) noexcept {
    return quantizePcm16(sample, dither.nextTpdf(kPcm16Lsb));
}

/** @brief Quantize a float sample to 24-bit PCM with an explicit dither offset. */
inline int32_t quantizePcm24(float sample, double dither) noexcept {
    const double scaled = (sanitizeAndClamp(sample) + dither) * kPcm24Scale;
    const int64_t value = std::lrint(scaled);
    const int64_t clamped = std::clamp(value, static_cast<int64_t>(-8388608), static_cast<int64_t>(8388607));
    return static_cast<int32_t>(clamped);
}

/** @brief Quantize a float sample to 24-bit PCM with auto-generated TPDF dither. */
inline int32_t quantizePcm24Dithered(float sample, TpdfDither& dither) noexcept {
    return quantizePcm24(sample, dither.nextTpdf(kPcm24Lsb));
}

/** @brief Pack a 24-bit PCM sample into 3 bytes, little-endian. */
inline void storePcm24LittleEndian(int32_t sample, uint8_t* dest) noexcept {
    const uint32_t packed = static_cast<uint32_t>(sample);
    dest[0] = static_cast<uint8_t>(packed & 0xFFu);
    dest[1] = static_cast<uint8_t>((packed >> 8u) & 0xFFu);
    dest[2] = static_cast<uint8_t>((packed >> 16u) & 0xFFu);
}

}
}
}
