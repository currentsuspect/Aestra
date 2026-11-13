// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace MixerScale {

/**
 * @brief The one dB scale the mixer shares (V81-M1, FD-28 point 2): meters, the fader cap and the scale at the
 *        panel's left edge all map level to height through this, so level and gain read against each other.
 *
 * Design Spec, Mixer (locked): a console scale. Piecewise linear, widest around unity and tighter toward the
 * bottom: +6 at the top, 0 dB at three quarters, then -6, -12, -24, -48, and -60 at the bottom (everything quieter
 * is the floor). norm() returns 0 at the floor and 1 at the top.
 */
inline constexpr float kTopDb = 6.0f;
inline constexpr float kFloorDb = -60.0f;
inline constexpr float kTicks[] = {6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f};

namespace detail {
inline constexpr float kKnotDb[] = {-60.0f, -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f};
inline constexpr float kKnotNorm[] = {0.0f, 0.06f, 0.13f, 0.24f, 0.33f, 0.44f, 0.58f, 0.75f, 1.0f};
inline constexpr int kKnots = 9;
} // namespace detail

inline float norm(float db) {
    if (!std::isfinite(db) || db <= kFloorDb)
        return 0.0f;
    if (db >= kTopDb)
        return 1.0f;
    for (int i = 1; i < detail::kKnots; ++i) {
        if (db <= detail::kKnotDb[i]) {
            const float t = (db - detail::kKnotDb[i - 1]) / (detail::kKnotDb[i] - detail::kKnotDb[i - 1]);
            return detail::kKnotNorm[i - 1] + t * (detail::kKnotNorm[i] - detail::kKnotNorm[i - 1]);
        }
    }
    return 1.0f;
}

inline float db(float n) {
    n = std::clamp(n, 0.0f, 1.0f);
    for (int i = 1; i < detail::kKnots; ++i) {
        if (n <= detail::kKnotNorm[i]) {
            const float t = (n - detail::kKnotNorm[i - 1]) / (detail::kKnotNorm[i] - detail::kKnotNorm[i - 1]);
            return detail::kKnotDb[i - 1] + t * (detail::kKnotDb[i] - detail::kKnotDb[i - 1]);
        }
    }
    return kTopDb;
}

/// Design Spec, Mixer (locked 12 Nov 2026): the peak line marks the highest level, holds for 1.5 s, then falls at
/// 20 dB a second (linear: ballistics, a measurement rule, not decoration). Never below the signal itself.
inline constexpr double kPeakHoldSeconds = 1.5;
inline constexpr float kPeakFallDbPerSecond = 20.0f;

inline void stepPeakHold(float& holdDb, double& timer, float peakDb, double dt) {
    if (peakDb >= holdDb) {
        holdDb = peakDb;
        timer = 0.0;
        return;
    }
    timer += dt;
    if (timer > kPeakHoldSeconds) {
        const double falling = std::min(dt, timer - kPeakHoldSeconds);
        holdDb = std::max(peakDb, holdDb - kPeakFallDbPerSecond * static_cast<float>(falling));
    }
}

} // namespace MixerScale
} // namespace AestraUI
