// © 2026 Aestra Studios — All Rights Reserved.
// Regression coverage for the shared global tooltip lifecycle and placement.

#include "NUIComponent.h"

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

void resetTooltip() {
    NUIComponent::setCursorCaptureActive(false);
    NUIComponent::resetPointerGestureState();
    NUIComponent::hideRemoteTooltip();
}

NUIMouseEvent pointerEvent(bool pressed, bool released, float wheel = 0.0f) {
    NUIMouseEvent e;
    e.button = (pressed || released) ? NUIMouseButton::Left : NUIMouseButton::None;
    e.type = pressed ? NUIMouseEventType::Down
                     : (released ? NUIMouseEventType::Up
                                 : (wheel != 0.0f ? NUIMouseEventType::Scroll : NUIMouseEventType::Move));
    e.pressed = pressed;
    e.released = released;
    e.wheelDelta = wheel;
    return e;
}

void testHoverTooltipsWaitForStableIntent() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("Delayed", {20.0f, 20.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.20);
    check(NUIComponent::getGlobalTooltipState().alpha == 0.0f, "tooltip remains hidden during hover delay");

    NUIComponent::showRemoteTooltip("Delayed", {80.0f, 80.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.26);
    const auto& state = NUIComponent::getGlobalTooltipState();
    check(state.alpha > 0.0f, "repeated show calls do not restart the hover delay");
    check(state.position.x == 20.0f && state.position.y == 20.0f, "visible tooltip keeps a stable anchor");
}

// SPEC 3 §1.3 ruling (2026-09-23): moving between controls while a tooltip shows hands off at
// once. This test used to pin the opposite (a hand-off restarted the delay and the fade).
void testVisibleTooltipHandsOffInstantly() {
    resetTooltip();
    int firstOwner = 0;
    int secondOwner = 0;
    NUIComponent::showRemoteTooltip("First", {10.0f, 10.0f}, &firstOwner);
    NUIComponent::updateGlobalTooltip(0.60);
    const float visibleAlpha = NUIComponent::getGlobalTooltipState().alpha;
    check(visibleAlpha > 0.0f, "first owner tooltip became visible");

    NUIComponent::showRemoteTooltip("Second", {30.0f, 40.0f}, &secondOwner);
    const auto& state = NUIComponent::getGlobalTooltipState();
    check(state.alpha == visibleAlpha, "a visible tooltip hands off with no second delay or fade");
    check(state.owner == &secondOwner && state.text == "Second", "the new control owns it");
    check(state.position.x == 30.0f && state.position.y == 40.0f, "hand-off adopts the new anchor");
}

// The same rule inside one owner: a new row of a list is a new control to the user.
void testNewRowWhileVisibleHandsOff() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("First row", {10.0f, 10.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.60);
    NUIComponent::showRemoteTooltip("Second row", {30.0f, 40.0f}, &owner);
    check(NUIComponent::getGlobalTooltipState().alpha > 0.0f, "a new row hands off while one is showing");
}

// Hand-off needs something on screen: before the first tooltip appears, new content waits.
void testNewContentBeforeVisibleRestartsDelay() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("First row", {10.0f, 10.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.20);
    NUIComponent::showRemoteTooltip("Second row", {30.0f, 40.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.30);
    check(NUIComponent::getGlobalTooltipState().alpha == 0.0f, "nothing was visible, so the delay restarts");
}

// Leaving a control starts a short fade; entering the next one inside it still hands off.
void testHandOffAcrossTheGapBetweenControls() {
    resetTooltip();
    int first = 0;
    int second = 0;
    NUIComponent::showRemoteTooltip("First", {10.0f, 10.0f}, &first);
    NUIComponent::updateGlobalTooltip(0.60);
    NUIComponent::hideRemoteTooltip(&first);
    NUIComponent::updateGlobalTooltip(0.20);
    NUIComponent::showRemoteTooltip("Second", {60.0f, 10.0f}, &second);
    check(NUIComponent::getGlobalTooltipState().alpha > 0.0f, "crossing the gap between two controls hands off");
}

void testForcedReadoutKeepsOpacityAcrossValueChanges() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("Value 10", {10.0f, 10.0f}, &owner, true);
    NUIComponent::updateGlobalTooltip(0.10);
    const float previousAlpha = NUIComponent::getGlobalTooltipState().alpha;
    NUIComponent::showRemoteTooltip("Value 11", {12.0f, 12.0f}, &owner, true);
    check(NUIComponent::getGlobalTooltipState().alpha == previousAlpha,
          "forced value changes do not flicker by resetting opacity");
}

void testOwnerlessHideIsAnUnconditionalGlobalDismiss() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("Owned", {10.0f, 10.0f}, &owner, true);
    NUIComponent::hideRemoteTooltip();
    check(!NUIComponent::getGlobalTooltipState().active, "ownerless hide dismisses an owner-scoped tooltip");
}

void testDifferentOwnerCannotDismissTooltip() {
    resetTooltip();
    int tooltipOwner = 0;
    int unrelatedOwner = 0;
    NUIComponent::showRemoteTooltip("Owned", {10.0f, 10.0f}, &tooltipOwner, true);
    NUIComponent::hideRemoteTooltip(&unrelatedOwner);
    check(NUIComponent::getGlobalTooltipState().active, "unrelated components cannot dismiss an owned tooltip");
    check(NUIComponent::getGlobalTooltipState().owner == &tooltipOwner, "the original owner remains active");
}

void testSameOwnerCanReassertWithinResumeWindow() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("Stable", {10.0f, 10.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.60);
    const float visibleAlpha = NUIComponent::getGlobalTooltipState().alpha;

    NUIComponent::hideRemoteTooltip(&owner);
    NUIComponent::updateGlobalTooltip(0.25);
    NUIComponent::showRemoteTooltip("Stable", {30.0f, 30.0f}, &owner);
    const auto& resumed = NUIComponent::getGlobalTooltipState();
    check(resumed.active, "same owner can reassert a tooltip after a brief routing gap");
    check(resumed.alpha == visibleAlpha, "brief same-owner reassertion preserves visible opacity");
    check(resumed.position.x == 10.0f && resumed.position.y == 10.0f,
          "brief same-owner reassertion preserves the stable anchor");

    NUIComponent::hideRemoteTooltip(&owner);
    NUIComponent::updateGlobalTooltip(0.51);
    const auto& cleared = NUIComponent::getGlobalTooltipState();
    check(!cleared.active && cleared.owner == nullptr && cleared.alpha == 0.0f,
          "an owner-scoped hide clears after the resume window expires");
}

void testCaptureDismissesHoverTooltipButForcedReadoutCanShow() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("Hover", {10.0f, 10.0f}, &owner);
    NUIComponent::setCursorCaptureActive(true);
    check(!NUIComponent::getGlobalTooltipState().active, "cursor capture dismisses the hover tooltip");

    NUIComponent::showRemoteTooltip("Drag value", {14.0f, 16.0f}, &owner, true);
    NUIComponent::updateGlobalTooltip(0.016);
    check(NUIComponent::getGlobalTooltipState().alpha > 0.0f, "forced drag readout bypasses hover delay");
    resetTooltip();
}

void testTooltipBoundsStayInsideViewportEdges() {
    const NUIRect viewport(0.0f, 0.0f, 320.0f, 180.0f);
    const NUISize tooltipSize(100.0f, 30.0f);

    const NUIRect topRight = NUIComponent::calculateTooltipBounds({315.0f, 3.0f}, tooltipSize, viewport);
    check(topRight.x >= 4.0f && topRight.right() <= 316.0f, "right-edge tooltip is horizontally clamped");
    check(topRight.y >= 4.0f && topRight.bottom() <= 176.0f, "top-edge tooltip flips and stays vertically clamped");

    const NUIRect bottomRight = NUIComponent::calculateTooltipBounds({319.0f, 179.0f}, tooltipSize, viewport);
    check(bottomRight.right() <= 316.0f, "bottom-right tooltip stays inside right edge");
    check(bottomRight.bottom() <= 176.0f, "bottom-right tooltip stays inside bottom edge");
}


// A press is a decision; a held button is a drag. Neither wants a hover explanation.
void testPressDismissesAndAHeldButtonSuppresses() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("Hover", {10.0f, 10.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.60);
    NUIComponent::notePointerGesture(pointerEvent(true, false));
    const auto& pressed = NUIComponent::getGlobalTooltipState();
    check(!pressed.active && pressed.alpha == 0.0f, "a press dismisses the tooltip at once");

    NUIComponent::showRemoteTooltip("Other", {50.0f, 10.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.60);
    check(!NUIComponent::getGlobalTooltipState().active, "no hover tooltip while a button is held (a drag)");

    NUIComponent::showRemoteTooltip("Value 3", {50.0f, 10.0f}, &owner, true);
    NUIComponent::updateGlobalTooltip(0.016);
    check(NUIComponent::getGlobalTooltipState().alpha > 0.0f, "a forced drag readout still shows");

    NUIComponent::notePointerGesture(pointerEvent(false, true));
    NUIComponent::hideRemoteTooltip();
    NUIComponent::showRemoteTooltip("Hover", {10.0f, 10.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.60);
    check(NUIComponent::getGlobalTooltipState().alpha > 0.0f, "after the release, hover tooltips return");
}

// The BPM report: scrolling the value while its tooltip sat on top of it.
void testScrollDismisses() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("BPM - scroll to adjust", NUIRect(100.0f, 10.0f, 60.0f, 24.0f), &owner);
    NUIComponent::updateGlobalTooltip(0.60);
    NUIComponent::notePointerGesture(pointerEvent(false, false, 1.0f));
    check(!NUIComponent::getGlobalTooltipState().active, "a scroll dismisses the tooltip");
}

// Focus lost with a button down: the release may never come. Tooltips must not stay off forever.
void testFocusLossForgetsAHeldButton() {
    resetTooltip();
    int owner = 0;
    NUIComponent::notePointerGesture(pointerEvent(true, false));
    check(NUIComponent::isPointerButtonHeld(), "the press is held");
    NUIComponent::resetPointerGestureState();
    NUIComponent::showRemoteTooltip("Hover", {10.0f, 10.0f}, &owner);
    check(NUIComponent::getGlobalTooltipState().active, "after a focus reset hover tooltips work again");
}

// Panel switch: hiding a panel hides only the panel; its buttons own their tooltips.
void testHidingAPanelTakesItsChildsTooltip() {
    resetTooltip();
    auto panel = std::make_shared<NUIComponent>();
    auto button = std::make_shared<NUIComponent>();
    panel->addChild(button);
    NUIComponent::showRemoteTooltip("Button", NUIRect(10.0f, 10.0f, 40.0f, 20.0f), button.get());
    NUIComponent::updateGlobalTooltip(0.60);
    check(NUIComponent::getGlobalTooltipState().alpha > 0.0f, "the button's tooltip is showing");
    panel->setVisible(false);
    const auto& state = NUIComponent::getGlobalTooltipState();
    check(!state.active && state.alpha == 0.0f, "hiding the panel dismisses its child's tooltip");

    int unrelated = 0;
    auto other = std::make_shared<NUIComponent>();
    NUIComponent::showRemoteTooltip("Elsewhere", NUIRect(200.0f, 10.0f, 40.0f, 20.0f), &unrelated);
    other->setVisible(false);
    check(NUIComponent::getGlobalTooltipState().active, "hiding an unrelated component leaves it alone");
    resetTooltip();
}

// One placement rule: below the control, left-aligned, never covering it; above only at the bottom edge.
void testTooltipSitsBelowItsControl() {
    const NUIRect viewport(0.0f, 0.0f, 400.0f, 300.0f);
    const NUISize size(90.0f, 20.0f);
    const NUIRect control(100.0f, 50.0f, 80.0f, 24.0f);
    const NUIRect tip = NUIComponent::calculateTooltipBounds(control, size, viewport);
    check(tip.y >= control.bottom(), "the tooltip sits below the control");
    check(tip.x == control.x, "left-aligned with the control");
    check(!tip.intersects(control), "it never covers the control it explains");

    const NUIRect nearBottom(100.0f, 270.0f, 80.0f, 24.0f);
    const NUIRect flipped = NUIComponent::calculateTooltipBounds(nearBottom, size, viewport);
    check(flipped.bottom() <= nearBottom.y, "with no room below it flips above");
    check(!flipped.intersects(nearBottom), "and still does not cover the control");

    const NUIRect nearTop(100.0f, 2.0f, 80.0f, 24.0f);
    check(NUIComponent::calculateTooltipBounds(nearTop, size, viewport).y >= nearTop.bottom(),
          "a control at the top edge gets the same below placement as every other (no flip)");
}

// Review (#965): a target may re-show its hover tooltip for an in-bounds scroll. Dismissing only
// before dispatch let it restart; the dispatch path dismisses after the target too.
class ScrollReshows : public NUIComponent {
public:
    bool forced = false;
    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (event.wheelDelta != 0.0f) {
            showRemoteTooltip(forced ? "Value 42" : "Scroll to adjust", NUIRect(10.0f, 10.0f, 60.0f, 20.0f), this,
                              forced);
        }
        return true;
    }
};

void testAScrollTheTargetReshowsStillDismisses() {
    resetTooltip();
    ScrollReshows target;
    NUIComponent::dispatchMouseEvent(&target, pointerEvent(false, false, 1.0f));
    check(!NUIComponent::getGlobalTooltipState().active, "a hover tooltip re-shown by the scroll target is dismissed");

    target.forced = true;
    NUIComponent::dispatchMouseEvent(&target, pointerEvent(false, false, 1.0f));
    check(NUIComponent::getGlobalTooltipState().active, "a forced readout shown for the scroll survives it");
    resetTooltip();
}

// Review (#965): bounds are window-absolute. A nested control's tooltip anchors to its own bounds,
// not to its bounds plus every ancestor's position again.
void testNestedControlAnchorsToItsOwnBounds() {
    resetTooltip();
    auto panel = std::make_shared<NUIComponent>();
    auto button = std::make_shared<NUIComponent>();
    panel->setBounds(NUIRect(100.0f, 50.0f, 400.0f, 300.0f));
    button->setBounds(NUIRect(120.0f, 60.0f, 40.0f, 20.0f));
    panel->addChild(button);
    button->setTooltip("Nested");
    button->onMouseEnter();
    const auto& state = NUIComponent::getGlobalTooltipState();
    check(state.anchor.x == 120.0f && state.anchor.y == 60.0f && state.anchor.width == 40.0f,
          "the tooltip anchors to the control where it is drawn and hit-tested");
    button->onMouseLeave();
    resetTooltip();
}

// Review (#965): releasing one button while another still drags is still a drag.
void testEachHeldButtonIsTrackedUntilItsRelease() {
    resetTooltip();
    int owner = 0;
    auto press = [](NUIMouseButton b) {
        NUIMouseEvent e = pointerEvent(true, false);
        e.button = b;
        return e;
    };
    auto release = [](NUIMouseButton b) {
        NUIMouseEvent e = pointerEvent(false, true);
        e.button = b;
        return e;
    };
    NUIComponent::notePointerGesture(press(NUIMouseButton::Left));
    NUIComponent::notePointerGesture(press(NUIMouseButton::Right));
    NUIComponent::notePointerGesture(release(NUIMouseButton::Right));
    check(NUIComponent::isPointerButtonHeld(), "Left is still down after Right's release");
    NUIComponent::showRemoteTooltip("Hover", {10.0f, 10.0f}, &owner);
    check(!NUIComponent::getGlobalTooltipState().active, "so hover tooltips stay down for the Left drag");
    NUIComponent::notePointerGesture(release(NUIMouseButton::Left));
    check(!NUIComponent::isPointerButtonHeld(), "all released");
    resetTooltip();
}

} // namespace

int main() {
    testHoverTooltipsWaitForStableIntent();
    testVisibleTooltipHandsOffInstantly();
    testNewRowWhileVisibleHandsOff();
    testNewContentBeforeVisibleRestartsDelay();
    testHandOffAcrossTheGapBetweenControls();
    testForcedReadoutKeepsOpacityAcrossValueChanges();
    testOwnerlessHideIsAnUnconditionalGlobalDismiss();
    testDifferentOwnerCannotDismissTooltip();
    testSameOwnerCanReassertWithinResumeWindow();
    testCaptureDismissesHoverTooltipButForcedReadoutCanShow();
    testTooltipBoundsStayInsideViewportEdges();
    testPressDismissesAndAHeldButtonSuppresses();
    testScrollDismisses();
    testFocusLossForgetsAHeldButton();
    testHidingAPanelTakesItsChildsTooltip();
    testTooltipSitsBelowItsControl();
    testAScrollTheTargetReshowsStillDismisses();
    testNestedControlAnchorsToItsOwnBounds();
    testEachHeldButtonIsTrackedUntilItsRelease();

    resetTooltip();
    if (g_failures != 0) {
        std::cout << g_failures << " tooltip check(s) failed\n";
        return 1;
    }

    std::cout << "All global tooltip checks passed\n";
    return 0;
}
