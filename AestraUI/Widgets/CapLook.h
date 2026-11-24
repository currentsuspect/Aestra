// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace CapLook {

/**
 * @brief The fader cap's dished face (Design Spec, Mixer, board p-mixer2 locked, variant F): 24 by 34, radius 3,
 *        a vertical gradient of the base colour: 20% black at the top, 14% white at 28%, the base at 50%, 10%
 *        white at 72%, 26% black at the bottom, then a 2px index line across the middle. Pure functions.
 */
inline constexpr float kW = 24.0f;
inline constexpr float kH = 34.0f;
inline constexpr float kRadius = 3.0f;
inline constexpr float kIndexH = 2.0f;

struct Rgb {
    float r, g, b;
};

struct Stop {
    float at;     // 0..1 down the face
    float amount; // how far toward the target
    bool white;   // target colour: white, otherwise black
};
inline constexpr Stop kStops[] = {{0.0f, 0.20f, false}, {0.28f, 0.14f, true}, {0.50f, 0.0f, true},
                                  {0.72f, 0.10f, true}, {1.0f, 0.26f, false}};
inline constexpr int kStopCount = 5;

inline Rgb stopColour(const Rgb& base, const Stop& s) {
    const float t = s.white ? 1.0f : 0.0f;
    return {base.r + (t - base.r) * s.amount, base.g + (t - base.g) * s.amount, base.b + (t - base.b) * s.amount};
}

/// The face colour at @p t (0 top .. 1 bottom): linear between the stops, like the board's gradient.
inline Rgb faceColour(const Rgb& base, float t) {
    t = std::clamp(std::isfinite(t) ? t : 0.0f, 0.0f, 1.0f);
    for (int i = 1; i < kStopCount; ++i) {
        if (t <= kStops[i].at) {
            const Rgb a = stopColour(base, kStops[i - 1]);
            const Rgb b = stopColour(base, kStops[i]);
            const float u = (t - kStops[i - 1].at) / (kStops[i].at - kStops[i - 1].at);
            return {a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u};
        }
    }
    return stopColour(base, kStops[kStopCount - 1]);
}

/// How far in from the left and right edge row @p row of a @p height tall face starts, so a row-by-row fill keeps the
/// face's rounded corners: the circle's edge at the row's centre, rounded down to whole pixels.
inline float cornerInset(int row, float height, float radius) {
    const float centre = static_cast<float>(row) + 0.5f;
    const float fromEdge = std::min(centre, height - centre);
    if (!(fromEdge < radius))
        return 0.0f;
    const float dy = radius - fromEdge;
    return std::floor(radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy)));
}

} // namespace CapLook
} // namespace AestraUI
