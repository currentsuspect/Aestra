// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The mixer's plugin finder had no scroll offset, so with ~218 scanned plugins most rows were unreachable and
// the wheel did nothing. These tests pin the pure scroll math in AestraUI/Helpers/DropdownScroll.h: clamping,
// the wheel convention, ensure-visible for keyboard selection, and hit-testing that honours the offset.

#include "../../AestraUI/Helpers/DropdownScroll.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace AestraUI::DropdownScroll;

namespace {
int g_failures = 0;
void expect(bool c, const std::string& m) {
    if (!c) { std::cerr << "[FAIL] " << m << "\n"; ++g_failures; }
}
bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// 3 headers (22) + items (34): one category header then 10 rows => content 22 + 340 = 362.
std::vector<Span> makeSpans(int rows) {
    std::vector<Span> s{{0.0f, 22.0f}};
    float y = 22.0f;
    for (int i = 0; i < rows; ++i) { s.push_back({y, 34.0f}); y += 34.0f; }
    return s;
}
} // namespace

int main() {
    const float viewport = 206.0f; // MAX_LIST_H 280 minus nothing: arbitrary, smaller than content
    const float content = 362.0f;

    // maxOffset / clamp
    expect(near(maxOffset(content, viewport), 156.0f), "maxOffset = content - viewport");
    expect(near(maxOffset(100.0f, viewport), 0.0f), "short list never scrolls");
    expect(near(clampOffset(-10.0f, content, viewport), 0.0f), "clamp low");
    expect(near(clampOffset(9999.0f, content, viewport), 156.0f), "clamp high");
    expect(near(clampOffset(50.0f, 100.0f, viewport), 0.0f), "clamp on short list is 0");

    // wheel: down (negative delta) scrolls content up (offset grows); up shrinks; clamped
    expect(near(applyWheel(0.0f, -1.0f, 40.0f, content, viewport), 40.0f), "wheel down one notch");
    expect(near(applyWheel(40.0f, 1.0f, 40.0f, content, viewport), 0.0f), "wheel up one notch");
    expect(near(applyWheel(0.0f, 1.0f, 40.0f, content, viewport), 0.0f), "wheel up at top stays 0");
    expect(near(applyWheel(150.0f, -3.0f, 40.0f, content, viewport), 156.0f), "wheel down clamps at max");
    expect(near(applyWheel(0.0f, -5.0f, 40.0f, 100.0f, viewport), 0.0f), "wheel on short list is a no-op");

    // ensureVisible
    expect(near(ensureVisible(0.0f, 22.0f, 34.0f, content, viewport), 0.0f), "row already visible: no move");
    expect(near(ensureVisible(0.0f, 300.0f, 34.0f, content, viewport), 128.0f), "row below: bottom aligns");
    expect(near(ensureVisible(156.0f, 22.0f, 34.0f, content, viewport), 22.0f), "row above: top aligns");
    expect(near(ensureVisible(0.0f, 328.0f, 34.0f, content, viewport), 156.0f), "last row: clamps to max");

    // hit test honours the offset and the viewport
    const auto spans = makeSpans(10);
    expect(hitTest(spans, 5.0f, 0.0f, viewport) == 0, "offset 0: header under y=5");
    expect(hitTest(spans, 30.0f, 0.0f, viewport) == 1, "offset 0: first row under y=30");
    expect(hitTest(spans, 30.0f, 100.0f, viewport) == 4, "offset 100: y=30 is content 130 => row index 4");
    expect(hitTest(spans, -1.0f, 100.0f, viewport) == -1, "above viewport is not hittable");
    expect(hitTest(spans, viewport, 100.0f, viewport) == -1, "below viewport (footer) is not hittable");
    expect(hitTest(spans, 205.0f, 156.0f, viewport) == 10, "bottom row reachable at max offset");

    // thumb
    expect(near(thumb(0.0f, 100.0f, viewport, 190.0f).height, 0.0f), "no thumb when it all fits");
    const Thumb top = thumb(0.0f, content, viewport, 190.0f);
    const Thumb bot = thumb(156.0f, content, viewport, 190.0f);
    expect(top.height > 0.0f && near(top.top, 0.0f), "thumb at top");
    expect(near(bot.top + bot.height, 190.0f), "thumb at bottom touches the track end");

    if (g_failures == 0) std::cout << "DropdownScrollTest: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
