// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// WCAG contrast ratio between two sRGB colours (components 0..1), 1:1 to 21:1. Pure and header-only. Used to check that a
// non-text cue (the key-owner tick) reads against the bar it sits on: it needs at least 3:1.

#include <algorithm>
#include <cmath>

namespace Aestra::ContrastRatio {

inline float linear(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float luminance(float r, float g, float b) { return 0.2126f * linear(r) + 0.7152f * linear(g) + 0.0722f * linear(b); }

inline float ratio(float r1, float g1, float b1, float r2, float g2, float b2) {
    const float a = luminance(r1, g1, b1), b_ = luminance(r2, g2, b2);
    return (std::max(a, b_) + 0.05f) / (std::min(a, b_) + 0.05f);
}

inline constexpr float kNonTextMinimum = 3.0f;

} // namespace Aestra::ContrastRatio
