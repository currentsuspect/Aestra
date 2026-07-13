// © 2026 Aestra Studios — All Rights Reserved.
// PanelWindowClickSwallowTest — regression for the global plugin-editor
// click-through bug.
//
// A floating plugin editor (AestraPanelWindow subclass) must be OPAQUE to the
// mouse over its own area: a left-click on empty panel space — hitting no
// control, close button, or title bar — must be consumed (return true) so it
// does not fall through to the widgets behind the editor. Before the fix, most
// editors fell through to `return false` on such clicks, so the click both hit
// the widget behind (click-through) and dismissed the editor. The fix routes
// every editor's fall-through through AestraPanelWindow::consumeInsideBounds().
//
// This exercises the base-class contract with a minimal subclass that follows
// the exact editor dispatch shape (chrome first, controls, then swallow).

#include "AestraPanelWindow.h"
#include "NUITypes.h"

#include <iostream>

using namespace AestraUI;

namespace {

int g_failures = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

// Mimics the canonical editor onMouseEvent: base chrome first, then a single
// interactive control, then the inside-bounds swallow as the fall-through.
class FakePanelEditor : public AestraPanelWindow {
public:
    NUIRect controlRect;
    bool controlHit = false;

    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (AestraPanelWindow::onMouseEvent(event))
            return true;
        if (event.pressed && event.button == NUIMouseButton::Left && controlRect.contains(event.position)) {
            controlHit = true;
            return true;
        }
        return consumeInsideBounds(event);
    }
};

NUIMouseEvent leftPress(float x, float y) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.pressed = true;
    e.button = NUIMouseButton::Left;
    e.position = {x, y};
    return e;
}

FakePanelEditor makeEditor(int& closeCount) {
    // Bounds well away from the origin; title bar is the top TITLE_BAR_H px.
    // Body: y in [132, 300). Close button: right()-28 .. right()-4, y 104..128.
    FakePanelEditor ed;
    ed.setBounds(NUIRect(100.0f, 100.0f, 300.0f, 200.0f)); // x100..400 y100..300
    ed.controlRect = NUIRect(140.0f, 160.0f, 60.0f, 60.0f); // a control inside the body
    ed.setOnClose([&closeCount] { ++closeCount; });
    return ed;
}

// Empty body click (no control, no chrome) must be consumed and must NOT close.
void testEmptyBodyClickIsSwallowed() {
    int closed = 0;
    auto ed = makeEditor(closed);
    bool handled = ed.onMouseEvent(leftPress(350.0f, 280.0f)); // inside body, outside control
    check(handled, "empty-body click consumed (no fall-through)");
    check(closed == 0, "empty-body click did not close the editor");
    check(!ed.controlHit, "empty-body click did not hit the control");
}

// A click on an actual control is consumed by the control, not the swallow.
void testControlClickStillWorks() {
    int closed = 0;
    auto ed = makeEditor(closed);
    bool handled = ed.onMouseEvent(leftPress(170.0f, 190.0f)); // inside controlRect
    check(handled, "control click consumed");
    check(ed.controlHit, "control click reached the control");
    check(closed == 0, "control click did not close the editor");
}

// A genuine outside click is NOT swallowed (propagates) and closes the editor.
void testOutsideClickPropagatesAndCloses() {
    int closed = 0;
    auto ed = makeEditor(closed);
    bool handled = ed.onMouseEvent(leftPress(600.0f, 600.0f)); // far outside bounds
    check(!handled, "outside click not swallowed (propagates for dismiss/passthrough)");
    check(closed == 1, "outside click closed the editor exactly once");
    check(!ed.controlHit, "outside click did not hit the control");
}

// A click on the title bar is consumed by chrome (drag), not the swallow, and
// does not close.
void testTitleBarClickConsumedByChrome() {
    int closed = 0;
    auto ed = makeEditor(closed);
    bool handled = ed.onMouseEvent(leftPress(200.0f, 110.0f)); // title bar strip
    check(handled, "title-bar click consumed by chrome");
    check(closed == 0, "title-bar click did not close the editor");
    check(ed.isDraggingWindow(), "title-bar press started a window drag");
}

} // namespace

int main() {
    testEmptyBodyClickIsSwallowed();
    testControlClickStillWorks();
    testOutsideClickPropagatesAndCloses();
    testTitleBarClickConsumedByChrome();

    if (g_failures > 0) {
        std::cout << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All panel-window click-swallow checks passed\n";
    return 0;
}
