// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-X2b criterion 4: "debug tooling can explain exactly why a widget ended up
// where it did", on a real surface — the timeline, laid out by the same
// functions TrackManagerUI calls — not on a synthetic tree.
//
// What is proven here:
//   - recording never changes the geometry it records;
//   - every placed rect has a step carrying the algorithm's own rule and numbers;
//   - "which constraint reached it, from which ancestor" walks back to the surface;
//   - the per-pass account (SPEC-003 §10.2) marks only what actually moved, so a
//     scroll changes rows and nothing else (§7.3 propagation check);
//   - the §10.1 conditions this layer can see are reported: clamps, a fixed rect
//     outside its space, non-finite geometry; scrolled-out rows are not problems;
//   - tracing is off unless AESTRA_LAYOUT_TRACE names the surface.
//
// Header-only, links nothing.

#include "TrackManagerUILayout.h"

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

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool sameRect(const AestraUI::Layout::NUILocalRect& a, const AestraUI::Layout::NUILocalRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

using namespace AestraUI::Layout;
using namespace Aestra::Audio;

constexpr float kControls = 204.0f;
constexpr float kRowHeight = 38.0f;

/** One traced timeline pass, the way TrackManagerUI::layoutTracks() runs it. */
TimelineLayout tracedPass(NUILayoutRecorder& rec, float width, float height, std::size_t rows, float scroll) {
    rec.beginPass(NUIWindowPoint(100.0f, 200.0f));
    const TimelineLayout layout = resolveTimelineLayout(width, height, kControls, &rec);
    arrangeTimelineRows(layout, kRowHeight, 0.0f, rows, scroll, &rec);
    return layout;
}

void testRecordingDoesNotChangeGeometry() {
    NUILayoutRecorder rec("timeline");
    const auto traced = resolveTimelineLayout(1280.0f, 720.0f, kControls, &rec);
    const auto plain = resolveTimelineLayout(1280.0f, 720.0f, kControls);
    check(sameRect(traced.minimap, plain.minimap) && sameRect(traced.rows, plain.rows) &&
              sameRect(traced.scrollbar, plain.scrollbar),
          "regions are identical with and without a recorder");
    const auto tracedRows = arrangeTimelineRows(plain, kRowHeight, 0.0f, 20, 57.0f, &rec);
    const auto plainRows = arrangeTimelineRows(plain, kRowHeight, 0.0f, 20, 57.0f);
    bool same = tracedRows.size() == plainRows.size();
    for (std::size_t i = 0; same && i < plainRows.size(); ++i) {
        same = sameRect(tracedRows[i].rect, plainRows[i].rect) && tracedRows[i].visible == plainRows[i].visible;
    }
    check(same, "rows are identical with and without a recorder");
}

void testEveryPlacedRectIsExplained() {
    NUILayoutRecorder rec("timeline");
    const auto layout = tracedPass(rec, 1280.0f, 720.0f, 14, 0.0f);

    const auto* row = rec.find("timeline.rows[3]");
    check(row != nullptr, "row 3 has a step");
    if (row) {
        check(contains(row->rule, "scrolling stack (vertical), item 3 of 14"), "the rule names the algorithm and item");
        check(contains(row->rule, "52 + 3 x (38 + 0) - scroll 0 = 166"), "the rule shows the arithmetic it did");
        check(sameRect(row->available, layout.rows), "row 3 was given the row viewport");
        check(row->from == "timeline.body.leading", "row 3's space came from the body split");
        check(row->requestsHeight && row->requestedHeight == kRowHeight && !row->requestsWidth,
              "a row asks for its height and fills the width");
    }

    const auto* minimap = rec.find("timeline.minimapRow.leading");
    check(minimap && sameRect(minimap->resolved, layout.minimap), "the minimap's rect is a recorded step");
    const auto* gutter = rec.find("timeline.body.trailing");
    check(gutter && sameRect(gutter->resolved, layout.scrollbar), "the scrollbar gutter's rect is a recorded step");

    const std::string why = rec.explain("timeline.rows[3]");
    check(contains(why, "WHY did timeline.rows[3] end up here?"), "explain answers in SPEC-003 §10.2's form");
    check(contains(why, "Requested: fill × 38"), "explain quotes what was asked for");
    check(contains(why, "Resolved:  0,166 1270×38  (Local)   = 100,366 1270×38  (Window)"),
          "explain quotes both spaces, converted by the surface's origin");
    check(contains(why, "↑ timeline.body.leading") && contains(why, "↑ timeline.bands.trailing"),
          "explain walks the chain of spaces back to the surface");
    check(contains(rec.explain("timeline.nope"), "No step named timeline.nope"), "an unknown name says so");
}

void testPassAccountMarksOnlyWhatMoved() {
    NUILayoutRecorder rec("timeline");
    tracedPass(rec, 1280.0f, 720.0f, 14, 0.0f);
    check(rec.passChangedAnything(), "the first pass adds everything");

    tracedPass(rec, 1280.0f, 720.0f, 14, 0.0f);
    check(!rec.passChangedAnything(), "an identical pass changes nothing");

    tracedPass(rec, 1280.0f, 720.0f, 14, 38.0f);
    bool regionsStill = true;
    bool rowsMoved = true;
    for (const auto& c : rec.changes()) {
        const bool isRow = contains(c.first, ".rows[");
        if (!isRow && c.second != NUILayoutChange::Unchanged) {
            regionsStill = false;
        }
        if (isRow && c.second != NUILayoutChange::Changed) {
            rowsMoved = false;
        }
    }
    check(regionsStill, "scrolling leaves every region unchanged (no stray propagation)");
    check(rowsMoved, "scrolling moves every row");

    tracedPass(rec, 1280.0f, 720.0f, 12, 38.0f);
    const std::string summary = rec.passSummary();
    check(contains(summary, "Layout pass #4 timeline: 0 changed, 0 added, 2 removed"), "removed rows are counted");
    check(contains(summary, "timeline.rows[13]  removed"), "and named");
}

void testProblemsAreReported() {
    NUILayoutRecorder rec("timeline");
    tracedPass(rec, 1280.0f, 720.0f, 40, 0.0f);
    check(rec.problems().empty(), "a normal timeline with rows scrolled out of view has no problems");

    tracedPass(rec, 150.0f, 30.0f, 3, 0.0f);
    bool bandClamp = false;
    for (const auto& p : rec.problems()) {
        if (p.step == "timeline.bands.leading" && contains(p.what, "height 52 clamped to 30")) {
            bandClamp = true;
        }
    }
    check(bandClamp, "a component shorter than the time band reports the clamp, with both numbers");

    NUILayoutRecorder row("titlebar");
    row.beginPass(NUIWindowPoint(0.0f, 0.0f));
    row.scope("buttons");
    arrangeTrailingRow(NUILocalRect(0.0f, 0.0f, 40.0f, 28.0f), {20.0f, 20.0f, 20.0f}, 20.0f, 4.0f, 4.0f, &row);
    bool outside = false;
    for (const auto& p : row.problems()) {
        if (p.step == "titlebar.buttons[2]" && contains(p.what, "outside the space it was given")) {
            outside = true;
        }
    }
    check(outside, "a button pushed past the title bar's leading edge is reported, not silently placed");

    NUILayoutRecorder bad("bad");
    bad.beginPass(NUIWindowPoint(0.0f, 0.0f));
    bad.scope("x");
    splitVertical(NUILocalRect(0.0f, 0.0f, 100.0f, std::numeric_limits<float>::quiet_NaN()), 10.0f, &bad);
    bool nonFinite = false;
    for (const auto& p : bad.problems()) {
        if (p.step == "bad.x.trailing" && contains(p.what, "non-finite")) {
            nonFinite = true;
        }
    }
    check(nonFinite, "NaN geometry is reported on the rect that carries it (the trailing part fills NaN - 10)");
}

void testTracingIsOffUnlessNamed() {
    check(!layoutTraceEnabledFor(nullptr, "timeline"), "unset: off");
    check(!layoutTraceEnabledFor("", "timeline"), "empty: off");
    check(layoutTraceEnabledFor("all", "timeline") && layoutTraceEnabledFor("1", "mixer"), "all / 1: on");
    check(layoutTraceEnabledFor("mixer,timeline", "timeline"), "named in a list: on");
    check(!layoutTraceEnabledFor("mixer,timelines", "timeline"), "a near-miss name: off");
    check(layoutTraceEnabledFor("window.Mixer", "window.Mixer"), "window panels are addressed by title");
}

} // namespace

int main() {
    testRecordingDoesNotChangeGeometry();
    testEveryPlacedRectIsExplained();
    testPassAccountMarksOnlyWhatMoved();
    testProblemsAreReported();
    testTracingIsOffUnlessNamed();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "NUILayoutExplainTest: all checks passed\n";
    return 0;
}
