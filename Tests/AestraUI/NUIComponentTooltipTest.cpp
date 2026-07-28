// © 2026 Aestra Studios — All Rights Reserved.
// Regression coverage for the shared global tooltip lifecycle and placement.

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

void resetTooltip() {
    NUIComponent::setCursorCaptureActive(false);
    NUIComponent::hideRemoteTooltip();
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

void testOwnershipHandoffRestartsTooltip() {
    resetTooltip();
    int firstOwner = 0;
    int secondOwner = 0;
    NUIComponent::showRemoteTooltip("First", {10.0f, 10.0f}, &firstOwner);
    NUIComponent::updateGlobalTooltip(0.60);
    check(NUIComponent::getGlobalTooltipState().alpha > 0.0f, "first owner tooltip became visible");

    NUIComponent::showRemoteTooltip("Second", {30.0f, 40.0f}, &secondOwner);
    const auto& state = NUIComponent::getGlobalTooltipState();
    check(state.alpha == 0.0f, "owner handoff does not morph an already-visible tooltip");
    check(state.owner == &secondOwner, "new owner controls the tooltip");
    check(state.position.x == 30.0f && state.position.y == 40.0f, "owner handoff adopts the new anchor");
}

void testTextChangeForSameHoverOwnerRestartsDelay() {
    resetTooltip();
    int owner = 0;
    NUIComponent::showRemoteTooltip("First row", {10.0f, 10.0f}, &owner);
    NUIComponent::updateGlobalTooltip(0.60);
    NUIComponent::showRemoteTooltip("Second row", {30.0f, 40.0f}, &owner);
    check(NUIComponent::getGlobalTooltipState().alpha == 0.0f, "new hover content restarts the delay");
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

} // namespace

int main() {
    testHoverTooltipsWaitForStableIntent();
    testOwnershipHandoffRestartsTooltip();
    testTextChangeForSameHoverOwnerRestartsDelay();
    testForcedReadoutKeepsOpacityAcrossValueChanges();
    testOwnerlessHideIsAnUnconditionalGlobalDismiss();
    testCaptureDismissesHoverTooltipButForcedReadoutCanShow();
    testTooltipBoundsStayInsideViewportEdges();

    resetTooltip();
    if (g_failures != 0) {
        std::cout << g_failures << " tooltip check(s) failed\n";
        return 1;
    }

    std::cout << "All global tooltip checks passed\n";
    return 0;
}
