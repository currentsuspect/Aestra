// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "TrackManagerUIMath.h"

#include <cmath>
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

void expectNear(double got, double wanted, double tolerance, const std::string& what) {
    if (std::abs(got - wanted) > tolerance) {
        std::cerr << "[FAIL] " << what << ": got " << got << ", wanted " << wanted << '\n';
        ++g_failures;
    }
}

using namespace Aestra::Audio;

} // namespace

int main() {
    // Callers name the coordinate basis by supplying its origin. The formula is
    // otherwise identical for component-, window-, and ruler-relative paths.
    expectNear(timelineGridStartX(0.0f, 236.0f), 241.0, 0.0001, "component-relative grid origin");
    expectNear(timelineGridStartX(80.0f, 236.0f), 321.0, 0.0001, "window-absolute grid origin");
    expectNear(timelineGridStartX(17.5f, 236.0f), 258.5, 0.0001, "ruler-relative grid origin");

    expectNear(timelineGridEndX(80.0f, 800.0f), 865.0, 0.0001, "grid end excludes scrollbar");
    expectNear(timelineTrackAreaTopY(40.0f), 130.0, 0.0001, "track area follows shared vertical stack");

    // Beat conversion is deliberately unclamped: zoom anchoring and dragging
    // left of bar one must preserve negative intermediate values.
    expectNear(timelineGridOffsetToBeat(-50.0f, 10.0f, 20.0f), -2.0, 0.0001,
               "negative beat remains available to caller");
    expectNear(timelineGridOffsetToBeat(150.0f, 50.0f, 25.0f), 8.0, 0.0001, "scroll participates exactly once");

    for (double beat : {-8.0, 0.0, 1.25, 64.0}) {
        const float offset = timelineBeatToGridOffset(beat, 37.0f, 18.0f);
        expectNear(timelineGridOffsetToBeat(offset, 37.0f, 18.0f), beat, 0.0001, "beat/pixel conversion roundtrip");
    }

    const float infinity = std::numeric_limits<float>::infinity();
    check(timelineGridOffsetToBeat(100.0f, 0.0f, 0.0f) == 0.0, "zero zoom has deterministic fallback");
    check(timelineGridOffsetToBeat(100.0f, 0.0f, -4.0f) == 0.0, "negative zoom has deterministic fallback");
    check(timelineGridOffsetToBeat(100.0f, 0.0f, infinity) == 0.0, "non-finite zoom has deterministic fallback");
    check(timelineBeatToGridOffset(8.0, 0.0f, infinity) == 0.0f, "inverse conversion shares invalid-zoom fallback");

    if (g_failures != 0) {
        std::cerr << "[FAIL] TimelineGeometryTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] TimelineGeometryTest\n";
    return 0;
}
