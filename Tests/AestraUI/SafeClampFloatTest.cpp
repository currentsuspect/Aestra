// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// safeClampFloat boundary semantics (#551, last item).
//
// This test is meaningful only because the call-site audit came first. A test
// written against the old implementation would have pinned "non-finite input
// returns 0.0f" as the contract — immaculate coverage of the wrong semantics.
// What the audit established, across 12 call sites in three families:
//
//   zoom / pixels-per-beat  (4)  bounds [1.0, 32000]        0 is NOT in range
//   position -> grid rect   (5)  computed geometry          0 is NOT in range,
//                                                           and bounds INVERT
//                                                           below ~256px width
//   scroll offset           (3)  bounds [0, maxScroll]      0 IS in range
//
// So the one property all twelve share is: the result must lie inside the
// caller's own interval. The old sentinel violated that for nine of them, and
// for the four zoom sites the violating value is a divisor.
//
// The inverted-bounds tolerance is the opposite case — it IS depended upon by
// ordinary narrow-window layout, and must survive unchanged.

#include "TrackManagerUIMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

using Aestra::Audio::safeClampFloat;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

void expect(float value, float a, float b, float wanted, const std::string& what) {
    const float got = safeClampFloat(value, a, b);
    const bool ok = (std::isnan(wanted) && std::isnan(got)) || (got == wanted);
    if (!ok) {
        std::cerr << "[FAIL] " << what << ": got " << got << ", wanted " << wanted << '\n';
        ++g_failures;
    }
}

} // namespace

int main() {
    // --- finite values, ordinary bounds ---------------------------------------
    expect(5.0f, 0.0f, 10.0f, 5.0f, "in range passes through");
    expect(-1.0f, 0.0f, 10.0f, 0.0f, "below range clamps to lo");
    expect(11.0f, 0.0f, 10.0f, 10.0f, "above range clamps to hi");
    expect(0.0f, 0.0f, 10.0f, 0.0f, "exactly lo");
    expect(10.0f, 0.0f, 10.0f, 10.0f, "exactly hi");

    // --- inverted finite bounds are NORMALISED, not rejected -------------------
    // Depended upon: gridStartX = bounds.x + 236 + 5 and gridEndX = bounds.x +
    // width - 15 invert below ~256px of component width, which is a supported
    // layout. Every case here must match its non-inverted twin above.
    expect(5.0f, 10.0f, 0.0f, 5.0f, "inverted: in range passes through");
    expect(-1.0f, 10.0f, 0.0f, 0.0f, "inverted: below clamps to lo");
    expect(11.0f, 10.0f, 0.0f, 10.0f, "inverted: above clamps to hi");
    // The real geometry, at the width where it flips (audit numbers).
    expect(243.0f, 241.0f, 235.0f, 241.0f, "inverted grid bounds at width 250");
    expect(200.0f, 241.0f, 235.0f, 235.0f, "inverted grid bounds, value below");

    // --- degenerate bounds -----------------------------------------------------
    expect(5.0f, 3.0f, 3.0f, 3.0f, "lo == hi returns that bound");
    expect(1.0f, 3.0f, 3.0f, 3.0f, "lo == hi from below");
    expect(3.0f, 3.0f, 3.0f, 3.0f, "lo == hi exactly");
    expect(kNaN, 3.0f, 3.0f, 3.0f, "lo == hi with NaN value");

    // --- NON-FINITE VALUES: the semantics that changed -------------------------
    // -inf -> lo, +inf -> hi, NaN -> lo. Previously all three returned 0.0f.
    expect(-kInf, 1.0f, 10.0f, 1.0f, "-inf resolves to lo");
    expect(kInf, 1.0f, 10.0f, 10.0f, "+inf resolves to hi");
    expect(kNaN, 1.0f, 10.0f, 1.0f, "NaN resolves to lo");

    // Same, with bounds inverted — the two rules must compose.
    expect(-kInf, 10.0f, 1.0f, 1.0f, "-inf resolves to lo (inverted bounds)");
    expect(kInf, 10.0f, 1.0f, 10.0f, "+inf resolves to hi (inverted bounds)");
    expect(kNaN, 10.0f, 1.0f, 1.0f, "NaN resolves to lo (inverted bounds)");

    // --- the regression this exists to prevent, stated per family --------------
    // Zoom: [1, 32000]. 0 is not a legal pixels-per-beat and is a DIVISOR in
    // every x-to-beat conversion, so the old 0.0f sentinel was the worst possible
    // answer here.
    for (float bad : {kNaN, kInf, -kInf}) {
        const float ppb = safeClampFloat(bad, 1.0f, 32000.0f);
        check(ppb >= 1.0f && ppb <= 32000.0f, "zoom family: non-finite stays in [1, 32000]");
        check(ppb != 0.0f, "zoom family: non-finite never yields a zero divisor");
    }

    // Position: window-absolute coordinates, 0 is off-screen-left of the grid.
    for (float bad : {kNaN, kInf, -kInf}) {
        const float x = safeClampFloat(bad, 241.0f, 656.0f);
        check(x >= 241.0f && x <= 656.0f, "position family: non-finite stays inside the grid rect");
    }

    // Scroll: [0, max]. 0 IS in range here, so this family's behaviour is
    // unchanged — pinned so a future edit cannot quietly move it.
    expect(-kInf, 0.0f, 500.0f, 0.0f, "scroll family: -inf still resolves to 0");
    expect(kNaN, 0.0f, 500.0f, 0.0f, "scroll family: NaN still resolves to 0");
    expect(kInf, 0.0f, 500.0f, 500.0f, "scroll family: +inf resolves to max, not 0");

    // --- bounds that describe no interval --------------------------------------
    // No in-range answer exists, so the defensive sentinel is retained. No caller
    // passes these today; pinned so the retention is deliberate rather than
    // accidental.
    expect(5.0f, kNaN, kNaN, 0.0f, "both bounds NaN -> sentinel");
    expect(5.0f, kInf, -kInf, 0.0f, "both bounds infinite -> sentinel");
    expect(kNaN, kNaN, kNaN, 0.0f, "everything non-finite -> sentinel");

    // --- one non-finite bound collapses to the other ---------------------------
    expect(5.0f, kNaN, 10.0f, 10.0f, "NaN lower bound collapses to the finite one");
    expect(5.0f, 10.0f, kNaN, 10.0f, "NaN upper bound collapses to the finite one");
    expect(5.0f, kInf, 7.0f, 7.0f, "infinite bound collapses to the finite one");

    // --- the result is ALWAYS inside the interval, or the sentinel -------------
    // The single property all twelve call sites share, asserted over a sweep
    // rather than case by case.
    const float values[] = {-kInf, -1e30f, -1.0f, 0.0f, 1.0f, 1e30f, kInf, kNaN};
    const float bounds[][2] = {{0.0f, 10.0f}, {10.0f, 0.0f}, {1.0f, 32000.0f},
                               {241.0f, 656.0f}, {241.0f, 235.0f}, {-5.0f, -1.0f}, {3.0f, 3.0f}};
    for (float v : values) {
        for (const auto& bnd : bounds) {
            const float got = safeClampFloat(v, bnd[0], bnd[1]);
            const float lo = std::min(bnd[0], bnd[1]);
            const float hi = std::max(bnd[0], bnd[1]);
            check(std::isfinite(got), "sweep: result is always finite");
            check(got >= lo && got <= hi, "sweep: result is always inside the caller's interval");
        }
    }

    if (g_failures != 0) {
        std::cerr << "[FAIL] SafeClampFloatTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] SafeClampFloatTest\n";
    return 0;
}
