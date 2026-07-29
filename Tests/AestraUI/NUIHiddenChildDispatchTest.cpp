// © 2026 Aestra Studios — All Rights Reserved.
//
// Regression coverage for #672: an inactive workspace must never intercept
// input belonging to the active one.
//
// This reproduces the MECHANISM behind #671, not its symptom. The live defect
// looked like "playlist rows 5 and 6 are dead", but the rows were incidental —
// a hidden AuditionPanel was laid out over them and swallowed presses. What
// mattered was the shape:
//
//   * a hidden sibling is still laid out, with geometry overlapping the visible
//     one;
//   * its onMouseEvent override returns true before ever delegating to
//     NUIComponent::onMouseEvent, so the base's `if (!visible_) return false`
//     never runs;
//   * the parent dispatches to children virtually, so nothing stops it.
//
// The test therefore builds a hidden overlapper whose override unconditionally
// consumes presses — the worst-case override — and proves the parent refuses to
// offer it the event at all.

#include "NUIComponent.h"

#include <iostream>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

/// Stands in for AuditionPanel: an override that consumes every press it is
/// offered and never consults visibility itself. Consuming unconditionally is
/// deliberate — a guard that only works for well-behaved children would not
/// have prevented #671.
class GreedyPanel : public NUIComponent {
public:
    int pressesSeen = 0;

    bool onMouseEvent(const NUIMouseEvent& event) override {
        ++pressesSeen;
        if (event.pressed) {
            return true;  // swallow, exactly as the queue branch did
        }
        return false;
    }
};

/// Stands in for a playlist lane: records whether it ever got the press.
class LaneSpy : public NUIComponent {
public:
    int pressesSeen = 0;

    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (event.pressed) {
            ++pressesSeen;
            return true;
        }
        return false;
    }
};

NUIMouseEvent makePress(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Down;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    event.pressed = true;
    return event;
}

NUIMouseEvent makeMove(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Move;
    event.position = {x, y};
    return event;
}

struct Fixture {
    NUIComponent root;
    std::shared_ptr<LaneSpy> lane = std::make_shared<LaneSpy>();
    std::shared_ptr<GreedyPanel> panel = std::make_shared<GreedyPanel>();

    Fixture() {
        root.setBounds({0.0f, 0.0f, 1000.0f, 700.0f});
        // The lane is added first, the overlapping panel second, so the panel is
        // visited FIRST in the front-to-back (reverse) walk — the same ordering
        // that let the real panel win.
        lane->setBounds({100.0f, 300.0f, 800.0f, 50.0f});
        panel->setBounds({0.0f, 0.0f, 1000.0f, 700.0f});
        root.addChild(lane);
        root.addChild(panel);
    }
};

void testHiddenOverlapperDoesNotInterceptPress() {
    Fixture fixture;
    fixture.panel->setVisible(false);

    const bool handled = fixture.root.onMouseEvent(makePress(400.0f, 320.0f));

    check(fixture.panel->pressesSeen == 0,
          "a hidden child must not be offered a press at all");
    check(fixture.lane->pressesSeen == 1,
          "the visible component underneath must receive the press");
    check(handled, "the press must still be reported as handled by the visible child");
}

void testVisibleOverlapperStillIntercepts() {
    // The guard must not turn into "hidden panels never win"; a VISIBLE overlay
    // consuming input is correct behaviour and must be preserved.
    Fixture fixture;
    fixture.panel->setVisible(true);

    fixture.root.onMouseEvent(makePress(400.0f, 320.0f));

    check(fixture.panel->pressesSeen == 1, "a visible overlay is still offered the press");
    check(fixture.lane->pressesSeen == 0, "a visible overlay still consumes the press");
}

void testHiddenChildIsSkippedForMotionToo() {
    // Motion is where the original defect hid: hover kept working over the dead
    // rows because moves are never marked handled, which disguised the
    // interception as a dead zone. A hidden child should not see motion either.
    Fixture fixture;
    fixture.panel->setVisible(false);

    fixture.root.onMouseEvent(makeMove(400.0f, 320.0f));

    check(fixture.panel->pressesSeen == 0, "a hidden child must not be offered motion");
}

void testCapturedCursorStillReachesHiddenChild() {
    // A component hidden mid-drag must still get its terminating events, or the
    // drag hangs forever. The captured flag is the framework's only signal for
    // this, so the visibility filter is skipped while it is set.
    Fixture fixture;
    fixture.panel->setVisible(false);

    NUIMouseEvent event = makePress(400.0f, 320.0f);
    event.cursorCaptured = true;
    fixture.root.onMouseEvent(event);

    check(fixture.panel->pressesSeen == 1,
          "a captured drag must still reach a child hidden mid-drag");
}

void testCapturedReleaseReachesHiddenChildAfterPress() {
    // The stranding scenario in full: a drag starts while the component is
    // visible, the workspace is switched mid-drag, and the terminating release
    // arrives captured. If the release is filtered out, the component's drag
    // state stays latched forever. Panels that keep such state (AuditionPanel's
    // waveform scrub) depend on this.
    Fixture fixture;

    NUIMouseEvent press = makePress(400.0f, 320.0f);
    press.cursorCaptured = true;
    fixture.root.onMouseEvent(press);
    const int seenAfterPress = fixture.panel->pressesSeen;

    fixture.panel->setVisible(false);  // hidden mid-drag

    NUIMouseEvent release;
    release.type = NUIMouseEventType::Up;
    release.position = {400.0f, 320.0f};
    release.button = NUIMouseButton::Left;
    release.released = true;
    release.cursorCaptured = true;
    fixture.root.onMouseEvent(release);

    check(fixture.panel->pressesSeen == seenAfterPress + 1,
          "a captured release must reach a child hidden mid-drag so its state can unwind");
}

void testStaleGeometryOverlappingActiveWorkspace() {
    // The #671 shape specifically: the hidden panel's geometry covers only PART
    // of the visible surface (its queue rectangle), so some rows were dead and
    // others were not. Both must work once the panel is hidden.
    Fixture fixture;
    fixture.panel->setVisible(false);
    fixture.panel->setBounds({100.0f, 300.0f, 800.0f, 50.0f});  // exactly over the lane

    fixture.root.onMouseEvent(makePress(400.0f, 320.0f));  // inside the overlap
    check(fixture.lane->pressesSeen == 1, "lane under a hidden panel's rect receives the press");

    fixture.root.onMouseEvent(makePress(400.0f, 340.0f));  // also inside the lane
    check(fixture.lane->pressesSeen == 2, "second press in the same band also lands");
}

}  // namespace

int main() {
    testHiddenOverlapperDoesNotInterceptPress();
    testVisibleOverlapperStillIntercepts();
    testHiddenChildIsSkippedForMotionToo();
    testCapturedCursorStillReachesHiddenChild();
    testCapturedReleaseReachesHiddenChildAfterPress();
    testStaleGeometryOverlappingActiveWorkspace();

    if (g_failures != 0) {
        std::cout << "[FAIL] NUIHiddenChildDispatchTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] NUIHiddenChildDispatchTest\n";
    return 0;
}
