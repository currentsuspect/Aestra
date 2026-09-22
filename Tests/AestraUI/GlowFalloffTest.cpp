// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C7: drawGlow was one expanded fillRect at alpha * intensity * 0.3 — a
// hard-edged square halo, and its only caller puts it around a 16px-rounded
// header. The replacement builds the falloff out of concentric rounded rects,
// because the renderer has no blur to reach for (the `blur` attribute reaches
// the shader as `vBlur` and is never read).
//
// The geometry cannot be asserted without a GL context, but the arithmetic that
// makes it a gradient rather than a slab can, and that is what actually
// distinguishes the fix from the placeholder. These assertions would all hold
// for a correct blur implementation too, so they pin the contract, not the
// technique.

#include "NUIGlowFalloff.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

void checkNear(float actual, float expected, float tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cout << "[FAIL] " << message << " (expected " << expected << ", got " << actual << ")\n";
        ++g_failures;
    }
}

// The core contract: solving for the per-layer alpha must actually land the
// stack on the requested opacity. Without this the ramp could be smooth and
// still be the wrong brightness.
void testLayerAlphaRoundTrips() {
    for (const int layers : {1, 2, 4, kGlowLayers, 16}) {
        for (const float target : {0.05f, 0.2f, 0.5f, 0.75f, 0.9f}) {
            const float layerAlpha = glowLayerAlpha(target, layers);
            checkNear(glowCompositeAlpha(layerAlpha, layers), target, 1e-5f,
                      "stacking " + std::to_string(layers) + " layers composites to the requested opacity");
        }
    }

    // One layer is the degenerate case and must be exact, not merely close.
    checkNear(glowLayerAlpha(0.42f, 1), 0.42f, 0.0f, "a single layer uses the target alpha unchanged");
}

// This is the assertion the placeholder would have failed: it painted one
// uniform slab, so every covered pixel had identical opacity.
void testFalloffIsAGradientNotASlab() {
    const float target = 0.8f;
    const float layerAlpha = glowLayerAlpha(target, kGlowLayers);

    float previous = 0.0f;
    for (int covered = 1; covered <= kGlowLayers; ++covered) {
        const float alpha = glowCompositeAlpha(layerAlpha, covered);
        check(alpha > previous, "opacity increases strictly with each additional covering layer");
        previous = alpha;
    }

    const float outermost = glowCompositeAlpha(layerAlpha, 1);
    const float innermost = glowCompositeAlpha(layerAlpha, kGlowLayers);
    check(outermost < innermost * 0.5f,
          "the outer edge is far fainter than the core, so the halo reads as a falloff");
    checkNear(innermost, target, 1e-5f, "the innermost band reaches the requested opacity");
}

void testSpreadIsWidestFirstAndCoversTheEdge() {
    const float radius = 20.0f;

    checkNear(glowLayerSpread(radius, 0, kGlowLayers), radius, 1e-5f,
              "the first layer spans the full glow radius");

    float previous = glowLayerSpread(radius, 0, kGlowLayers);
    for (int layer = 1; layer < kGlowLayers; ++layer) {
        const float spread = glowLayerSpread(radius, layer, kGlowLayers);
        check(spread < previous, "each layer is strictly tighter than the one before it");
        previous = spread;
    }

    // The innermost band must still extend past the source rect, or the glow
    // detaches from the shape it is supposed to be glowing around.
    check(previous > 0.0f, "the innermost layer still spreads beyond the source rect");
    checkNear(previous, radius / static_cast<float>(kGlowLayers), 1e-5f,
              "the innermost spread is one radius step");
}

void testDegenerateInputs() {
    checkNear(glowLayerAlpha(0.0f, kGlowLayers), 0.0f, 0.0f, "a zero target needs no coverage");
    checkNear(glowLayerAlpha(1.0f, kGlowLayers), 1.0f, 0.0f, "a fully opaque target saturates");
    checkNear(glowLayerAlpha(-0.5f, kGlowLayers), 0.0f, 0.0f, "a negative target clamps to zero");
    checkNear(glowLayerAlpha(4.0f, kGlowLayers), 1.0f, 0.0f, "an over-unity target clamps to one");

    checkNear(glowCompositeAlpha(0.3f, 0), 0.0f, 0.0f, "no covering layers means no opacity");
    checkNear(glowCompositeAlpha(0.3f, -2), 0.0f, 0.0f, "a negative coverage count is treated as none");

    checkNear(glowLayerSpread(0.0f, 0, kGlowLayers), 0.0f, 0.0f, "a zero radius spreads nowhere");
    checkNear(glowLayerSpread(20.0f, 0, 0), 0.0f, 0.0f, "zero layers spread nowhere");
    // Out-of-range indices clamp rather than producing a negative spread, which
    // would invert the rect and paint a band the caller never asked for.
    check(glowLayerSpread(20.0f, 99, kGlowLayers) > 0.0f, "an out-of-range layer index clamps, never inverts");
}

} // namespace

int main() {
    testLayerAlphaRoundTrips();
    testFalloffIsAGradientNotASlab();
    testSpreadIsWidestFirstAndCoversTheEdge();
    testDegenerateInputs();

    if (g_failures == 0) {
        std::cout << "Glow falloff tests passed\n";
        return 0;
    }
    std::cout << g_failures << " glow falloff test(s) failed\n";
    return 1;
}
