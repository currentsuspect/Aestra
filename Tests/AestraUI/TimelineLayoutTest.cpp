// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-X2b phase 4: TrackManagerUI's timeline regions and track rows, resolved by
// the layout system (TrackManagerUILayout.h), checked against the hand-written
// arithmetic they replace.
//
// The reference functions below are copies of what layoutTracks(),
// getTrackAtPosition(), the drop preview and the scroll-range code computed
// before the migration, kept here so the expected values never come from the
// code under test. Real constants: track controls 204 px (NUIThemeSystem.h),
// row height 38, spacing 0 (TrackManagerUI.h). Spacing 6 is checked too, so a
// non-zero gap is covered before anything ships one.
//
// Header-only, links nothing: AESTRA_CI=ON disables the UI targets, and a test
// that needed them would be skipped rather than failed.

#include "TrackManagerUILayout.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

bool nearly(float a, float b) { return std::fabs(a - b) <= 1e-3f; }

bool sameRect(const AestraUI::Layout::NUILocalRect& r, float x, float y, float w, float h) {
    return nearly(r.x, x) && nearly(r.y, y) && nearly(r.width, w) && nearly(r.height, h);
}

using namespace Aestra::Audio;

constexpr float kControls = 204.0f;
constexpr float kRowHeight = 38.0f;

// --- pre-migration arithmetic, component-relative (bounds origin 0,0) -------

struct RefRect { float x, y, w, h; };

RefRect refMinimap(float width) {
    const float minimapWidth = std::max(0.0f, width - kTimelineScrollbarWidth - kControls);
    return {kControls, 0.0f, minimapWidth, kTimelineMinimapHeight};
}

RefRect refScrollbar(float width, float height) {
    const float viewportHeight = std::max(0.0f, height - kTimelineTimeBandHeight);
    return {std::max(0.0f, width - kTimelineScrollbarWidth), kTimelineTimeBandHeight, kTimelineScrollbarWidth,
            viewportHeight};
}

RefRect refRow(float width, std::size_t i, float spacing, float scroll) {
    const float yPos = kTimelineTimeBandHeight + (static_cast<float>(i) * (kRowHeight + spacing)) - scroll;
    return {0.0f, yPos, timelineTrackRowWidth(width), kRowHeight};
}

int refSlot(float localY, float spacing, float scroll) {
    const float relativeY = localY - kTimelineTimeBandHeight + scroll;
    if (relativeY < 0) {
        return -1;
    }
    return static_cast<int>(relativeY / (kRowHeight + spacing));
}

// -----------------------------------------------------------------------------

void testRegionsMatchLayoutTracks() {
    for (float width : {300.0f, 1280.0f, 1919.5f}) {
        for (float height : {120.0f, 720.0f}) {
            const auto layout = resolveTimelineLayout(width, height, kControls);
            const auto mm = refMinimap(width);
            const auto sb = refScrollbar(width, height);
            const std::string at = " at " + std::to_string(width) + "x" + std::to_string(height);
            check(sameRect(layout.minimap, mm.x, mm.y, mm.w, mm.h), "minimap matches layoutTracks()" + at);
            check(sameRect(layout.scrollbar, sb.x, sb.y, sb.w, sb.h), "scrollbar matches layoutTracks()" + at);
            check(nearly(layout.rows.y, kTimelineTimeBandHeight), "rows start under the time band" + at);
            check(nearly(layout.rows.width, timelineTrackRowWidth(width)), "rows end at the scrollbar" + at);
        }
    }
}

void testRowsMatchLayoutTracks() {
    for (float spacing : {0.0f, 6.0f}) {
        for (float scroll : {0.0f, 57.0f, 400.0f}) {
            const auto layout = resolveTimelineLayout(1280.0f, 720.0f, kControls);
            const auto rows = arrangeTimelineRows(layout, kRowHeight, spacing, 24, scroll);
            check(rows.size() == 24, "one row per track");
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto ref = refRow(1280.0f, i, spacing, scroll);
                check(sameRect(rows[i].rect, ref.x, ref.y, ref.w, ref.h),
                      "row " + std::to_string(i) + " matches layoutTracks() (spacing " + std::to_string(spacing) +
                          ", scroll " + std::to_string(scroll) + ")");
                check(nearly(timelineRowTopY(layout, kRowHeight, spacing, static_cast<int>(i), scroll), ref.y),
                      "timelineRowTopY agrees with the arranged row " + std::to_string(i));
            }
        }
    }
}

void testRowVisibilityAgainstTheViewport() {
    // 720 tall: the row viewport runs from y=52 to 720. Scrolled 400 px, row i
    // starts at 52 - 400 + 38i. Row 9 (-6..32) is entirely under the time band;
    // row 10 (32..70) straddles the band's edge; row 28 (716..754) starts just
    // inside the bottom; row 29 (754) starts below it.
    const auto layout = resolveTimelineLayout(1280.0f, 720.0f, kControls);
    const auto rows = arrangeTimelineRows(layout, kRowHeight, 0.0f, 30, 400.0f);
    check(!rows[9].visible, "row 9 has scrolled entirely under the time band");
    check(rows[10].visible, "row 10 straddles the band's edge and counts as visible");
    check(rows[28].visible, "row 28 starts inside the bottom edge");
    check(!rows[29].visible, "row 29 starts below the row viewport");
}

void testSlotsMatchTheHandWrittenHitTests() {
    const auto layout = resolveTimelineLayout(1280.0f, 720.0f, kControls);
    for (float spacing : {0.0f, 6.0f}) {
        for (float scroll : {0.0f, 57.0f}) {
            for (float y = 0.0f; y < 720.0f; y += 0.5f) {
                const int got = timelineRowSlotAt(layout, kRowHeight, spacing, scroll, y);
                const int want = refSlot(y, spacing, scroll);
                if (got != want) {
                    check(false, "slot at y=" + std::to_string(y) + " is " + std::to_string(got) + ", hand-written " +
                                     std::to_string(want));
                    return;
                }
            }
        }
    }
    check(timelineRowSlotAt(layout, kRowHeight, 0.0f, 0.0f, 10.0f) == -1, "the time band is above every row");
    check(timelineRowSlotAt(layout, kRowHeight, 0.0f, 0.0f, 52.0f) == 0, "the first pixel under the band is row 0");
    check(timelineRowSlotAt(layout, kRowHeight, 0.0f, 0.0f, 52.0f + 38.0f * 50.0f) == 50,
          "below the last row the slot keeps counting (drops can target new rows)");
}

void testScrollRange() {
    const auto layout = resolveTimelineLayout(1280.0f, 720.0f, kControls);
    check(nearly(timelineRowsContentHeight(kRowHeight, 0.0f, 30), 30.0f * 38.0f),
          "content height is rows x height when rows touch");
    check(nearly(timelineRowsContentHeight(kRowHeight, 6.0f, 3), 38.0f * 3.0f + 6.0f * 2.0f),
          "no spacing after the last row");
    check(nearly(timelineRowsContentHeight(kRowHeight, 6.0f, 0), 0.0f), "no rows, no content");
    check(nearly(timelineMaxVerticalScroll(layout, kRowHeight, 0.0f, 30), 30.0f * 38.0f - 668.0f),
          "max scroll is the content beyond the 668 px row viewport");
    check(nearly(timelineMaxVerticalScroll(layout, kRowHeight, 0.0f, 5), 0.0f), "content that fits does not scroll");
}

void testNarrowAndShortComponentsStayNonNegative() {
    // Narrower than controls + gutter, shorter than the time band: nothing
    // goes negative, and the rows/scrollbar never overlap.
    const auto layout = resolveTimelineLayout(150.0f, 30.0f, kControls);
    check(layout.minimap.width >= 0.0f && layout.minimap.height >= 0.0f, "minimap never negative");
    check(layout.rows.height >= 0.0f && layout.scrollbar.height >= 0.0f, "row viewport never negative");
    check(nearly(layout.rows.right(), layout.scrollbar.x), "rows end exactly where the scrollbar starts");
}

} // namespace

int main() {
    testRegionsMatchLayoutTracks();
    testRowsMatchLayoutTracks();
    testRowVisibilityAgainstTheViewport();
    testSlotsMatchTheHandWrittenHitTests();
    testScrollRange();
    testNarrowAndShortComponentsStayNonNegative();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "TimelineLayoutTest: all checks passed\n";
    return 0;
}
