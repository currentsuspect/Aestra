// © 2026 Aestra Studios — All Rights Reserved.

#include "UIInsertRoutePicker.h"
#include "UIMixerRoutePicker.h"

#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++failures;
    }
}

bool equals(const std::vector<uint32_t>& actual, std::initializer_list<uint32_t> expected) {
    return actual == std::vector<uint32_t>(expected);
}

} // namespace

int main() {
    using AestraUI::UIMixerRoutePicker;

    UIMixerRoutePicker picker;
    picker.setRoutes({{0, 0, "Master", 0}, {11, 1, "Drums", 0}, {42, 12, "Lead Bus", 0}, {99, 120, "Parallel FX", 0}},
                     42);

    check(picker.getSelectedRoute() == 42, "selection must preserve the stable mixer ID");

    picker.setSearchQuery("drum");
    check(equals(picker.getFilteredRouteIds(), {11}), "name search must find a mixer channel");

    picker.setSearchQuery("12");
    const auto numericMatches = picker.getFilteredRouteIds();
    check(equals(numericMatches, {42, 99}), "numeric search must retain prefix matches");
    check(!numericMatches.empty() && numericMatches.front() == 42, "exact channel number must rank first");

    uint32_t routedId = 0;
    picker.setOnRouteSelected([&routedId](uint32_t routeId) { routedId = routeId; });
    check(picker.routeFirstMatch(), "Enter-style first-match routing must succeed");
    check(routedId == 42 && picker.getSelectedRoute() == 42,
          "first-match routing must return the stable ID, not the display number");

    picker.setSearchQuery("channel 120");
    check(equals(picker.getFilteredRouteIds(), {99}), "Channel-prefixed search must normalize correctly");

    picker.setSearchQuery("insert 120");
    check(equals(picker.getFilteredRouteIds(), {99}), "Legacy Insert-prefixed search must remain compatible");

    picker.setSearchQuery("master");
    check(equals(picker.getFilteredRouteIds(), {0}), "Master must remain searchable");

    picker.setSearchQuery("not present");
    check(picker.getFilteredRouteIds().empty(), "unmatched search must not silently choose a route");
    check(!picker.routeFirstMatch(), "routing an empty result must be rejected");

    AestraUI::UIInsertRoutePicker legacyPicker;
    legacyPicker.setRoutes({{0, 0, "Master", 0}, {42, 12, "Lead Bus", 0}}, 42);
    check(legacyPicker.getSelectedRoute() == 42,
          "legacy picker wrapper must remain source-compatible with class forward declarations");
    legacyPicker.setSearchQuery("insert 12");
    check(equals(legacyPicker.getFilteredRouteIds(), {42}),
          "legacy picker wrapper must preserve legacy-prefixed search behavior");

    if (failures != 0) {
        std::cout << failures << " mixer route-picker check(s) failed\n";
        return 1;
    }
    std::cout << "All mixer route-picker checks passed\n";
    return 0;
}
