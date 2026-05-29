// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MasterSafetyLimiter — final output safety limiter for the master bus.
// Uses a light soft-knee region ahead of a hard safety ceiling so accidental
// spikes get caught before they turn into speaker- or ear-hostile blasts.

#pragma once

#include <algorithm>
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

    bool process(float& L, float& R) {
        const float inL = L;
        const float inR = R;
        L = apply(L);
        R = apply(R);
        return (L != inL) || (R != inR);
    }

    void reset() {}

    // Constants -- do not change without updating tests.
    static constexpr float kKneeStart = 0.98f;
    static constexpr float kCeiling = 0.9997f;
    static constexpr float kKneeRange = kCeiling - kKneeStart;

    // Compilation-compatible aliases. Values intentionally reflect the
    // reshaped limiter, not the old 0.85/0.95 thresholds.
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
        return std::copysign(std::min(softKnee(a), a), s);
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
        const double soft = static_cast<double>(kKneeStart) + shaped * static_cast<double>(kKneeRange);
        return std::copysign(std::min(soft, a), s);
    }
};

} // namespace Audio
} // namespace Aestra
