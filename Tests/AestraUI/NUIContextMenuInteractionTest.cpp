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

// A context menu is attached to the *root* component so it can draw outside its
// owner's bounds, which means it outlives the component that built it, while its
// callbacks capture that component. Destroying the owner with the menu still up
// (refreshTracks() rebuilding lanes is the common way) therefore left every
// callback holding a dangling pointer.
void testOwnerLifetimeGatesCallbacks() {
    NUIComponent::clearFocusedComponent();

    // No owner bound: menus that are not owned by a component must be untouched.
    // A default-constructed weak_ptr is already expired, so this is the case a
    // naive expired() check would break.
    {
        auto menu = std::make_shared<NUIContextMenu>();
        int activations = 0;
        menu->addItem("Copy", [&]() { ++activations; });
        check(menu->hasLiveOwner(), "a menu with no owner bound is never gated");
        menu->showAt(10, 10);
        menu->onKeyEvent(press(NUIKeyCode::Enter));
        check(activations == 1, "an un-owned menu still activates its items");
    }

    // Live owner. Without this half, the dead-owner checks below would pass just
    // as happily if the gate blocked every callback unconditionally.
    {
        auto owner = std::make_shared<NUIComponent>();
        auto menu = std::make_shared<NUIContextMenu>();
        int activations = 0;
        int hides = 0;
        menu->addItem("Copy", [&]() { ++activations; });
        menu->setOnHide([&]() { ++hides; });
        menu->setOwner(owner);
        check(menu->hasLiveOwner(), "a bound, live owner reports live");
        menu->showAt(10, 10);
        menu->onKeyEvent(press(NUIKeyCode::Enter));
        check(activations == 1, "a live owner does not block item activation");
        check(hides == 1, "a live owner does not block the hide handler");
    }

    // Dead owner: the use-after-free. Both callbacks below would have run
    // against freed owner state.
    {
        auto doomed = std::make_shared<NUIComponent>();
        auto menu = std::make_shared<NUIContextMenu>();
        int activations = 0;
        int hides = 0;
        menu->addItem("Delete Clip", [&]() { ++activations; });
        menu->setOnHide([&]() { ++hides; });
        menu->setOwner(doomed);
        menu->showAt(10, 10);

        doomed.reset(); // refreshTracks() destroying the lane component

        check(!menu->hasLiveOwner(), "a destroyed owner reports dead");
        menu->onKeyEvent(press(NUIKeyCode::Enter));
        check(activations == 0, "a destroyed owner blocks item activation");
        check(hides == 0, "a destroyed owner blocks the hide handler");
        check(!menu->isVisible(), "activating against a dead owner dismisses the menu");
    }

    NUIComponent::clearFocusedComponent();
}

// A submenu is a separate NUIContextMenu: onMouseEvent forwards to
// activeSubmenu_, and it consults its own gate. Binding only the parent left the
// deepest level ungated while its items captured the same component —
// TrackUIComponent's "Route Linked Clips" submenu is exactly that shape, and its
// radio callbacks dereference m_trackManager. Both attach orders are covered
// because showClipRoutingMenu attaches the submenu *before* the owner is bound.
void testSubmenuOwnerPropagation() {
    NUIComponent::clearFocusedComponent();

    // Live owner first: without this, the dead-owner half below would pass even
    // if propagation disabled submenus outright.
    {
        auto owner = std::make_shared<NUIComponent>();
        auto menu = std::make_shared<NUIContextMenu>();
        auto attachedFirst = std::make_shared<NUIContextMenu>();
        int activations = 0;
        attachedFirst->addItem("Master", [&]() { ++activations; });
        menu->addSubmenu("Route Linked Clips", attachedFirst);
        menu->setOwner(owner);

        auto attachedLater = std::make_shared<NUIContextMenu>();
        attachedLater->addItem("Channel 1", [&]() { activations += 10; });
        menu->addSubmenu("Added after binding", attachedLater);

        check(attachedFirst->hasLiveOwner(), "binding a parent reaches an already-attached submenu");
        check(attachedLater->hasLiveOwner(), "a submenu attached after binding inherits the owner");

        attachedFirst->showAt(20, 20);
        attachedFirst->onKeyEvent(press(NUIKeyCode::Enter));
        check(activations == 1, "a live owner does not block submenu item activation");
    }

    // Dead owner: this is the hole the top-level gate alone left open.
    {
        auto doomed = std::make_shared<NUIComponent>();
        auto menu = std::make_shared<NUIContextMenu>();
        auto submenu = std::make_shared<NUIContextMenu>();
        auto nested = std::make_shared<NUIContextMenu>();
        int activations = 0;
        nested->addItem("Deepest", [&]() { activations += 100; });
        submenu->addItem("Master", [&]() { ++activations; });
        submenu->addSubmenu("Nested", nested);
        menu->addSubmenu("Route Linked Clips", submenu);
        menu->setOwner(doomed);

        // Deliberately no live-owner assertion on `nested` here: hasLiveOwner()
        // is true both when the owner propagated and when nothing was ever
        // bound, so it would pass for the wrong reason. The dead-owner checks
        // below are what actually prove the owner reached this depth.
        menu->showAt(10, 10);
        doomed.reset(); // refreshTracks() destroying the lane component

        check(!submenu->hasLiveOwner(), "a destroyed owner reaches the submenu gate");
        check(!nested->hasLiveOwner(), "a destroyed owner reaches the nested submenu gate");

        submenu->showAt(20, 20);
        submenu->onKeyEvent(press(NUIKeyCode::Enter));
        check(activations == 0, "a destroyed owner blocks submenu item activation");

        nested->showAt(30, 30);
        nested->onKeyEvent(press(NUIKeyCode::Enter));
        check(activations == 0, "a destroyed owner blocks nested submenu item activation");
    }

    NUIComponent::clearFocusedComponent();
}

} // namespace

int main() {
    testKeyboardNavigationAndFocusRestoration();
    testSubmenuKeyboardLifecycle();
    testTallMenuScrollingAndViewportContainment();
    testOwnerLifetimeGatesCallbacks();
    testSubmenuOwnerPropagation();
    NUIComponent::clearFocusedComponent();

    if (failures == 0) {
        std::cout << "Context menu interaction tests passed\n";
        return 0;
    }
    std::cout << failures << " context menu interaction test(s) failed\n";
    return 1;
}
