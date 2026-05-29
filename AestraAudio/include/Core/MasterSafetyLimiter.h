// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MasterSafetyLimiter — final output safety limiter for the master bus.
// Uses a light soft-knee region ahead of a hard safety ceiling so accidental
// spikes get caught before they turn into speaker- or ear-hostile blasts.

#pragma once

#include <cmath>

namespace Aestra {
namespace Audio {

class MasterSafetyLimiter {
public:
    MasterSafetyLimiter() = default;

    // Process a stereo frame in-place. Returns true if limiting occurred.
    bool process(double& L, double& R) {
        const double inL = L;
        const double inR = R;
        L = apply(L);
        R = apply(R);
        const bool limited = (L != inL) || (R != inR);
        return limited;
    }

    void process(float& L, float& R) {
        L = apply(L);
        R = apply(R);
    }

    void reset() {}

    // Constants -- do not change without updating tests.
    static constexpr float kKneeStart = 0.98f;
    static constexpr float kCeiling = 0.9997f;
    static constexpr float kKneeRange = kCeiling - kKneeStart;

    // Backward-compatible aliases for older tests/tools.
    static constexpr double SOFT_KNEE_START = kKneeStart;
    static constexpr double OUTPUT_CEILING = kCeiling;

    static float softKnee(float x) {
        const float t = (x - kKneeStart) / kKneeRange;
        const float shaped = t * t * (3.0f - 2.0f * t);
        return kKneeStart + shaped * kKneeRange;
    }

    static float apply(float s) {
        if (!std::isfinite(s)) {
            return 0.0f;
        }

        const float a = std::abs(s);
        if (a <= kKneeStart) {
            return s;
        }
        if (a >= kCeiling) {
            return std::copysign(kCeiling, s);
        }
        return std::copysign(softKnee(a), s);
    }

    static double apply(double s) {
        if (!std::isfinite(s)) {
            return 0.0;
        }

        const double a = std::abs(s);
        if (a <= static_cast<double>(kKneeStart)) {
            return s;
        }
        if (a >= static_cast<double>(kCeiling)) {
            return std::copysign(static_cast<double>(kCeiling), s);
        }

        const double t = (a - static_cast<double>(kKneeStart)) / static_cast<double>(kKneeRange);
        const double shaped = t * t * (3.0 - 2.0 * t);
        return std::copysign(static_cast<double>(kKneeStart) + shaped * static_cast<double>(kKneeRange), s);
    }
};

} // namespace Audio
} // namespace Aestra
