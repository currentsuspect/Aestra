// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PanelModeBehaviourTest — SPEC 3 section 1.1. What the two choices do to a real panel:
//   Docked: the title bar no longer drags and the edges no longer resize, but maximize and the
//           panel's menu still work; Floating: both work, as before.
//   Contextual: a left press outside is reported to the owner; Keep Open: it is not.
//   A right-click on the title bar asks for the panel's menu.

#include "WindowPanel.h"
#include "AestraPanelWindow.h"
#include "NUICoreWidgets.h"
#include "Platform/NUICursorStyle.h"

#include <iostream>

using namespace AestraUI;
using Aestra::Audio::WindowPanel;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}
NUIMouseEvent press(float x, float y, NUIMouseButton b = NUIMouseButton::Left) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.position = {x, y};
    e.button = b;
    e.pressed = true;
    return e;
}
NUIMouseEvent move(float x, float y) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Move;
    e.position = {x, y};
    e.button = NUIMouseButton::None;
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
} // namespace

int main() {
    WindowPanel panel("Test");
    panel.setBounds({100, 100, 400, 300});
    panel.setMinimumPanelSize(100, 100);
    int dragStarts = 0, resizeStarts = 0, menus = 0, outside = 0;
    panel.setOnDragStart([&](const NUIPoint&) { ++dragStarts; });
    panel.setOnResizeStart([&]() { ++resizeStarts; });
    panel.setOnMenuRequested([&](const NUIPoint&) { ++menus; });
    panel.setOnOutsidePress([&]() { ++outside; });
    const NUIPoint titleBar{250, 110};   // inside the title bar, clear of its buttons
    const NUIPoint rightEdge{499, 250};  // within 7 px of the right edge

    std::cout << "[1] floating: drags and resizes\n";
    panel.onMouseEvent(press(titleBar.x, titleBar.y));
    panel.onMouseEvent(release(titleBar.x, titleBar.y));
    check(dragStarts == 1, "a title-bar press starts a drag");
    panel.onMouseEvent(press(rightEdge.x, rightEdge.y));
    panel.onMouseEvent(release(rightEdge.x, rightEdge.y));
    check(resizeStarts == 1, "an edge press starts a resize");
    check(panel.getResizeCursorStyleForPoint(rightEdge) == NUICursorStyle::ResizeEW, "and the edge shows the resize cursor");

    std::cout << "[2] docked: locked in place\n";
    panel.setLocked(true);
    dragStarts = resizeStarts = 0;
    panel.onMouseEvent(press(titleBar.x, titleBar.y));
    panel.onMouseEvent(move(300, 150));
    panel.onMouseEvent(release(300, 150));
    check(dragStarts == 0, "the title bar no longer drags");
    panel.onMouseEvent(press(rightEdge.x, rightEdge.y));
    panel.onMouseEvent(release(rightEdge.x, rightEdge.y));
    check(resizeStarts == 0, "the edge no longer resizes");
    check(panel.getResizeCursorStyleForPoint(rightEdge) == NUICursorStyle::Arrow, "and shows no resize cursor");
    check(panel.getBounds().x == 100 && panel.getBounds().y == 100 && panel.getBounds().width == 400,
          "the panel did not move or resize");
    check(panel.isLocked(), "it reports itself locked");
    panel.setLocked(false);

    std::cout << "[3] the title bar's menu\n";
    panel.onMouseEvent(press(titleBar.x, titleBar.y, NUIMouseButton::Right));
    check(menus == 1, "a right-click on the title bar asks for the menu");
    panel.onMouseEvent(press(300, 250, NUIMouseButton::Right));
    check(menus == 1, "a right-click in the body does not");

    std::cout << "[4] contextual vs keep open\n";
    panel.setContextual(false);
    panel.onMouseEvent(press(900, 700));
    check(outside == 0, "Keep Open: a press outside is not reported");
    panel.setContextual(true);
    panel.onMouseEvent(press(900, 700));
    check(outside == 1, "Contextual: a press outside is reported");
    panel.onMouseEvent(press(300, 250));
    check(outside == 1, "a press inside is not");
    panel.onMouseEvent(press(900, 700, NUIMouseButton::Right));
    check(outside == 1, "only a left press counts");

    std::cout << "[5] plugin editors (AestraPanelWindow)\n";
    {
        AestraPanelWindow editor;
        editor.setBounds({100, 100, 400, 300});
        int closes = 0, editorMenus = 0, dragEnds = 0;
        editor.setOnClose([&]() { ++closes; });
        editor.setOnMenuRequested([&](const NUIPoint&) { ++editorMenus; });
        editor.setOnWindowDragEnd([&]() { ++dragEnds; });
        const NUIPoint title{250, 110};

        editor.onMouseEvent(press(900, 700));
        check(closes == 1, "Contextual (the default): a press outside closes it");
        editor.setCloseOnOutsideClick(false);
        editor.onMouseEvent(press(900, 700));
        check(closes == 1, "Keep Open: it stays");

        editor.onMouseEvent(press(title.x, title.y));
        editor.onMouseEvent(move(300, 200));
        editor.onMouseEvent(release(300, 200));
        check(editor.getBounds().x > 100 && dragEnds == 1, "Floating: the title bar drags it");

        editor.setBounds({100, 100, 400, 300});
        editor.setLocked(true);
        editor.onMouseEvent(press(title.x, title.y));
        editor.onMouseEvent(move(300, 200));
        editor.onMouseEvent(release(300, 200));
        check(editor.getBounds().x == 100 && editor.getBounds().y == 100, "Docked: it stays where it is");
        editor.onMouseEvent(press(title.x, title.y, NUIMouseButton::Right));
        check(editorMenus == 1, "a right-click on the title bar asks for the menu (docked too)");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
