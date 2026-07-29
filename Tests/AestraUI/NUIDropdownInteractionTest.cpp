// © 2026 Aestra Studios — All Rights Reserved.
// Regression coverage for shared select/dropdown interaction behavior.

#include "NUIDropdown.h"

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

NUIMouseEvent leftPress(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Down;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    event.pressed = true;
    return event;
}

NUIMouseEvent wheel(float x, float y, float delta) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Scroll;
    event.position = {x, y};
    event.wheelDelta = delta;
    return event;
}

NUIKeyEvent keyPress(NUIKeyCode key) {
    NUIKeyEvent event;
    event.keyCode = key;
    event.pressed = true;
    return event;
}

std::shared_ptr<NUIDropdown> makeDropdown(float x = 10.0f) {
    auto dropdown = std::make_shared<NUIDropdown>();
    dropdown->setBounds(x, 10.0f, 140.0f, 32.0f);
    return dropdown;
}

void open(const std::shared_ptr<NUIDropdown>& dropdown) {
    const auto bounds = dropdown->getBounds();
    check(dropdown->onMouseEvent(leftPress(bounds.x + 4.0f, bounds.y + 4.0f)), "trigger press handled");
    check(dropdown->isOpen(), "dropdown opened");
}

void testHiddenRowsDoNotOccupyMenuSlots() {
    auto dropdown = makeDropdown();
    dropdown->addItem("Hidden", 0);
    dropdown->addItem("First visible", 1);
    dropdown->addItem("Second visible", 2);
    dropdown->setItemVisible(0, false);

    open(dropdown);
    check(dropdown->onMouseEvent(leftPress(20.0f, 50.0f)), "visible row press handled");
    check(dropdown->getSelectedValue() == 1, "first rendered row maps to first visible item");
}

void testDisabledRowsDoNotDismissMenu() {
    auto dropdown = makeDropdown();
    dropdown->addItem("Disabled", 0);
    dropdown->addItem("Enabled", 1);
    dropdown->setItemEnabled(0, false);

    open(dropdown);
    check(dropdown->onMouseEvent(leftPress(20.0f, 50.0f)), "disabled row press consumed");
    check(dropdown->isOpen(), "disabled row does not dismiss the menu");
    check(dropdown->getSelectedIndex() == -1, "disabled row is not selected");
}

void testKeyboardNavigationPreviewsAndSkipsUnavailableItems() {
    auto dropdown = makeDropdown();
    dropdown->addItem("Current", 0);
    dropdown->addItem("Hidden", 1);
    dropdown->addItem("Disabled", 2);
    dropdown->addItem("Next", 3);
    dropdown->setItemVisible(1, false);
    dropdown->setItemEnabled(2, false);
    dropdown->setSelectedIndex(0);
    dropdown->setFocused(true);
    int callbackCount = 0;
    dropdown->setOnSelectionChanged([&callbackCount](int) { ++callbackCount; });

    open(dropdown);
    check(dropdown->onKeyEvent(keyPress(NUIKeyCode::Down)), "down key handled");
    check(dropdown->getSelectedValue() == 0, "navigation does not commit before confirmation");
    check(callbackCount == 0, "navigation preview does not fire selection callbacks");
    check(dropdown->onKeyEvent(keyPress(NUIKeyCode::Enter)), "enter key handled");
    check(dropdown->getSelectedValue() == 3, "enter commits the next enabled visible item");
    check(callbackCount == 1, "confirmation fires one selection callback");
    check(!dropdown->isOpen(), "enter closes after committing");
}

void testLongMenusScrollToReachEveryItem() {
    auto dropdown = makeDropdown();
    dropdown->setMaxVisibleItems(2);
    dropdown->addItem("Zero", 0);
    dropdown->addItem("One", 1);
    dropdown->addItem("Two", 2);
    dropdown->addItem("Three", 3);

    open(dropdown);
    check(dropdown->onMouseEvent(wheel(20.0f, 60.0f, -1.0f)), "menu wheel handled");
    check(dropdown->onMouseEvent(leftPress(20.0f, 80.0f)), "scrolled row press handled");
    check(dropdown->getSelectedValue() == 2, "scroll exposes and selects items beyond the initial window");
}

void testSwitchingDropdownsTakesOneClick() {
    auto root = std::make_shared<NUIComponent>();
    root->setBounds(0.0f, 0.0f, 400.0f, 200.0f);
    auto first = makeDropdown(10.0f);
    auto second = makeDropdown(170.0f);
    first->addItem("A", 1);
    second->addItem("B", 2);
    root->addChild(first);
    root->addChild(second);

    check(root->onMouseEvent(leftPress(14.0f, 14.0f)), "first trigger press handled through parent");
    check(first->isOpen(), "first dropdown opened through parent");
    check(root->onMouseEvent(leftPress(174.0f, 14.0f)), "second trigger press handled through parent");
    check(!first->isOpen(), "opening another dropdown closes the previous one");
    check(second->isOpen(), "second dropdown opens on the same click");
}

void testMenuFlipsAboveWhenLowerViewportEdgeIsTight() {
    auto root = std::make_shared<NUIComponent>();
    root->setBounds(0.0f, 0.0f, 300.0f, 160.0f);
    auto dropdown = std::make_shared<NUIDropdown>();
    dropdown->setBounds(10.0f, 120.0f, 140.0f, 32.0f);
    dropdown->addItem("First", 1);
    dropdown->addItem("Second", 2);
    root->addChild(dropdown);

    check(root->onMouseEvent(leftPress(14.0f, 124.0f)), "bottom-edge trigger press handled");
    check(dropdown->isOpen(), "bottom-edge dropdown opened");
    check(root->onMouseEvent(leftPress(20.0f, 66.0f)), "row above the trigger handled");
    check(dropdown->getSelectedValue() == 1, "menu flips above instead of rendering beyond viewport");
}

void testHidingSelectionDoesNotLeaveInvisibleValueDisplayed() {
    auto dropdown = makeDropdown();
    dropdown->addItem("Visible", 1);
    dropdown->addItem("Will hide", 2);
    dropdown->setSelectedIndex(1);
    dropdown->setItemVisible(1, false);

    check(dropdown->getSelectedIndex() == -1, "hiding the selected item clears stale selection");

    // Pin what the two value accessors report in the no-selection state, not
    // just the index. This is the product policy, and a consumer has already
    // depended on it: getSelectedValue() returns 0 here, which is
    // indistinguishable from the item whose value IS 0, so it cannot be used to
    // detect "nothing selected". getSelectedItem() is the accessor that can say
    // nothing without inventing a legal-looking value. PR #657 persists audio
    // device ids and would have written this 0 over a real device.
    check(!dropdown->getSelectedItem().has_value(),
          "no selection reports std::nullopt — the unambiguous accessor");
    check(dropdown->getSelectedValue() == 0,
          "getSelectedValue() reports 0 with nothing selected; it is NOT a selection signal");

    // The other half of the discriminator: a genuine selection whose value is 0
    // must be reportable, so consumers cannot special-case 0 as "absent".
    auto zeroValued = makeDropdown();
    zeroValued->addItem("Device zero", 0);
    zeroValued->setSelectedIndex(0);
    check(zeroValued->getSelectedItem().has_value(), "a real selection reports a value");
    check(zeroValued->getSelectedValue() == 0, "a real selection of value 0 reports 0");

    // Hiding must clear the selection rather than silently moving it to another
    // visible row. Auto-advancing would change the user's choice without asking,
    // which for a settings control is a silent intent change.
    auto multi = makeDropdown();
    multi->addItem("First", 1);
    multi->addItem("Chosen", 2);
    multi->addItem("Third", 3);
    multi->setSelectedIndex(1);
    multi->setItemVisible(1, false);
    check(multi->getSelectedIndex() == -1, "hiding the selection does not auto-advance to a neighbour");
    check(!multi->getSelectedItem().has_value(), "and leaves no phantom selection behind");
}

} // namespace

int main() {
    testHiddenRowsDoNotOccupyMenuSlots();
    testDisabledRowsDoNotDismissMenu();
    testKeyboardNavigationPreviewsAndSkipsUnavailableItems();
    testLongMenusScrollToReachEveryItem();
    testSwitchingDropdownsTakesOneClick();
    testMenuFlipsAboveWhenLowerViewportEdgeIsTight();
    testHidingSelectionDoesNotLeaveInvisibleValueDisplayed();

    if (g_failures != 0) {
        std::cout << g_failures << " dropdown interaction check(s) failed\n";
        return 1;
    }

    std::cout << "All dropdown interaction checks passed\n";
    return 0;
}
