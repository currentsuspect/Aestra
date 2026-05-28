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

    // Draft exists as an explicit enum value.
    require(static_cast<int>(ArsenalRouteMode::Draft) != static_cast<int>(ArsenalRouteMode::PreviewToMaster),
            "Draft must remain a distinct route mode value");

    // Current authority remains routeId mapping (PreviewToMaster for <0, RoutedToTimelineTrack for >=0).
    require(arsenalRouteModeFromRouteId(-1) == ArsenalRouteMode::PreviewToMaster,
            "routeId < 0 must map to PreviewToMaster");
    require(arsenalRouteModeFromRouteId(0) == ArsenalRouteMode::RoutedToTimelineTrack,
            "routeId >= 0 must map to RoutedToTimelineTrack");

    // Draft is currently inactive scaffolding; route decisions still follow routeId.
    ArsenalProcessingContext ctx;
    UnitState draftPreview{};
    draftPreview.routeId = -1;
    draftPreview.routeMode = ArsenalRouteMode::Draft;
    require(ctx.shouldRenderToMasterPreview(draftPreview),
            "Draft must not override current preview path compatibility");
    require(!ctx.shouldRenderToTimelineTrack(draftPreview, 0),
            "Draft preview route should not render to timeline track");

    UnitState draftTrack{};
    draftTrack.routeId = 3;
    draftTrack.routeMode = ArsenalRouteMode::Draft;
    require(ctx.shouldRenderToTimelineTrack(draftTrack, 3),
            "Draft must not override current timeline route compatibility");
    require(!ctx.shouldRenderToMasterPreview(draftTrack),
            "Draft timeline route should not render to preview path");

    // Disagreement between routeMode and routeId resolves non-fatally to routeId behavior.
    UnitManager manager;
    Aestra::JSON root = Aestra::JSON::object();
    root.set("nextId", Aestra::JSON(2.0));
    Aestra::JSON units = Aestra::JSON::array();
    Aestra::JSON unit = Aestra::JSON::object();
    unit.set("id", Aestra::JSON(1.0));
    unit.set("name", Aestra::JSON("Policy"));
    unit.set("enabled", Aestra::JSON(true));
    unit.set("targetMixerRoute", Aestra::JSON(4.0)); // timeline route authority
    Aestra::JSON routeMode = Aestra::JSON::object();
    routeMode.set("id", Aestra::JSON(static_cast<double>(static_cast<uint8_t>(ArsenalRouteMode::PreviewToMaster))));
    routeMode.set("name", Aestra::JSON("PreviewToMaster"));
    unit.set("routeMode", routeMode);
    units.push(unit);
    root.set("units", units);
    manager.loadFromJSON(root);

    const UnitInfo* loaded = manager.getUnit(1);
    require(loaded != nullptr, "Policy unit failed to load");
    require(loaded->targetMixerRoute == 4, "routeId must remain unchanged");
    require(loaded->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
            "routeMode must resolve to routeId-compatible behavior");

    // routeMode field round-trips in current schema (compatibility fielding from Phase 2A).
    const Aestra::JSON saved = manager.saveToJSON();
    require(saved.has("units") && saved["units"].isArray() && saved["units"].size() == 1,
            "Saved policy units array invalid");
    require(saved["units"][0].has("routeMode"), "Serialized unit should include routeMode compatibility field");

    PluginInfo internalPlugin{};
    internalPlugin.id = "com.Aestrastudios.sampler";
    internalPlugin.name = "Aestra Sampler";
    internalPlugin.format = PluginFormat::Internal;
    require(shouldRestoreArsenalPluginFromProject(internalPlugin),
            "Project load may restore first-party/internal Arsenal plugins");

    PluginInfo vstPlugin = internalPlugin;
    vstPlugin.id = "com.vendor.instrument";
    vstPlugin.format = PluginFormat::VST3;
    require(!shouldRestoreArsenalPluginFromProject(vstPlugin),
            "Project load must not auto-instantiate VST3 Arsenal plugins from untrusted JSON");

    PluginInfo clapPlugin = internalPlugin;
    clapPlugin.id = "com.vendor.clap-instrument";
    clapPlugin.format = PluginFormat::CLAP;
    require(!shouldRestoreArsenalPluginFromProject(clapPlugin),
            "Project load must not auto-instantiate CLAP Arsenal plugins from untrusted JSON");

    std::cout << "[PASS] ArsenalRouteModePolicyTest\n";
    return 0;
}
