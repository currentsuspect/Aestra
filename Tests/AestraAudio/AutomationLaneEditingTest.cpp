// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-A2: how an automation lane reads and draws several curves. Each target has
// its own value range (Pan is -1…+1, as the engine applies it), and a press can
// reach the points of every visible curve, the edited curve's first.

#include "AutomationLaneEditing.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

void testValueRanges() {
    check(near(automationValueAtHeight(AutomationTarget::Volume, 0.0), 0.0), "volume: bottom is silence");
    check(near(automationValueAtHeight(AutomationTarget::Volume, 1.0), 1.0), "volume: top is unity");
    check(near(automationValueAtHeight(AutomationTarget::Pan, 0.0), -1.0), "pan: bottom is hard left");
    check(near(automationValueAtHeight(AutomationTarget::Pan, 0.5), 0.0), "pan: the middle is centre");
    check(near(automationValueAtHeight(AutomationTarget::Pan, 1.0), 1.0), "pan: top is hard right");
    check(near(automationHeightOfValue(AutomationTarget::Pan, -0.5), 0.25), "pan draws on its own range");
    check(near(automationHeightOfValue(AutomationTarget::Volume, 0.25), 0.25), "volume draws on 0…1");
    for (double f : {0.0, 0.2, 0.5, 0.9, 1.0}) {
        check(near(automationHeightOfValue(AutomationTarget::Pan, automationValueAtHeight(AutomationTarget::Pan, f)), f),
              "pan: height -> value -> height round-trips");
    }
    check(near(automationValueAtHeight(AutomationTarget::Pan, 1.7), 1.0) &&
              near(automationHeightOfValue(AutomationTarget::Volume, -3.0), 0.0),
          "outside the lane clamps to the range");
}

void testCurvesByTarget() {
    std::vector<AutomationCurve> curves;
    check(automationCurveIndexFor(curves, AutomationTarget::Volume) == -1, "an empty lane has no volume curve");
    curves.push_back(makeAutomationCurve(AutomationTarget::Pan));
    curves.push_back(makeAutomationCurve(AutomationTarget::Volume));
    check(automationCurveIndexFor(curves, AutomationTarget::Volume) == 1, "volume is found where it is, not at 0");
    check(automationCurveIndexFor(curves, AutomationTarget::Pan) == 0, "and pan likewise");
    check(curves[0].getDefaultValue() == 0.0f, "a new pan curve rests at centre");
    check(curves[1].getDefaultValue() == 1.0f, "a new volume curve rests at unity");
}

void testHitReachesEveryCurve() {
    std::vector<AutomationCurve> curves{makeAutomationCurve(AutomationTarget::Volume),
                                        makeAutomationCurve(AutomationTarget::Pan)};
    curves[0].addPoint(1.0, 1.0f, 24000.0); // volume point at the top
    curves[1].addPoint(1.0, 0.0f, 24000.0); // pan point at centre
    curves[1].addPoint(4.0, -1.0f, 24000.0); // pan point at the bottom
    // 100 px per beat, lane 0..100 px high.
    auto screenOf = [&](int c, const AutomationPoint& p) {
        const auto t = curves[static_cast<size_t>(c)].getAutomationTarget();
        return std::pair<float, float>{static_cast<float>(p.beat * 100.0),
                                       static_cast<float>((1.0 - automationHeightOfValue(t, p.value)) * 100.0)};
    };

    auto hit = hitAutomationPoint(curves, 0, 100.0f, 50.0f, 12.0f, screenOf);
    check(hit.curve == 1 && hit.point == 0, "a press on a pan point reaches pan while volume is edited");
    hit = hitAutomationPoint(curves, 0, 400.0f, 100.0f, 12.0f, screenOf);
    check(hit.curve == 1 && hit.point == 1, "pan's -1 is drawn at the bottom and hit there");
    hit = hitAutomationPoint(curves, 0, 100.0f, 0.0f, 12.0f, screenOf);
    check(hit.curve == 0 && hit.point == 0, "the volume point is still hit");
    check(!hitAutomationPoint(curves, 0, 250.0f, 50.0f, 12.0f, screenOf).found(), "empty space hits nothing");

    // Where two curves share a spot, the edited one wins.
    curves[0].addPoint(4.0, 0.0f, 24000.0); // volume 0 is also drawn at the bottom
    hit = hitAutomationPoint(curves, 1, 400.0f, 100.0f, 12.0f, screenOf);
    check(hit.curve == 1, "overlapping points: the edited curve (pan) wins");
    hit = hitAutomationPoint(curves, 0, 400.0f, 100.0f, 12.0f, screenOf);
    check(hit.curve == 0, "and with volume edited, volume wins");
}

void testSelection() {
    std::vector<AutomationCurve> curves{makeAutomationCurve(AutomationTarget::Volume),
                                        makeAutomationCurve(AutomationTarget::Pan)};
    curves[0].addPoint(0.0, 1.0f, 24000.0);
    curves[0].addPoint(4.0, 0.5f, 24000.0);
    curves[1].addPoint(2.0, 0.0f, 24000.0);
    selectAutomationPoint(curves, 0, 0, false);
    check(curves[0].getPoints()[0].selected && selectedAutomationPointCount(curves) == 1, "a press selects one point");
    selectAutomationPoint(curves, 1, 0, true);
    check(selectedAutomationPointCount(curves) == 2, "Shift adds a point, across curves");
    selectAutomationPoint(curves, 0, 0, true);
    check(!curves[0].getPoints()[0].selected && selectedAutomationPointCount(curves) == 1, "Shift again takes it out");
    selectAutomationPoint(curves, 0, 1, false);
    check(curves[0].getPoints()[1].selected && selectedAutomationPointCount(curves) == 1,
          "a plain press replaces the selection on every curve");
    selectAutomationPoint(curves, 0, 9, false);
    check(selectedAutomationPointCount(curves) == 1, "an index past the end changes nothing");
}

// V82-T1: what an automation row's header says.
void testHeaderLabels() {
    check(automationTargetLabel(AutomationTarget::Volume) == "Volume", "header names volume");
    check(automationTargetLabel(AutomationTarget::Pan) == "Pan", "header names pan");
    check(automationValueLabel(AutomationTarget::Volume, 1.0) == "0.0 dB", "unity volume reads 0.0 dB");
    check(automationValueLabel(AutomationTarget::Volume, 0.5) == "-6.0 dB", "half gain reads -6.0 dB");
    check(automationValueLabel(AutomationTarget::Volume, 0.7) == "-3.1 dB", "0.7 reads -3.1 dB (board 8)");
    check(automationValueLabel(AutomationTarget::Volume, 0.0) == "-\xE2\x88\x9E dB", "silence reads -inf dB");
    check(automationValueLabel(AutomationTarget::Pan, 0.0) == "C", "centre pan reads C");
    check(automationValueLabel(AutomationTarget::Pan, -0.4) == "L 40", "pan left in percent");
    check(automationValueLabel(AutomationTarget::Pan, 0.25) == "R 25", "pan right in percent");
}

} // namespace

// Screen maths is float; a thousandth of a pixel is far below anything drawn.
bool nearf(double a, double b) { return std::fabs(a - b) < 1e-4; }

void testVisibleSegmentSpan() {
    // Lane from x=100 to x=500.
    auto span = visibleSegmentSpan(150.0f, 450.0f, 100.0f, 500.0f);
    check(nearf(span.t0, 0.0) && nearf(span.t1, 1.0), "a segment inside the lane is all visible");
    span = visibleSegmentSpan(-300.0f, 700.0f, 100.0f, 500.0f);
    check(nearf(span.t0, 0.4) && nearf(span.t1, 0.8), "a segment that spans the lane shows only its middle: [0.4, 0.8]");
    check(nearf(-300.0f + (700.0f - -300.0f) * static_cast<float>(span.t0), 100.0) &&
              nearf(-300.0f + (700.0f - -300.0f) * static_cast<float>(span.t1), 500.0),
          "and that middle starts and ends exactly on the lane's edges");
    span = visibleSegmentSpan(-200.0f, 300.0f, 100.0f, 500.0f);
    check(nearf(span.t0, 0.6) && nearf(span.t1, 1.0), "a segment starting off-screen to the left is cut at the lane's left edge");
    span = visibleSegmentSpan(300.0f, 900.0f, 100.0f, 500.0f);
    check(nearf(span.t0, 0.0) && nearf(span.t1, 1.0 / 3.0), "a segment running off to the right is cut at the lane's right edge");
    check(visibleSegmentSpan(-400.0f, -50.0f, 100.0f, 500.0f).empty(), "a segment wholly left of the lane is empty");
    check(visibleSegmentSpan(600.0f, 900.0f, 100.0f, 500.0f).empty(), "a segment wholly right of the lane is empty");
    check(visibleSegmentSpan(300.0f, 300.0f, 100.0f, 500.0f).empty() == false, "a zero-length segment inside the lane is kept");
    check(visibleSegmentSpan(50.0f, 50.0f, 100.0f, 500.0f).empty(), "and one outside it is not");
    // Zoomed in until the points are a million pixels apart: the lane still gets its slice.
    span = visibleSegmentSpan(-500000.0f, 500000.0f, 100.0f, 500.0f);
    check(!span.empty() && nearf(span.t1 - span.t0, 400.0 / 1000000.0), "zoomed far in, the visible slice is tiny but not empty");
}

void testHeaderReadout() {
    AutomationCurve curve = makeAutomationCurve(AutomationTarget::Volume);
    constexpr double kSamplesPerBeat = 24000.0; // 120 bpm at 48 kHz
    curve.addPoint(0.0, 1.0f, kSamplesPerBeat);
    curve.addPoint(4.0, 0.5f, kSamplesPerBeat);
    curve.addPoint(8.0, 0.25f, kSamplesPerBeat);

    auto r = automationHeaderReadout(&curve, 1.0, 2.0);
    check(!r.isNode && nearf(r.value, 0.75), "no node selected: the level at the playhead (halfway down the first slope)");
    auto& points = curve.getPoints();
    points[1].selected = true;
    r = automationHeaderReadout(&curve, 1.0, 2.0);
    check(r.isNode && nearf(r.value, 0.5), "one node selected: that node's level, wherever the playhead is");
    points[1].value = 0.3;
    r = automationHeaderReadout(&curve, 1.0, 2.0);
    check(r.isNode && nearf(r.value, 0.3), "and it follows the node as it is dragged");
    points[2].selected = true;
    r = automationHeaderReadout(&curve, 1.0, 6.0);
    check(!r.isNode, "two nodes selected: back to the playhead level, there is no single node to name");
    points[1].selected = points[2].selected = false;
    check(!automationHeaderReadout(&curve, 1.0, 0.0).isNode, "deselected: the playhead again");
    check(!automationHeaderReadout(nullptr, 0.7, 3.0).isNode && nearf(automationHeaderReadout(nullptr, 0.7, 3.0).value, 0.7),
          "no curve yet: the default level, not a node");
    AutomationCurve empty = makeAutomationCurve(AutomationTarget::Pan);
    const auto e = automationHeaderReadout(&empty, 0.0, 1.0);
    check(!e.isNode && nearf(e.value, 0.0), "a curve with no points: the default level");
}

int main() {
    testHeaderReadout();
    testVisibleSegmentSpan();
    testValueRanges();
    testCurvesByTarget();
    testHitReachesEveryCurve();
    testSelection();
    testHeaderLabels();
    if (g_failures == 0) {
        std::cout << "AutomationLaneEditingTest: all passed\n";
        return 0;
    }
    std::cout << "AutomationLaneEditingTest: " << g_failures << " failure(s)\n";
    return 1;
}
