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
 * Piecewise linear, tallest around 0 dB where mixing happens: +6 at the top, -60 at the bottom (everything
 * quieter is the floor). norm() returns 0 at the floor and 1 at the top.
 */
inline constexpr float kTopDb = 6.0f;
inline constexpr float kFloorDb = -60.0f;
inline constexpr float kTicks[] = {6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f};

namespace detail {
inline constexpr float kKnotDb[] = {-60.0f, -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f};
inline constexpr float kKnotNorm[] = {0.0f, 0.07f, 0.15f, 0.30f, 0.42f, 0.56f, 0.74f, 0.90f, 1.0f};
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

} // namespace MixerScale
} // namespace AestraUI
