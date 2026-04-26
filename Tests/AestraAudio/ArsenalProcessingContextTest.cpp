// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/ArsenalProcessingContext.h"
#include "Models/UnitManager.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    {
        ArsenalProcessingContext empty;
        require(!empty.hasUnits(), "Empty context should report no units");
        require(!empty.getSnapshot(), "Empty context snapshot should be null");
    }

    UnitManager manager;
    ArsenalProcessingContext ctx(&manager);
    require(ctx.getSnapshot() != nullptr, "Context with UnitManager should return snapshot");
    require(!ctx.hasUnits(), "Fresh UnitManager should have no units");

    const UnitID unitId = manager.createUnit("Ctx Unit", UnitType::Sampler);
    manager.setUnitEnabled(unitId, true);
    manager.assignUnitToTimelineLane(unitId, 2);

    auto snapshot = ctx.getSnapshot();
    require(snapshot != nullptr, "Snapshot should exist after adding unit");
    require(snapshot->units.size() == 1, "Snapshot should contain one unit");
    require(ArsenalProcessingContext::routesToTimelineTrack(snapshot->units[0].routeId),
            "Route helper should classify timeline route");

    manager.clearUnitTimelineLane(unitId);
    snapshot = ctx.getSnapshot();
    require(snapshot != nullptr, "Snapshot should still exist");
    require(snapshot->units.size() == 1, "Snapshot should still contain one unit");
    require(ArsenalProcessingContext::routesToMasterPreview(snapshot->units[0].routeId),
            "Route helper should classify master preview route");

    std::cout << "[PASS] ArsenalProcessingContextTest\n";
    return 0;
}
