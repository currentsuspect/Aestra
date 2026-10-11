// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>

namespace Aestra {
namespace Audio {
namespace KWeighting {

/**
 * @brief ITU-R BS.1770-4 K-weighting, the one place its coefficients are computed.
 *
 * Two biquads in series: a high-shelf pre-filter (the head's acoustic effect)
 * then the RLB high-pass. The live meter (AudioEngine) and offline analysis
 * (AudioAnalysis) both use these, so a render's measured loudness and the
 * master meter's agree by construction.
 *
 * Coefficients use the normalised form y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
 */
struct Biquad {
    double b0{0.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
};

/// The BS.1770 48 kHz reference coefficients.
inline constexpr Biquad kPreFilter48k{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241,
                                      0.73248077421585};
inline constexpr Biquad kRlb48k{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};

namespace detail {
inline bool matches(const Biquad& a, const Biquad& b, double tolerance) {
    return std::abs(a.b0 - b.b0) <= tolerance && std::abs(a.b1 - b.b1) <= tolerance &&
           std::abs(a.b2 - b.b2) <= tolerance && std::abs(a.a1 - b.a1) <= tolerance &&
           std::abs(a.a2 - b.a2) <= tolerance;
}
} // namespace detail

/**
 * @brief The high-shelf pre-filter at @p sampleRate.
 *
 * Bilinear transform of the BS.1770 analog prototype: shelf height Vh, mid-band
 * term Vb. At 48 kHz it reproduces the reference to 1e-14; the reference
 * constants stand in only if that ever stops being true.
 *
 * (Until V8-S8 the engine used 10^(G/40) and no Vb. That missed the reference,
 * so 48 kHz always fell back to it, and every other rate metered about 1 dB low.)
 */
inline Biquad preFilter(double sampleRate) {
    constexpr double kPi = 3.14159265358979323846;
    const double f0 = 1681.974450955533;
    const double G = 3.999843853973347; // dB
    const double Q = 0.7071752369554196;

    const double K = std::tan(kPi * f0 / sampleRate);
    const double K2 = K * K;
    const double Vh = std::pow(10.0, G / 20.0);
    const double Vb = std::pow(Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K2;

    const Biquad c{(Vh + Vb * K / Q + K2) / a0, 2.0 * (K2 - Vh) / a0, (Vh - Vb * K / Q + K2) / a0,
                   2.0 * (K2 - 1.0) / a0, (1.0 - K / Q + K2) / a0};
    if (std::abs(sampleRate - 48000.0) < 1.0e-9 && !detail::matches(c, kPreFilter48k, 1.0e-9)) {
        return kPreFilter48k;
    }
    return c;
}

/**
 * @brief The RLB high-pass at @p sampleRate.
 *
 * Denominator from the BS.1770 analog prototype; the numerator is the
 * reference {1, -2, 1}. Same 48 kHz guard as preFilter().
 */
inline Biquad rlbHighPass(double sampleRate) {
    constexpr double kPi = 3.14159265358979323846;
    const double f0 = 38.13547087602444;
    const double Q = 0.5003270373238773;

    const double K = std::tan(kPi * f0 / sampleRate);
    const double K2 = K * K;
    const double norm = 1.0 + K / Q + K2;

    const Biquad c{1.0, -2.0, 1.0, 2.0 * (K2 - 1.0) / norm, (1.0 - K / Q + K2) / norm};
    if (std::abs(sampleRate - 48000.0) < 1.0e-9 && !detail::matches(c, kRlb48k, 1.0e-9)) {
        return kRlb48k;
    }
    return c;
}

} // namespace KWeighting
} // namespace Audio
} // namespace Aestra
