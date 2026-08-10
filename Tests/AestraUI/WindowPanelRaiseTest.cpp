// © 2026 Aestra Studios — All Rights Reserved.
// WindowPanelRaiseTest — regression for the transport-click panel-raise bug.
//
// The overlay layer forwards every press to every visible floating panel so
// that clicks over empty overlay area pass through to the workspace. A
// WindowPanel must therefore only raise itself (bringToFront) when the press
// actually lands inside its bounds. Before the fix, any press anywhere —
// including a click on the transport bar's Stop button, which sits above the
// panels — popped every visible panel to the front, so double-clicking Stop
// in Arsenal mode raised the ARSENAL panel over the piano roll.

#include "NUIComponent.h"
#include "NUITypes.h"
#include "WindowPanel.h"

#include <iostream>

using namespace AestraUI;
using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

NUIMouseEvent leftPress(float x, float y) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.pressed = true;
    e.button = NUIMouseButton::Left;
    e.position = {x, y};
    return e;
}

// Deliver one press exactly like OverlayLayer does: topmost child first, stop
// at the first panel that consumes it.
bool dispatchPress(NUIComponent& overlay, const NUIMouseEvent& event) {
    const auto& children = overlay.getChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if ((*it)->isVisible() && (*it)->isHitTestVisible() && (*it)->onMouseEvent(event)) {
            return true;
        }
    }
    return false;
}

bool isTopmost(const NUIComponent& overlay, const WindowPanel& panel) {
    const auto& children = overlay.getChildren();
    return !children.empty() && children.back().get() == &panel;
}

void test_press_outside_bounds_does_not_raise() {
    NUIComponent overlay;
    overlay.setBounds({0, 0, 900, 600});

    auto panelA = std::make_shared<WindowPanel>("A");
    panelA->setBounds({100, 100, 300, 200});
    auto panelB = std::make_shared<WindowPanel>("B");
    panelB->setBounds({500, 100, 300, 200});
    overlay.addChild(panelA);
    overlay.addChild(panelB);

    // Press far away from both panels — e.g. the transport bar strip.
    const NUIMouseEvent press = leftPress(450, 20);
    const bool consumed = dispatchPress(overlay, press);
    check(!consumed, "an outside press must pass through to the workspace");
    check(isTopmost(overlay, *panelB), "topmost panel must stay topmost after an outside press");
    check(overlay.getChildren().size() == 2, "no panel may be removed or duplicated");

    std::cout << "PASS: press outside panel bounds does not raise panels\n";
}

void test_press_inside_bounds_raises_panel() {
    NUIComponent overlay;
    overlay.setBounds({0, 0, 900, 600});

    auto panelA = std::make_shared<WindowPanel>("A");
    panelA->setBounds({100, 100, 300, 200});
    auto panelB = std::make_shared<WindowPanel>("B");
    panelB->setBounds({500, 100, 300, 200});
    overlay.addChild(panelA);
    overlay.addChild(panelB);
    check(isTopmost(overlay, *panelB), "sanity: last added panel is topmost");

    // Press inside panel A, which is behind panel B.
    const bool consumed = dispatchPress(overlay, leftPress(150, 150));
    check(consumed, "an inside press must be consumed by a panel");
    check(isTopmost(overlay, *panelA), "a press inside a panel must raise it to the front");
}

void test_press_inside_topmost_panel_stays_consumed() {
    NUIComponent overlay;
    overlay.setBounds({0, 0, 900, 600});

    auto panelA = std::make_shared<WindowPanel>("A");
    panelA->setBounds({100, 100, 300, 200});
    auto panelB = std::make_shared<WindowPanel>("B");
    panelB->setBounds({500, 100, 300, 200});
    overlay.addChild(panelA);
    overlay.addChild(panelB);

    const bool consumed = dispatchPress(overlay, leftPress(550, 150));
    check(consumed, "press inside the topmost panel must be consumed");
    check(isTopmost(overlay, *panelB), "topmost panel must remain topmost");
}

void test_raise_requires_bounds_not_z_position() {
    // A panel must not raise on a press that overlaps its z-order slot but not
    // its bounds — the exact transport-bar scenario.
    NUIComponent overlay;
    overlay.setBounds({0, 0, 900, 600});

    auto panelA = std::make_shared<WindowPanel>("A");
    panelA->setBounds({100, 100, 300, 200});
    auto panelB = std::make_shared<WindowPanel>("B");
    panelB->setBounds({500, 100, 300, 200});
    overlay.addChild(panelA);
    overlay.addChild(panelB);

    const bool consumed = dispatchPress(overlay, leftPress(650, 20));
    check(!consumed, "press above the topmost panel's bounds must pass through");
    check(isTopmost(overlay, *panelB), "no panel may be raised by an out-of-bounds press");
}

} // namespace

int main() {
    std::cout << "=== WindowPanelRaise Unit Tests ===\n\n";

    test_press_outside_bounds_does_not_raise();
    test_press_inside_bounds_raises_panel();
    test_press_inside_topmost_panel_stays_consumed();
    test_raise_requires_bounds_not_z_position();

    if (g_failures != 0) {
        std::cout << "\n=== Results: " << g_failures << " failed ===\n";
        return 1;
    }
    std::cout << "\n=== Results: all passed ===\n";
    return 0;
}
