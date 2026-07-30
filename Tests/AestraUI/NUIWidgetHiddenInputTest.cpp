// © 2026 Aestra Studios — All Rights Reserved.
//
// Regression coverage for the leaf-side half of #672.
//
// NUIHiddenChildDispatchTest pins the PARENT-side guard added in #674: the
// generic child walk in NUIComponent::onMouseEvent refuses to offer input to an
// invisible child. That closed the live defect (#671), but it is not the whole
// invariant, because roughly a dozen overrides in this codebase do not use the
// generic walk at all — they forward straight to a named child:
//
//   SettingsDialog.cpp:298-303      m_applyButton / m_cancelButton / m_okButton
//   PianoRollToolbar.cpp:457-462    the seven tool-strip buttons
//   TrackUIComponent.cpp:1865       routeControlButton() -> mute / solo / record
//
// Those forwards bypass the parent filter entirely, so the guarantee has to also
// hold when a widget is handed an event directly. Every other base widget
// already self-guards (NUISlider, NUIDropdown, NUITextInput, NUIContextMenu,
// NUIScrollbar, NUIToggle, NUICheckbox all open with an isVisible() test).
// NUIButton did not: it checked only isEnabled(), called the base for its hover
// side effects while DISCARDING the result, then hit-tested and fired onClick_.
// Hidden components keep their bounds, so containsPoint() still matched.
//
// No shipping caller hides one of the directly-forwarded buttons today, so this
// was a trap rather than an outage — the same trap that produced #671 once a
// hidden sibling acquired overlapping geometry. These tests make the leaf-side
// property explicit so it cannot regress silently.

#include "NUIButton.h"

#include <iostream>
#include <memory>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

NUIMouseEvent makePress(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Down;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    event.pressed = true;
    return event;
}

NUIMouseEvent makeRelease(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Up;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    event.released = true;
    return event;
}

struct ButtonFixture {
    std::shared_ptr<NUIButton> button = std::make_shared<NUIButton>("Sign Out");
    int clicks = 0;

    ButtonFixture() {
        button->setBounds({100.0f, 200.0f, 120.0f, 32.0f});
        button->setOnClick([this] { ++clicks; });
    }

    /// A press+release pair delivered straight to the widget, exactly as
    /// SettingsDialog / PianoRollToolbar / TrackUIComponent do.
    void clickDirectly() {
        button->onMouseEvent(makePress(160.0f, 216.0f));
        button->onMouseEvent(makeRelease(160.0f, 216.0f));
    }
};

void testVisibleButtonStillClicks() {
    // Proves the reason the negative case below passes. Without this, a button
    // that never fires at all would satisfy the hidden-button assertion and the
    // test would be worthless.
    ButtonFixture fixture;

    fixture.clickDirectly();

    check(fixture.clicks == 1, "a visible button invoked directly still fires onClick");
}

void testHiddenButtonDoesNotClick() {
    ButtonFixture fixture;
    fixture.button->setVisible(false);

    fixture.clickDirectly();

    check(fixture.clicks == 0,
          "a hidden button handed a press+release directly must not fire onClick");
}

void testHiddenButtonReportsEventUnhandled() {
    // Consuming the event would be its own defect: the forwarding parent treats
    // `true` as "someone dealt with this" and stops routing, so a hidden button
    // that returned true would create a dead zone over whatever is underneath —
    // precisely the #671 symptom.
    ButtonFixture fixture;
    fixture.button->setVisible(false);

    const bool pressHandled = fixture.button->onMouseEvent(makePress(160.0f, 216.0f));
    const bool releaseHandled = fixture.button->onMouseEvent(makeRelease(160.0f, 216.0f));

    check(!pressHandled, "a hidden button must report the press as unhandled");
    check(!releaseHandled, "a hidden button must report the release as unhandled");
}

void testHiddenToggleDoesNotToggle() {
    // The toggle path is a separate branch from onClick_ and carries persistent
    // state, so a spurious fire is worse: the button's own toggled_ flips and the
    // observer is told about it.
    ButtonFixture fixture;
    bool toggledTo = false;
    int toggleCount = 0;
    fixture.button->setToggleable(true);
    fixture.button->setOnToggle([&](bool on) {
        toggledTo = on;
        ++toggleCount;
    });
    fixture.button->setVisible(false);

    fixture.clickDirectly();

    check(toggleCount == 0, "a hidden toggle button must not invoke its toggle observer");
    check(!toggledTo, "a hidden toggle button must not report a new toggle state");
    check(!fixture.button->isToggled(), "a hidden toggle button must not flip its own state");
}

void testReshownButtonWorksAgain() {
    // The guard must gate on current visibility, not latch. A button hidden and
    // shown again has to behave normally, or panels that swap visibility (the
    // membership page's login/sign-out pair) would go permanently dead.
    ButtonFixture fixture;
    fixture.button->setVisible(false);
    fixture.clickDirectly();
    check(fixture.clicks == 0, "still suppressed while hidden");

    fixture.button->setVisible(true);
    fixture.clickDirectly();

    check(fixture.clicks == 1, "a re-shown button fires again");
}

void testHiddenButtonDoesNotLatchPressedState() {
    // A press swallowed while hidden must not leave pressed_ armed, or the next
    // release after being re-shown would fire a click the user never started.
    ButtonFixture fixture;
    fixture.button->setVisible(false);

    fixture.button->onMouseEvent(makePress(160.0f, 216.0f));
    fixture.button->setVisible(true);
    fixture.button->onMouseEvent(makeRelease(160.0f, 216.0f));

    check(fixture.clicks == 0,
          "a release after re-showing must not complete a press that was never accepted");
}

void testHidingMidPressCancelsTheGesture() {
    // The inverse of the case above, and a regression the visibility guard itself
    // introduced. Before the guard, EVERY release cleared pressed_ — either via
    // the out-of-bounds branch or the in-bounds one. An early return skips both,
    // so a press accepted while visible stayed latched when its release arrived
    // hidden. The button then reappeared looking pressed, and the next release
    // landing inside it completed a click whose press had been cancelled.
    //
    // Hiding a component mid-gesture must cancel that gesture, not suspend it.
    ButtonFixture fixture;

    fixture.button->onMouseEvent(makePress(160.0f, 216.0f));  // accepted, visible
    check(fixture.button->isPressed(), "the visible press is accepted and latched");

    fixture.button->setVisible(false);
    fixture.button->onMouseEvent(makeRelease(160.0f, 216.0f));  // swallowed

    check(!fixture.button->isPressed(),
          "hiding mid-press must clear the latched press, not suspend it");

    fixture.button->setVisible(true);
    fixture.button->onMouseEvent(makeRelease(160.0f, 216.0f));  // unmatched release

    check(fixture.clicks == 0,
          "an unmatched release after re-showing must not complete the cancelled press");
}

void testDisablingMidPressCancelsTheGesture() {
    // Same latch, reached through the enabled_ half of the same guard. Buttons are
    // disabled far more often than they are hidden (transport state, sign-in
    // state), so this path is the more likely one in practice.
    ButtonFixture fixture;

    fixture.button->onMouseEvent(makePress(160.0f, 216.0f));
    fixture.button->setEnabled(false);
    fixture.button->onMouseEvent(makeRelease(160.0f, 216.0f));

    check(!fixture.button->isPressed(), "disabling mid-press must clear the latched press");

    fixture.button->setEnabled(true);
    fixture.button->onMouseEvent(makeRelease(160.0f, 216.0f));

    check(fixture.clicks == 0, "an unmatched release after re-enabling must not fire a click");
}

}  // namespace

int main() {
    testVisibleButtonStillClicks();
    testHiddenButtonDoesNotClick();
    testHiddenButtonReportsEventUnhandled();
    testHiddenToggleDoesNotToggle();
    testReshownButtonWorksAgain();
    testHiddenButtonDoesNotLatchPressedState();
    testHidingMidPressCancelsTheGesture();
    testDisablingMidPressCancelsTheGesture();

    if (g_failures != 0) {
        std::cout << "[FAIL] NUIWidgetHiddenInputTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] NUIWidgetHiddenInputTest\n";
    return 0;
}
