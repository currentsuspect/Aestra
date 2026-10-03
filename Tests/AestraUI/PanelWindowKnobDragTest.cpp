// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// AestraPanelWindow::handleKnobDrag() is the one knob interaction shared by the
// Sat, OTT, Filter and LFO editors. Each of them used to carry its own copy of
// this logic, and those copies are what this test now stands in for: press a
// knob to grab it, move to change the value (clamped to 0..1, Shift for fine),
// release to let go, and a press anywhere else must not grab anything.
//
// No platform bridge is attached, so cursor capture is a no-op; the value
// arithmetic and the grab state are what is under test.

#include "Widgets/AestraPanelWindow.h"

#include <cmath>
#include <cstdio>
#include <iterator>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1.0e-5f;
}

constexpr float kRangePx = 160.0f;

class KnobProbe : public AestraPanelWindow {
public:
    float values[4] = {0.5f, 0.5f, 0.5f, 0.5f};

    bool feed(const NUIMouseEvent& event) {
        const KnobTarget knobs[] = {
            {{10.0f, 10.0f, 40.0f, 40.0f}, 2},
            {{60.0f, 10.0f, 40.0f, 40.0f}, 3},
        };
        return handleKnobDrag(
            event, knobs, std::size(knobs), kRangePx, [this](uint32_t id) { return values[id]; },
            [this](uint32_t id, float v) { values[id] = v; });
    }

    int grabbed() const { return knobDragParam(); }
};

NUIMouseEvent press(float x, float y, NUIMouseButton button = NUIMouseButton::Left) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.position = {x, y};
    e.button = button;
    e.pressed = true;
    return e;
}

NUIMouseEvent release(float x, float y) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Up;
    e.position = {x, y};
    e.button = NUIMouseButton::Left;
    e.released = true;
    return e;
}

NUIMouseEvent motion(float dy, NUIModifiers mods = NUIModifiers::None) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Move;
    e.delta = {0.0f, dy};
    e.modifiers = mods;
    return e;
}

void testPressGrabsTheKnobUnderTheCursor() {
    KnobProbe p;
    check(p.feed(press(80.0f, 30.0f)), "a press on a knob is consumed");
    check(p.grabbed() == 3, "the press grabs that knob's parameter, not the first one");
    check(near(p.values[3], 0.5f), "grabbing does not move the value");
}

void testMotionStepsTheGrabbedValue() {
    KnobProbe p;
    p.feed(press(80.0f, 30.0f));
    // Up 16 px over a 160 px range is +0.1 (screen y grows downward).
    check(p.feed(motion(-16.0f)), "motion while grabbed is consumed");
    check(near(p.values[3], 0.6f), "16 px up over a 160 px range adds 0.1");
    check(near(p.values[2], 0.5f), "the other knob is untouched");
    p.feed(motion(16.0f));
    check(near(p.values[3], 0.5f), "16 px down takes it back");
}

void testShiftIsFine() {
    KnobProbe p;
    p.feed(press(80.0f, 30.0f));
    p.feed(motion(-16.0f, NUIModifiers::Shift));
    check(near(p.values[3], 0.525f), "Shift drags at a quarter speed");
}

void testValueIsClamped() {
    KnobProbe p;
    p.feed(press(30.0f, 30.0f));
    p.feed(motion(-1000.0f));
    check(near(p.values[2], 1.0f), "a long drag up stops at 1");
    p.feed(motion(5000.0f));
    check(near(p.values[2], 0.0f), "a long drag down stops at 0");
}

void testReleaseLetsGo() {
    KnobProbe p;
    p.feed(press(80.0f, 30.0f));
    check(p.feed(release(80.0f, 30.0f)), "the release that ends a drag is consumed");
    check(p.grabbed() == -1, "release lets go of the knob");
    check(!p.feed(motion(-16.0f)), "motion after release is not consumed");
    check(near(p.values[3], 0.5f), "motion after release changes nothing");
}

void testPressesThatMustNotGrab() {
    KnobProbe p;
    check(!p.feed(press(200.0f, 200.0f)), "a press away from every knob is not consumed");
    check(p.grabbed() == -1, "and grabs nothing");
    check(!p.feed(press(80.0f, 30.0f, NUIMouseButton::Right)), "a right-press on a knob is not a grab");
    check(p.grabbed() == -1, "and leaves nothing grabbed");
    check(!p.feed(motion(-16.0f)), "motion with nothing grabbed is not consumed");
}

} // namespace

int main() {
    testPressGrabsTheKnobUnderTheCursor();
    testMotionStepsTheGrabbedValue();
    testShiftIsFine();
    testValueIsClamped();
    testReleaseLetsGo();
    testPressesThatMustNotGrab();
    if (g_failures == 0) {
        std::printf("PanelWindowKnobDragTest: all checks passed\n");
        return 0;
    }
    std::printf("PanelWindowKnobDragTest: %d check(s) failed\n", g_failures);
    return 1;
}
