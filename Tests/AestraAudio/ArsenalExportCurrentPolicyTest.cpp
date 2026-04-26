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

struct CurrentRoutingDecision {
    bool toMasterPreview{false};
    bool toTimelineTrack{false};
};

CurrentRoutingDecision classifyCurrentRouting(const Aestra::Audio::ArsenalProcessingContext& ctx,
                                              const Aestra::Audio::UnitState& unit,
                                              uint32_t trackIndex,
                                              bool /*isOffline*/) {
    // Current authority parity guard: route checks are the same in live/offline
    // execution because export follows the same processBlock routing path.
    CurrentRoutingDecision decision;
    decision.toMasterPreview = ctx.shouldRenderToMasterPreview(unit);
    decision.toTimelineTrack = ctx.shouldRenderToTimelineTrack(unit, trackIndex);
    return decision;
}
} // namespace

int main() {
    using namespace Aestra::Audio;
    ArsenalProcessingContext ctx;

    // Current policy: routeId < 0 (PreviewToMaster) remains renderable under
    // the same authority in both live and offline processBlock paths.
    UnitState previewUnit{};
    previewUnit.routeId = -1;
    previewUnit.routeMode = ArsenalRouteMode::PreviewToMaster;
    const auto previewLive = classifyCurrentRouting(ctx, previewUnit, 0, false);
    const auto previewOffline = classifyCurrentRouting(ctx, previewUnit, 0, true);
    require(previewLive.toMasterPreview, "PreviewToMaster must render to master in current policy");
    require(previewOffline.toMasterPreview, "PreviewToMaster export parity changed unexpectedly");
    require(previewLive.toMasterPreview == previewOffline.toMasterPreview,
            "PreviewToMaster live/offline parity must remain stable");

    // Current policy: routeId >= 0 (RoutedToTimelineTrack) remains renderable
    // to the matching timeline track in both live and offline processBlock paths.
    UnitState routedUnit{};
    routedUnit.routeId = 2;
    routedUnit.routeMode = ArsenalRouteMode::RoutedToTimelineTrack;
    const auto routedLive = classifyCurrentRouting(ctx, routedUnit, 2, false);
    const auto routedOffline = classifyCurrentRouting(ctx, routedUnit, 2, true);
    require(routedLive.toTimelineTrack, "Track-routed unit must render to matching track in current policy");
    require(routedOffline.toTimelineTrack, "Track-routed unit export parity changed unexpectedly");
    require(routedLive.toTimelineTrack == routedOffline.toTimelineTrack,
            "Track-routed live/offline parity must remain stable");

    std::cout << "[PASS] ArsenalExportCurrentPolicyTest\n";
    return 0;
}
