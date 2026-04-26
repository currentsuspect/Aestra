// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/UnitManager.h"

#include <iostream>

int main() {
    using namespace Aestra::Audio;

    if (arsenalRouteModeFromRouteId(-1) != ArsenalRouteMode::PreviewToMaster) {
        std::cerr << "[FAIL] routeId -1 must map to PreviewToMaster\n";
        return 1;
    }

    if (arsenalRouteModeFromRouteId(-64) != ArsenalRouteMode::PreviewToMaster) {
        std::cerr << "[FAIL] negative routeId must map to PreviewToMaster\n";
        return 1;
    }

    if (arsenalRouteModeFromRouteId(0) != ArsenalRouteMode::RoutedToTimelineTrack) {
        std::cerr << "[FAIL] routeId 0 must map to RoutedToTimelineTrack\n";
        return 1;
    }

    if (arsenalRouteModeFromRouteId(7) != ArsenalRouteMode::RoutedToTimelineTrack) {
        std::cerr << "[FAIL] non-negative routeId must map to RoutedToTimelineTrack\n";
        return 1;
    }

    std::cout << "[PASS] ArsenalRouteModeTest\n";
    return 0;
}
