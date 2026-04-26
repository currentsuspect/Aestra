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

    ArsenalProcessingContext empty;
    require(!empty.hasUnits(), "Empty context should report no units");
    require(!empty.getSnapshot(), "Empty context snapshot should be null");

    UnitManager manager;
    ArsenalProcessingContext ctx(&manager);
    require(ctx.getSnapshot() != nullptr, "Context with UnitManager should return snapshot");
    require(!ctx.hasUnits(), "Fresh UnitManager should have no units");

    const UnitID timelineUnit = manager.createUnit("Timeline Unit", UnitType::Sampler);
    manager.assignUnitToTimelineLane(timelineUnit, 2);

    const UnitID previewUnit = manager.createUnit("Preview Unit", UnitType::Sampler);
    manager.clearUnitTimelineLane(previewUnit);

    auto snapshot = ctx.getSnapshot();
    require(snapshot != nullptr, "Snapshot should exist after adding units");
    require(snapshot->units.size() == 2, "Snapshot should contain two units");

    const UnitState* timelineState = nullptr;
    const UnitState* previewState = nullptr;
    for (const auto& unit : snapshot->units) {
        if (unit.id == static_cast<int>(timelineUnit)) {
            timelineState = &unit;
        } else if (unit.id == static_cast<int>(previewUnit)) {
            previewState = &unit;
        }
    }

    require(timelineState != nullptr, "Timeline unit state missing");
    require(previewState != nullptr, "Preview unit state missing");

    require(ctx.shouldRenderToTimelineTrack(*timelineState, 2),
            "Timeline unit should render for its assigned track");
    require(!ctx.shouldRenderToTimelineTrack(*timelineState, 1),
            "Timeline unit should not render for another track");
    require(!ctx.shouldRenderToMasterPreview(*timelineState),
            "Timeline-routed unit should not render to master preview");

    require(ctx.shouldRenderToMasterPreview(*previewState),
            "Preview unit should render to master preview");
    require(!ctx.shouldRenderToTimelineTrack(*previewState, 2),
            "Preview unit should not render to timeline tracks");

    std::cout << "[PASS] ArsenalProcessingContextRoutingTest\n";
    return 0;
}
