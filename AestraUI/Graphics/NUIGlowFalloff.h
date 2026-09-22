// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Falloff arithmetic for drawGlow, kept separate from the GL backend so it can
// be tested without a GL context.
//
// WHY LAYERS AND NOT A BLUR: the renderer has no blur. `blur` is plumbed all the
// way from addQuad() through a per-vertex attribute into the `vBlur` varying,
// and the fragment shader never reads it — two comments in the shader describe
// behaviour that does not exist. drawShadow() passes a blur too, and is
// likewise hard-edged. Until that is addressed (it changes every shadow in the
// app, so it needs a visual pass), a glow has to be built out of primitives
// that do anti-alias, which means concentric rounded rects.
//
// A region covered by k of n stacked layers, each at alpha a, ends up at
// 1 - (1 - a)^k. That is a smooth monotonic ramp from the outer edge inward, so
// the geometry supplies the falloff and only the per-layer alpha has to be
// solved for.

#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {

/** @brief Number of concentric layers a glow is composed from. */
inline constexpr int kGlowLayers = 8;

/**
 * @brief Per-layer alpha such that `layers` stacked layers composite to `target`.
 *
 * Inverts 1 - (1 - a)^n = target. With one layer the answer is `target` itself,
 * which keeps the single-layer case exact rather than merely close.
 */
inline float glowLayerAlpha(float target, int layers) {
    const float clamped = std::clamp(target, 0.0f, 1.0f);
    if (layers <= 1 || clamped <= 0.0f || clamped >= 1.0f) {
        return clamped;
    }
    return 1.0f - std::pow(1.0f - clamped, 1.0f / static_cast<float>(layers));
}

/**
 * @brief Opacity reached where `covered` of `layers` overlap, each at `layerAlpha`.
 *
 * The inverse of glowLayerAlpha(), and what the tests assert the ramp against.
 */
inline float glowCompositeAlpha(float layerAlpha, int covered) {
    const float clamped = std::clamp(layerAlpha, 0.0f, 1.0f);
    if (covered <= 0) {
        return 0.0f;
    }
    return 1.0f - std::pow(1.0f - clamped, static_cast<float>(covered));
}

/**
 * @brief Outward spread of layer `index`, widest first.
 *
 * Layer 0 sits at the full `radius` and the last layer hugs the source rect, so
 * drawing in index order paints largest to smallest and the overlap count rises
 * toward the centre.
 */
inline float glowLayerSpread(float radius, int index, int layers) {
    if (layers <= 0 || radius <= 0.0f) {
        return 0.0f;
    }
    const int clampedIndex = std::clamp(index, 0, layers - 1);
    const float step = radius / static_cast<float>(layers);
    return radius - (step * static_cast<float>(clampedIndex));
}

} // namespace AestraUI
