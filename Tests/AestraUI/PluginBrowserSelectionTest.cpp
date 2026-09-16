// © 2026 Aestra Studios — All Rights Reserved.
// Regression coverage for plugin-browser selection identity across filtering.

#include "PluginBrowserPanel.h"

#include <iostream>
#include <vector>

using namespace AestraUI;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << "\n";
        ++failures;
    }
}

PluginListItem plugin(const char* id, const char* name, const char* type, const char* format) {
    PluginListItem item;
    item.id = id;
    item.name = name;
    item.vendor = "Test Vendor";
    item.typeName = type;
    item.formatStr = format;
    return item;
}

void testSelectionClearsWhenPluginIsFilteredOut() {
    PluginBrowserPanel browser;
    browser.setPluginList({plugin("fx-a", "Effect A", "Effect", "VST3"),
                           plugin("inst-b", "Instrument B", "Instrument", "VST3"),
                           plugin("fx-c", "Effect C", "Effect", "VST3")});
    browser.selectPlugin("inst-b");

    browser.setTypeFilter(PluginBrowserPanel::PluginTypeFilter::Effects);

    check(browser.getSelectedPlugin() == nullptr,
          "filtering out the selected plugin must not select the item that inherits its numeric index");
}

void testSelectionFollowsPluginIdentityWhenIndexChanges() {
    PluginBrowserPanel browser;
    browser.setPluginList({plugin("inst-a", "Instrument A", "Instrument", "VST3"),
                           plugin("fx-b", "Effect B", "Effect", "VST3"), plugin("fx-c", "Effect C", "Effect", "VST3")});
    browser.selectPlugin("fx-c");

    browser.setTypeFilter(PluginBrowserPanel::PluginTypeFilter::Effects);

    const PluginListItem* selected = browser.getSelectedPlugin();
    check(selected != nullptr, "selected plugin should remain selected when it still passes the filter");
    check(selected && selected->id == "fx-c", "selection must follow plugin ID when its filtered index changes");
}

NUIKeyEvent keyDown(NUIKeyCode code) {
    NUIKeyEvent e;
    e.keyCode = code;
    e.pressed = true;
    return e;
}

std::vector<PluginListItem> threePlugins() {
    return {plugin("fx-a", "Effect A", "Effect", "VST3"), plugin("inst-b", "Instrument B", "Instrument", "VST3"),
            plugin("fx-c", "Effect C", "Effect", "VST3")};
}

void testKeyboardRequiresFocus() {
    PluginBrowserPanel browser;
    browser.setVisible(true);
    browser.setPluginList(threePlugins());
    // No focus: arrows must not move selection (and must not claim the event).
    check(!browser.onKeyEvent(keyDown(NUIKeyCode::Down)), "unfocused arrows are not consumed");
    check(browser.getSelectedPlugin() == nullptr, "unfocused arrows move nothing");
}

void testKeyboardMovesSelection() {
    PluginBrowserPanel browser;
    browser.setVisible(true);
    browser.setBounds(NUIRect(0.0f, 0.0f, 300.0f, 400.0f));
    browser.setPluginList(threePlugins());
    browser.setFocused(true);
    std::vector<std::string> selected;
    std::vector<std::string> loaded;
    browser.setOnPluginSelected([&selected](const PluginListItem& item) { selected.push_back(item.id); });
    browser.setOnPluginLoadRequested([&loaded](const PluginListItem& item) { loaded.push_back(item.id); });

    browser.onKeyEvent(keyDown(NUIKeyCode::Down));
    check(browser.getSelectedPlugin() && browser.getSelectedPlugin()->id == "fx-a",
          "Down from empty starts at the top");
    browser.onKeyEvent(keyDown(NUIKeyCode::Down));
    check(browser.getSelectedPlugin() && browser.getSelectedPlugin()->id == "inst-b",
          "Down advances again");
    browser.onKeyEvent(keyDown(NUIKeyCode::Up));
    check(browser.getSelectedPlugin() && browser.getSelectedPlugin()->id == "fx-a", "Up retreats");
    browser.onKeyEvent(keyDown(NUIKeyCode::Up));
    check(browser.getSelectedPlugin() && browser.getSelectedPlugin()->id == "fx-a",
          "Up clamps at the top without firing");
    check(selected.size() == 3, "selection callback fires per move only");

    browser.onKeyEvent(keyDown(NUIKeyCode::Enter));
    check(loaded.size() == 1 && loaded.back() == "fx-a", "Enter loads the highlighted plugin");
    check(browser.isFocused(), "Enter keeps focus for continued navigation");
}

void testKeyboardEnterWithoutSelectionLoadsNothing() {
    PluginBrowserPanel browser;
    browser.setVisible(true);
    browser.setPluginList(threePlugins());
    browser.setFocused(true);
    bool loaded = false;
    browser.setOnPluginLoadRequested([&loaded](const PluginListItem&) { loaded = true; });
    browser.onKeyEvent(keyDown(NUIKeyCode::Enter));
    check(!loaded, "Enter with no selection loads nothing");
}

void testKeyboardScrollsSelectionIntoView() {
    PluginBrowserPanel browser;
    browser.setVisible(true);
    browser.setBounds(NUIRect(0.0f, 0.0f, 300.0f, 200.0f));
    std::vector<PluginListItem> many;
    for (int i = 0; i < 20; ++i) {
        many.push_back(plugin(("fx-" + std::to_string(i)).c_str(), ("Effect " + std::to_string(i)).c_str(), "Effect",
                              "VST3"));
    }
    browser.setPluginList(many);
    browser.setFocused(true);
    for (int i = 0; i < 19; ++i) {
        browser.onKeyEvent(keyDown(NUIKeyCode::Down));
    }
    check(browser.getSelectedPlugin() && browser.getSelectedPlugin()->id == "fx-18",
          "repeated Down reaches the last item");
    for (int i = 0; i < 120; ++i) {
        browser.onUpdate(1.0 / 60.0);
    }
    check(browser.getTargetScrollOffset() > 0.0f, "the list scrolls to follow keyboard selection");
}

} // namespace

int main() {
    testSelectionClearsWhenPluginIsFilteredOut();
    testSelectionFollowsPluginIdentityWhenIndexChanges();
    testKeyboardRequiresFocus();
    testKeyboardMovesSelection();
    testKeyboardEnterWithoutSelectionLoadsNothing();
    testKeyboardScrollsSelectionIntoView();

    if (failures != 0) {
        std::cout << failures << " plugin-browser selection check(s) failed\n";
        return 1;
    }

    std::cout << "All plugin-browser selection checks passed\n";
    return 0;
}
