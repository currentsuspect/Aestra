// © 2026 Aestra Studios — All Rights Reserved.
// TimelineMarqueeTest — widget-independent marquee drag contract (#847).
//
// The two #847 failure modes presented as an app hang: a marquee that let the
// pointer escape mid-drag, and per-move cache invalidation. The drag state
// machine (TimelineMarquee.h) owns the sequencing rules so they are testable
// without linking AestraUI (AESTRA_CI=ON). The "no per-move invalidation
// storm" property is enforced structurally: update() is the only move-time
// entry point and it mutates nothing but the endpoint.

#include "TimelineMarquee.h"

#include <cmath>
#include <iostream>

using Aestra::Components::MarqueeButton;
using Aestra::Components::TimelineMarqueeDrag;

namespace {

constexpr MarqueeButton kLeft = 1;
constexpr MarqueeButton kRight = 2;

int g_failures = 0;

void check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "  FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

void testBeginRules() {
    std::cout << "  [1/6] Begin: marquee-button press inside the track area... ";
    TimelineMarqueeDrag drag;
    check(!drag.begin(false, kLeft, 100.0f, 200.0f, true), "release does not begin");
    check(!drag.begin(true, 0, 100.0f, 200.0f, true), "no-button press does not begin");
    check(!drag.begin(true, kLeft, 100.0f, 200.0f, false), "press outside the track area does not begin");
    check(drag.begin(true, kLeft, 100.0f, 200.0f, true), "press inside the track area begins");
    check(drag.active, "drag active after begin");
    check(!drag.begin(true, kRight, 150.0f, 220.0f, true), "a second press cannot restart an active drag");
    check(drag.startX == 100.0f && drag.startY == 200.0f, "start pinned at the begin point");
    check(drag.endX == 100.0f && drag.endY == 200.0f, "endpoint starts at the begin point");
    std::cout << "PASSED\n";
}

void testOwnershipUntilRelease() {
    std::cout << "  [2/6] Active marquee owns every event until its button releases... ";
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 100.0f, 200.0f, true);
    check(drag.ownsEvent(), "owns events while active");
    drag.update(140.0f, 260.0f);
    check(drag.ownsEvent(), "still owns events after moves");
    check(!drag.shouldFinalize(true, kRight), "foreign-button release does not finalize");
    check(drag.ownsEvent(), "drag survives a foreign-button release");
    check(drag.shouldFinalize(true, kLeft), "own-button release finalizes");
    drag.finalize();
    check(!drag.ownsEvent(), "no longer owns events after finalize");
    check(!drag.shouldFinalize(true, kLeft), "finalize refused when inactive");
    std::cout << "PASSED\n";
}

void testMoveIsPureEndpointUpdate() {
    std::cout << "  [3/6] Move mutates only the endpoint (no storm surface)... ";
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 100.0f, 200.0f, true);
    const bool wasActive = drag.active;
    const MarqueeButton wasButton = drag.button;
    const float wasStartX = drag.startX;
    const float wasStartY = drag.startY;

    for (int i = 0; i < 1000; ++i) { // the drag of a long select: many moves
        drag.update(100.0f + static_cast<float>(i), 200.0f + static_cast<float>(i) * 0.5f);
    }

    check(drag.active == wasActive && drag.button == wasButton, "activation state untouched by moves");
    check(drag.startX == wasStartX && drag.startY == wasStartY, "start corner untouched by moves");
    check(std::abs(drag.endX - 1099.0f) < 0.001f && std::abs(drag.endY - 699.5f) < 0.001f,
          "endpoint tracks the last position only");
    check(!drag.shouldFinalize(false, kLeft), "no finalize signal from a move");
    std::cout << "PASSED\n";
}

void testUpdateIgnoredWhenInactive() {
    std::cout << "  [4/6] Update on an inactive machine is a no-op... ";
    TimelineMarqueeDrag drag;
    drag.update(50.0f, 60.0f);
    check(!drag.active && drag.endX == 0.0f && drag.endY == 0.0f, "inactive update changes nothing");
    std::cout << "PASSED\n";
}

void testRectNormalization() {
    std::cout << "  [5/6] Rectangle normalizes regardless of drag direction... ";
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 300.0f, 500.0f, true);
    drag.update(120.0f, 180.0f); // dragged up-left: end < start on both axes
    check(std::abs(drag.rectMinX() - 120.0f) < 0.001f && std::abs(drag.rectMaxX() - 300.0f) < 0.001f,
          "x extent normalized");
    check(std::abs(drag.rectMinY() - 180.0f) < 0.001f && std::abs(drag.rectMaxY() - 500.0f) < 0.001f,
          "y extent normalized");
    check(std::abs(drag.rectWidth() - 180.0f) < 0.001f && std::abs(drag.rectHeight() - 320.0f) < 0.001f,
          "extents positive in any direction");
    std::cout << "PASSED\n";
}

void testRestartAfterFinalize() {
    std::cout << "  [6/6] A finalize allows an immediate new drag... ";
    TimelineMarqueeDrag drag;
    drag.begin(true, kRight, 10.0f, 20.0f, true);
    drag.update(30.0f, 40.0f);
    drag.finalize();
    check(drag.begin(true, kLeft, 100.0f, 200.0f, true), "new drag begins after finalize");
    check(drag.button == kLeft && drag.startX == 100.0f, "new drag carries its own button and start");
    std::cout << "PASSED\n";
}

} // namespace

int main() {
    std::cout << "TimelineMarqueeTest (#847)\n";
    testBeginRules();
    testOwnershipUntilRelease();
    testMoveIsPureEndpointUpdate();
    testUpdateIgnoredWhenInactive();
    testRectNormalization();
    testRestartAfterFinalize();
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All timeline marquee tests passed\n";
    return 0;
}
