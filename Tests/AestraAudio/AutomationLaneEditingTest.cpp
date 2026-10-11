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

} // namespace

int main() {
    testValueRanges();
    testCurvesByTarget();
    testHitReachesEveryCurve();
    testSelection();
    if (g_failures == 0) {
        std::cout << "AutomationLaneEditingTest: all passed\n";
        return 0;
    }
    std::cout << "AutomationLaneEditingTest: " << g_failures << " failure(s)\n";
    return 1;
}
