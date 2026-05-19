// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Aestra {
namespace Audio {
namespace ExportQuantization {

static constexpr double kPcm16Scale = 32768.0;
static constexpr double kPcm16Lsb = 1.0 / kPcm16Scale;
static constexpr double kPcm24Scale = 8388608.0;
static constexpr double kPcm24Lsb = 1.0 / kPcm24Scale;

struct TpdfDither {
    uint32_t state{0xA357A11Du};

    void setSeed(uint32_t seed) noexcept { state = seed == 0 ? 0xA357A11Du : seed; }

    uint32_t next() noexcept {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    double nextFloat() noexcept { return static_cast<double>(next() & 0x00FFFFFFu) * (1.0 / 16777216.0); }

    double nextTpdf(double lsb) noexcept { return (nextFloat() - nextFloat()) * lsb; }
};

inline double sanitizeAndClamp(float sample) noexcept {
    if (!std::isfinite(sample)) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(sample), -1.0, 1.0);
}

inline int16_t quantizePcm16(float sample, double dither) noexcept {
    const double scaled = (sanitizeAndClamp(sample) + dither) * kPcm16Scale;
    const int64_t value = std::lrint(scaled);
    const int64_t clamped = std::clamp(value, static_cast<int64_t>(-32768), static_cast<int64_t>(32767));
    return static_cast<int16_t>(clamped);
}

inline int16_t quantizePcm16Dithered(float sample, TpdfDither& dither) noexcept {
    return quantizePcm16(sample, dither.nextTpdf(kPcm16Lsb));
}

inline int32_t quantizePcm24(float sample, double dither) noexcept {
    const double scaled = (sanitizeAndClamp(sample) + dither) * kPcm24Scale;
    const int64_t value = std::lrint(scaled);
    const int64_t clamped = std::clamp(value, static_cast<int64_t>(-8388608), static_cast<int64_t>(8388607));
    return static_cast<int32_t>(clamped);
}

inline int32_t quantizePcm24Dithered(float sample, TpdfDither& dither) noexcept {
    return quantizePcm24(sample, dither.nextTpdf(kPcm24Lsb));
}

inline void storePcm24LittleEndian(int32_t sample, uint8_t* dest) noexcept {
    const uint32_t packed = static_cast<uint32_t>(sample);
    dest[0] = static_cast<uint8_t>(packed & 0xFFu);
    dest[1] = static_cast<uint8_t>((packed >> 8u) & 0xFFu);
    dest[2] = static_cast<uint8_t>((packed >> 16u) & 0xFFu);
}

}
}
}
