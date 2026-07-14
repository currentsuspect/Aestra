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

} // namespace

int main() {
    testSelectionClearsWhenPluginIsFilteredOut();
    testSelectionFollowsPluginIdentityWhenIndexChanges();

    if (failures != 0) {
        std::cout << failures << " plugin-browser selection check(s) failed\n";
        return 1;
    }

    std::cout << "All plugin-browser selection checks passed\n";
    return 0;
}
