// © 2026 Aestra Studios — All Rights Reserved.

#include "NUIContextMenu.h"

#include <iostream>
#include <memory>

using namespace AestraUI;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++failures;
    }
}

NUIKeyEvent press(NUIKeyCode key, NUIModifiers modifiers = NUIModifiers::None) {
    NUIKeyEvent event;
    event.keyCode = key;
    event.modifiers = modifiers;
    event.pressed = true;
    return event;
}

void testKeyboardNavigationAndFocusRestoration() {
    auto previous = std::make_shared<NUIComponent>();
    previous->setFocused(true);

    auto menu = std::make_shared<NUIContextMenu>();
    auto disabled = std::make_shared<NUIContextMenuItem>("Unavailable");
    disabled->setEnabled(false);
    menu->addItem(disabled);
    menu->addSeparator();
    int activations = 0;
    menu->addItem("Copy", [&]() { ++activations; });
    menu->addSeparator();
    menu->addItem("Delete", [&]() { activations += 10; });

    menu->showAt(10, 10);
    check(NUIComponent::getFocusedComponent() == menu.get(), "opening a menu transfers keyboard focus to it");
    check(menu->getHoveredItemIndex() == 2, "initial focus skips disabled items and separators");

    menu->onKeyEvent(press(NUIKeyCode::Down));
    check(menu->getHoveredItemIndex() == 4, "Down skips separators");
    menu->onKeyEvent(press(NUIKeyCode::Up));
    check(menu->getHoveredItemIndex() == 2, "Up skips separators and disabled items");
    menu->onKeyEvent(press(NUIKeyCode::Enter));

    check(activations == 1, "Enter activates the highlighted action exactly once");
    check(!menu->isVisible(), "choosing an action closes the menu");
    check(NUIComponent::getFocusedComponent() == previous.get(), "closing a menu restores prior focus");
}

void testSubmenuKeyboardLifecycle() {
    auto previous = std::make_shared<NUIComponent>();
    previous->setFocused(true);
    auto parent = std::make_shared<NUIContextMenu>();
    auto child = std::make_shared<NUIContextMenu>();
    child->addItem("Master", []() {});
    parent->addSubmenu("Route Source", child);

    parent->showAt(20, 20);
    parent->onKeyEvent(press(NUIKeyCode::Right));
    check(child->isVisible(), "Right opens the highlighted submenu");
    check(NUIComponent::getFocusedComponent() == child.get(), "an open submenu owns keyboard focus");

    child->onKeyEvent(press(NUIKeyCode::Escape));
    check(!child->isVisible() && parent->isVisible(), "Escape closes only the active submenu first");
    check(NUIComponent::getFocusedComponent() == parent.get(), "closing a submenu returns focus to its parent");

    parent->onKeyEvent(press(NUIKeyCode::Escape));
    check(!parent->isVisible(), "a second Escape closes the parent menu");
    check(NUIComponent::getFocusedComponent() == previous.get(), "the complete menu stack restores original focus");
}

void testTallMenuScrollingAndViewportContainment() {
    auto root = std::make_shared<NUIComponent>();
    root->setBounds({0.0f, 0.0f, 480.0f, 220.0f});

    auto parent = std::make_shared<NUIContextMenu>();
    auto child = std::make_shared<NUIContextMenu>();
    child->setMaxHeight(120.0f);
    int activated = -1;
    for (int index = 0; index < 20; ++index) {
        child->addItem("Destination " + std::to_string(index), [&, index]() { activated = index; });
    }
    parent->addSubmenu("Route Linked Clips", child);
    root->addChild(parent);

    parent->showAt(430, 190);
    parent->onKeyEvent(press(NUIKeyCode::Right));
    check(child->isVisible(), "keyboard opens a tall submenu");
    check(child->getBounds().right() <= root->getBounds().right() &&
              child->getBounds().bottom() <= root->getBounds().bottom(),
          "tall submenu stays inside its viewport");

    NUIMouseEvent wheel;
    wheel.type = NUIMouseEventType::Scroll;
    wheel.position = {child->getBounds().x + 20.0f, child->getBounds().y + 14.0f};
    wheel.wheelDelta = -1.0f;
    check(child->onMouseEvent(wheel), "wheel input is consumed by a scrollable menu");

    NUIMouseEvent pressEvent;
    pressEvent.type = NUIMouseEventType::Down;
    pressEvent.position = wheel.position;
    pressEvent.button = NUIMouseButton::Left;
    pressEvent.pressed = true;
    child->onMouseEvent(pressEvent);
    pressEvent.type = NUIMouseEventType::Up;
    pressEvent.pressed = false;
    pressEvent.released = true;
    child->onMouseEvent(pressEvent);
    check(activated == 1, "scrolling exposes and activates the next routing destination");
}

} // namespace

int main() {
    testKeyboardNavigationAndFocusRestoration();
    testSubmenuKeyboardLifecycle();
    testTallMenuScrollingAndViewportContainment();
    NUIComponent::clearFocusedComponent();

    if (failures == 0) {
        std::cout << "Context menu interaction tests passed\n";
        return 0;
    }
    std::cout << failures << " context menu interaction test(s) failed\n";
    return 1;
}
