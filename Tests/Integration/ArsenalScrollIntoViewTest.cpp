// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// After "+ Add a sound" the list scrolls so the new row is fully in view, and when it is the last row the add line
// under it too. Pure header, no libraries.

#include "ArsenalScrollIntoView.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

using Aestra::ArsenalScrollIntoView::scrollToShow;

void testAlreadyVisibleStaysPut() {
    require(scrollToShow(100.0f, 300.0f, 150.0f, 200.0f, 8.0f, 1000.0f) == 100.0f, "a row in view does not move the list");
}

void testBelowTheViewScrollsUpJustEnough() {
    // visible content is [100, 400); the row is [420, 460): scroll so its bottom (plus the margin) is the view's bottom.
    require(scrollToShow(100.0f, 300.0f, 420.0f, 460.0f, 8.0f, 1000.0f) == 168.0f, "a row below the view scrolls into view");
}

void testAboveTheViewScrollsBack() {
    require(scrollToShow(100.0f, 300.0f, 40.0f, 80.0f, 8.0f, 1000.0f) == 32.0f, "a row above the view scrolls back to it");
}

void testNeverPastTheContent() {
    require(scrollToShow(0.0f, 300.0f, 900.0f, 990.0f, 40.0f, 1000.0f) == 700.0f, "the list never scrolls past its content");
    require(scrollToShow(0.0f, 300.0f, 10.0f, 20.0f, 8.0f, 200.0f) == 0.0f, "short content does not scroll at all");
}

void testRowTallerThanTheViewShowsItsTop() {
    require(scrollToShow(0.0f, 100.0f, 300.0f, 500.0f, 8.0f, 1000.0f) == 292.0f, "a row taller than the view shows its top");
}

} // namespace

int main() {
    testAlreadyVisibleStaysPut();
    testBelowTheViewScrollsUpJustEnough();
    testAboveTheViewScrollsBack();
    testNeverPastTheContent();
    testRowTallerThanTheViewShowsItsTop();
    std::cout << "ArsenalScrollIntoViewTest: all checks passed\n";
    return 0;
}
