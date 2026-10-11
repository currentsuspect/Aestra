// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AutomationClipLayerTest — V8-A7. The gestures on automation clips, through the real layer and
// the real command history: drag moves, Ctrl-drag copies, a drop onto another clip is refused and
// leaves the last good place, and each gesture is exactly one undo step.

#include "AutomationClipLayer.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <iostream>

using namespace Aestra::Audio;
using namespace AestraUI;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

NUIMouseEvent mouse(NUIMouseEventType type, float x, float y, NUIMouseButton button = NUIMouseButton::Left,
                    bool ctrl = false) {
    NUIMouseEvent e;
    e.type = type;
    e.position = {x, y};
    e.button = button;
    e.pressed = type == NUIMouseEventType::Down;
    e.released = type == NUIMouseEventType::Up;
    if (ctrl)
        e.modifiers = NUIModifiers::Ctrl;
    return e;
}
} // namespace

int main() {
    TrackManager tm;
    auto& pm = tm.getPlaylistModel();
    const auto laneId = pm.createLane("A");
    AutomationCurve curve("vol", AutomationTarget::Volume);
    for (auto [b, v] : {std::pair{0.0, 0.2f}, {2.0, 0.8f}, {4.0, 0.4f}})
        curve.addPoint(b, v, 24000.0);
    pm.getLane(laneId)->automationCurves = {curve};

    // 40 px per beat, grid starts at x=100, lane row at y 0..80. Header strip: y 0..12.
    AutomationClipLayer::Geometry g{{0, 0, 800, 80}, 100.0f, 40.0f, 0.0f};
    AutomationClipLayer::Hooks hooks;
    hooks.snapBeat = [](double b) { return std::round(b); }; // whole beats
    int captures = 0;
    hooks.captureMouse = [&](bool on) { captures += on ? 1 : -1; };
    NUIComponent owner;
    AutomationClipLayer layer(owner);
    int edited = -1;
    const auto curves = [&]() -> const std::vector<AutomationCurve>& { return pm.getLane(laneId)->automationCurves; };
    const auto before = curves();

    std::cout << "[1] a press off the header is not the layer's\n";
    check(!layer.onMouse(mouse(NUIMouseEventType::Down, 140, 40), tm, laneId, g, hooks, edited), "the lane body is left to point editing");
    check(!layer.isDragging(), "and nothing is dragging");

    check(!layer.onMouse(mouse(NUIMouseEventType::Down, 200, 5), tm, laneId, g, hooks, edited),
          "a left press on the header away from the grip is point editing's too (a point can go there)");
    check(!layer.onMouse(mouse(NUIMouseEventType::Down, 112, 5, NUIMouseButton::Left), tm, laneId, g, hooks, edited) ||
              layer.isDragging(),
          "(the grip itself is the layer's)");
    if (layer.isDragging())
        layer.onMouse(mouse(NUIMouseEventType::Up, 112, 5), tm, laneId, g, hooks, edited);
    captures = 0;

    std::cout << "[2] drag the clip's grip: move\n";
    // Clip spans beats 0..4 = x 100..260; its grip is the tab at x 106..122. Drag it 3 beats right.
    check(layer.onMouse(mouse(NUIMouseEventType::Down, 112, 5), tm, laneId, g, hooks, edited), "press on the header is taken");
    check(layer.isDragging() && captures == 1 && edited == 0, "dragging, mouse captured, curve chosen");
    layer.onMouse(mouse(NUIMouseEventType::Move, 232, 30), tm, laneId, g, hooks, edited);
    check(std::fabs(curves()[0].points[0].beat - 3.0) < 1e-9, "the points follow the pointer (snapped)");
    layer.onMouse(mouse(NUIMouseEventType::Up, 232, 30), tm, laneId, g, hooks, edited);
    check(!layer.isDragging() && captures == 0, "release ends the drag and the capture");
    check(curves()[0].regions.size() == 1 && curves()[0].regions[0].startBeat == 3.0, "the clip is stored, at beat 3");
    tm.getCommandHistory().undo();
    check(automationCurvesEqual(curves(), before), "ONE undo puts it all back");
    tm.getCommandHistory().redo();
    check(curves()[0].points[0].beat == 3.0, "redo moves it again");
    tm.getCommandHistory().undo();

    std::cout << "[3] Ctrl-drag: copy\n";
    layer.onMouse(mouse(NUIMouseEventType::Down, 112, 5, NUIMouseButton::Left, true), tm, laneId, g, hooks, edited);
    layer.onMouse(mouse(NUIMouseEventType::Move, 432, 30), tm, laneId, g, hooks, edited); // +8 beats
    layer.onMouse(mouse(NUIMouseEventType::Up, 432, 30), tm, laneId, g, hooks, edited);
    check(curves()[0].regions.size() == 2 && curves()[0].points.size() == 6, "two clips, six points");
    check(curves()[0].points[0].beat == 0.0 && curves()[0].points[3].beat == 8.0, "the original stayed, the copy is 8 beats on");
    tm.getCommandHistory().undo();
    check(automationCurvesEqual(curves(), before), "ONE undo removes the copy");

    std::cout << "[4] dropping onto another clip is refused\n";
    {
        auto two = before;
        two[0].points.clear();
        for (auto [b, v] : {std::pair{0.0, 0.2f}, {2.0, 0.8f}, {10.0, 0.4f}, {12.0, 0.6f}})
            two[0].addPoint(b, v, 24000.0);
        two[0].regions = {{1, 0.0, 2.0}, {2, 10.0, 12.0}};
        pm.getLane(laneId)->automationCurves = two;
        layer.onMouse(mouse(NUIMouseEventType::Down, 112, 5), tm, laneId, g, hooks, edited); // clip 1
        layer.onMouse(mouse(NUIMouseEventType::Move, 112 + 40 * 9, 30), tm, laneId, g, hooks, edited); // lands on clip 2
        check(curves()[0].regions[0].startBeat == 0.0, "the clip stays where it was");
        layer.onMouse(mouse(NUIMouseEventType::Up, 112 + 40 * 9, 30), tm, laneId, g, hooks, edited);
        check(automationCurvesEqual(curves(), two), "nothing changed");
        check(!tm.getCommandHistory().canUndo(), "so there is no undo step");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
