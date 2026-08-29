// © 2026 Aestra Studios — All Rights Reserved.
// TimelineMarqueeTest — widget-independent marquee drag contract (#847).
//
// The two #847 failure modes presented as an app hang: a marquee that let the
// pointer escape mid-drag, and per-move cache invalidation. The drag state
// machine (TimelineMarquee.h) owns the sequencing rules so they are testable
// without linking AestraUI (AESTRA_CI=ON):
//   - An active marquee owns every event until the button that began it
//     releases; foreign buttons neither finalize it nor move its band.
//   - onEvent() is the only event-time entry point and mutates nothing but
//     the endpoint, on Move kinds and the initiating Release only — wheel,
//     enter/leave and synthetic events map to Other and cannot move the
//     band. The "no per-move invalidation storm" property is structural.
// Routing an active marquee before sibling controls (minimap, loop handles)
// is widget policy; the machine guarantees the finalize once routed.

#include "TimelineMarquee.h"

#include <cmath>
#include <iostream>

using Aestra::Components::MarqueeButton;
using Aestra::Components::MarqueeEventKind;
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
    std::cout << "  [1/8] Begin: marquee-button press inside the track area... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    check(!drag.begin(false, kLeft, 100.0f, 200.0f, true), "release does not begin");
    check(!drag.begin(true, 0, 100.0f, 200.0f, true), "no-button press does not begin");
    check(!drag.begin(true, kLeft, 100.0f, 200.0f, false), "press outside the track area does not begin");
    check(drag.begin(true, kLeft, 100.0f, 200.0f, true), "press inside the track area begins");
    check(drag.active(), "drag active after begin");
    check(!drag.begin(true, kRight, 150.0f, 220.0f, true), "a second press cannot restart an active drag");
    check(drag.startX() == 100.0f && drag.startY() == 200.0f, "start pinned at the begin point");
    check(drag.endX() == 100.0f && drag.endY() == 200.0f, "endpoint starts at the begin point");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testOwnershipUntilRelease() {
    std::cout << "  [2/8] Active marquee owns every event until its button releases... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 100.0f, 200.0f, true);
    check(drag.ownsEvent(), "owns events while active");
    drag.onEvent(MarqueeEventKind::Move, kLeft, 140.0f, 260.0f);
    check(drag.ownsEvent(), "still owns events after moves");
    check(!drag.onEvent(MarqueeEventKind::Press, kRight, 150.0f, 260.0f), "foreign press: consumed, no finalize");
    check(!drag.onEvent(MarqueeEventKind::Release, kRight, 160.0f, 270.0f), "foreign release: consumed, no finalize");
    check(drag.active(), "drag survives foreign press and release");
    check(drag.onEvent(MarqueeEventKind::Release, kLeft, 170.0f, 280.0f), "own-button release finalizes");
    drag.finalize();
    check(!drag.ownsEvent(), "no longer owns events after finalize");
    check(!drag.onEvent(MarqueeEventKind::Release, kLeft, 0.0f, 0.0f), "finalize refused when inactive");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testForeignEventsDoNotMoveTheBand() {
    std::cout << "  [3/8] Foreign press/release never moves the endpoint... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 100.0f, 200.0f, true);
    drag.onEvent(MarqueeEventKind::Move, kLeft, 140.0f, 260.0f); // move to (140, 260)

    // A right-button press + release mid-drag must not touch the band.
    drag.onEvent(MarqueeEventKind::Press, kRight, 999.0f, 999.0f);
    drag.onEvent(MarqueeEventKind::Release, kRight, 888.0f, 888.0f);
    check(drag.endX() == 140.0f && drag.endY() == 260.0f, "endpoint unchanged by foreign press/release");

    // The initiating button's release is part of the gesture.
    check(drag.onEvent(MarqueeEventKind::Release, kLeft, 200.0f, 300.0f), "own-button release finalizes");
    check(drag.endX() == 200.0f && drag.endY() == 300.0f, "release position is the final endpoint");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testNonMoveKindsCannotMoveTheBand() {
    std::cout << "  [4/8] Wheel-class (Other) and Press kinds never move the band... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 100.0f, 200.0f, true);
    drag.onEvent(MarqueeEventKind::Move, kLeft, 140.0f, 260.0f);

    // Wheel, enter/leave and synthetic events classify as Other (the widget
    // maps NUIMouseEventType::Scroll there) — they must be consumed without
    // replacing the endpoint, even though their flag shape matches a move.
    drag.onEvent(MarqueeEventKind::Other, kLeft, 999.0f, 999.0f);
    drag.onEvent(MarqueeEventKind::Press, kLeft, 999.0f, 999.0f);
    check(drag.endX() == 140.0f && drag.endY() == 260.0f, "endpoint unchanged by Other/Press kinds");
    check(drag.active(), "drag survives non-move kinds");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testMoveIsPureEndpointUpdate() {
    std::cout << "  [5/8] Move mutates only the endpoint (no storm surface)... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 100.0f, 200.0f, true);
    const bool wasActive = drag.active();
    const MarqueeButton wasButton = drag.button();
    const float wasStartX = drag.startX();
    const float wasStartY = drag.startY();

    for (int i = 0; i < 1000; ++i) { // the drag of a long select: many moves
        drag.onEvent(MarqueeEventKind::Move, kLeft, 100.0f + static_cast<float>(i),
                     200.0f + static_cast<float>(i) * 0.5f);
    }

    check(drag.active() == wasActive && drag.button() == wasButton, "activation state untouched by moves");
    check(drag.startX() == wasStartX && drag.startY() == wasStartY, "start corner untouched by moves");
    check(std::abs(drag.endX() - 1099.0f) < 0.001f && std::abs(drag.endY() - 699.5f) < 0.001f,
          "endpoint tracks the last position only");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testUpdateIgnoredWhenInactive() {
    std::cout << "  [6/8] Events on an inactive machine are no-ops... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    drag.onEvent(MarqueeEventKind::Move, kLeft, 50.0f, 60.0f);
    drag.onEvent(MarqueeEventKind::Press, kLeft, 50.0f, 60.0f);
    drag.onEvent(MarqueeEventKind::Release, kLeft, 50.0f, 60.0f);
    check(!drag.active() && drag.endX() == 0.0f && drag.endY() == 0.0f, "inactive events change nothing");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testRectNormalization() {
    std::cout << "  [7/8] Rectangle normalizes regardless of drag direction... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    drag.begin(true, kLeft, 300.0f, 500.0f, true);
    drag.onEvent(MarqueeEventKind::Move, kLeft, 120.0f, 180.0f); // dragged up-left
    check(std::abs(drag.rectMinX() - 120.0f) < 0.001f && std::abs(drag.rectMaxX() - 300.0f) < 0.001f,
          "x extent normalized");
    check(std::abs(drag.rectMinY() - 180.0f) < 0.001f && std::abs(drag.rectMaxY() - 500.0f) < 0.001f,
          "y extent normalized");
    check(std::abs(drag.rectWidth() - 180.0f) < 0.001f && std::abs(drag.rectHeight() - 320.0f) < 0.001f,
          "extents positive in any direction");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testRestartAfterFinalize() {
    std::cout << "  [8/8] A finalize allows an immediate new drag... ";
    const int before = g_failures;
    TimelineMarqueeDrag drag;
    drag.begin(true, kRight, 10.0f, 20.0f, true);
    drag.onEvent(MarqueeEventKind::Move, kRight, 30.0f, 40.0f);
    drag.finalize();
    check(drag.begin(true, kLeft, 100.0f, 200.0f, true), "new drag begins after finalize");
    check(drag.button() == kLeft && drag.startX() == 100.0f, "new drag carries its own button and start");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

} // namespace

int main() {
    std::cout << "TimelineMarqueeTest (#847)\n";
    const struct {
        const char* name;
        void (*fn)();
    } tests[] = {
        {"begin rules", testBeginRules},
        {"ownership until release", testOwnershipUntilRelease},
        {"foreign events do not move the band", testForeignEventsDoNotMoveTheBand},
        {"non-move kinds cannot move the band", testNonMoveKindsCannotMoveTheBand},
        {"move is a pure endpoint update", testMoveIsPureEndpointUpdate},
        {"inactive events are no-ops", testUpdateIgnoredWhenInactive},
        {"rectangle normalization", testRectNormalization},
        {"restart after finalize", testRestartAfterFinalize},
    };
    for (const auto& test : tests) {
        const int before = g_failures;
        test.fn();
        if (g_failures > before) {
            std::cerr << "TEST FAILED: " << test.name << "\n";
        }
    }
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All timeline marquee tests passed\n";
    return 0;
}
