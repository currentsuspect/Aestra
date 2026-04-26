// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/ArsenalProcessingContext.h"
#include "Models/UnitManager.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

void verifyRouteIdMapping() {
    using namespace Aestra::Audio;
    require(arsenalRouteModeFromRouteId(-1) == ArsenalRouteMode::PreviewToMaster,
            "routeId < 0 must map to PreviewToMaster");
    require(arsenalRouteModeFromRouteId(-64) == ArsenalRouteMode::PreviewToMaster,
            "negative routeId must map to PreviewToMaster");
    require(arsenalRouteModeFromRouteId(0) == ArsenalRouteMode::RoutedToTimelineTrack,
            "routeId == 0 must map to RoutedToTimelineTrack");
    require(arsenalRouteModeFromRouteId(7) == ArsenalRouteMode::RoutedToTimelineTrack,
            "routeId >= 0 must map to RoutedToTimelineTrack");
}

void verifyLegacyRouteIdOnlyLoad() {
    using namespace Aestra::Audio;
    UnitManager manager;
    Aestra::JSON root = Aestra::JSON::object();
    root.set("nextId", Aestra::JSON(2.0));
    Aestra::JSON units = Aestra::JSON::array();
    Aestra::JSON unit = Aestra::JSON::object();
    unit.set("id", Aestra::JSON(1.0));
    unit.set("name", Aestra::JSON("Legacy"));
    unit.set("enabled", Aestra::JSON(true));
    unit.set("targetMixerRoute", Aestra::JSON(-1.0));
    units.push(unit);
    root.set("units", units);

    manager.loadFromJSON(root);
    const UnitInfo* loaded = manager.getUnit(1);
    require(loaded != nullptr, "Legacy unit failed to load");
    require(loaded->targetMixerRoute < 0, "Legacy unit routeId mismatch");
    require(loaded->routeMode == ArsenalRouteMode::PreviewToMaster,
            "Legacy routeId-only unit must resolve to PreviewToMaster");
}

void verifyExplicitRouteModeCompatibility() {
    using namespace Aestra::Audio;
    UnitManager manager;
    Aestra::JSON root = Aestra::JSON::object();
    root.set("nextId", Aestra::JSON(2.0));
    Aestra::JSON units = Aestra::JSON::array();
    Aestra::JSON unit = Aestra::JSON::object();
    unit.set("id", Aestra::JSON(1.0));
    unit.set("name", Aestra::JSON("Mismatch"));
    unit.set("enabled", Aestra::JSON(true));
    unit.set("targetMixerRoute", Aestra::JSON(3.0));
    Aestra::JSON routeMode = Aestra::JSON::object();
    routeMode.set("id", Aestra::JSON(static_cast<double>(static_cast<uint8_t>(ArsenalRouteMode::PreviewToMaster))));
    routeMode.set("name", Aestra::JSON("PreviewToMaster"));
    unit.set("routeMode", routeMode);
    units.push(unit);
    root.set("units", units);

    manager.loadFromJSON(root);
    const UnitInfo* loaded = manager.getUnit(1);
    require(loaded != nullptr, "Mismatched routeMode unit failed to load");
    require(loaded->targetMixerRoute == 3, "routeId should remain source-of-truth");
    require(loaded->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
            "Explicit routeMode must remain routeId-compatible in current behavior");
}

void verifyDraftIsScaffoldingOnly() {
    using namespace Aestra::Audio;
    ArsenalProcessingContext ctx;

    UnitState previewDraft{};
    previewDraft.enabled = true;
    previewDraft.routeId = -1;
    previewDraft.routeMode = ArsenalRouteMode::Draft;
    require(ctx.shouldRenderToMasterPreview(previewDraft),
            "Draft must not suppress current preview-to-master behavior");
    require(!ctx.shouldRenderToTimelineTrack(previewDraft, 0),
            "Preview draft should not route to timeline track");

    UnitState trackDraft{};
    trackDraft.enabled = true;
    trackDraft.routeId = 2;
    trackDraft.routeMode = ArsenalRouteMode::Draft;
    require(ctx.shouldRenderToTimelineTrack(trackDraft, 2),
            "Draft must not suppress current routeId-based timeline routing");
    require(!ctx.shouldRenderToMasterPreview(trackDraft),
            "Track-routed draft should not route to preview path");
}
} // namespace

int main() {
    verifyRouteIdMapping();
    verifyLegacyRouteIdOnlyLoad();
    verifyExplicitRouteModeCompatibility();
    verifyDraftIsScaffoldingOnly();

    std::cout << "[PASS] ArsenalRouteModeCompatibilityTest\n";
    return 0;
}
