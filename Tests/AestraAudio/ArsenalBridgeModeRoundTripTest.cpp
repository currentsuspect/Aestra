// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/ArsenalBridgeMode.h"
#include "Models/UnitManager.h"

#include <array>
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

    constexpr std::array<ArsenalBridgeMode, 6> kModes = {
        ArsenalBridgeMode::DraftOnly,
        ArsenalBridgeMode::PreviewToMaster,
        ArsenalBridgeMode::LinkedRack,
        ArsenalBridgeMode::LocalCopy,
        ArsenalBridgeMode::RenderedAudio,
        ArsenalBridgeMode::FrozenAudio,
    };

    // Each bridge mode serializes/deserializes without touching route metadata.
    for (ArsenalBridgeMode mode : kModes) {
        UnitManager source;
        const UnitID id = source.createUnit("BridgeRoundTrip", UnitType::Sampler);
        source.assignUnitToTimelineLane(id, 2);
        auto* unit = source.getUnit(id);
        require(unit != nullptr, "Unit creation failed");
        unit->bridgeMode = mode;

        const Aestra::JSON saved = source.saveToJSON();
        UnitManager loaded;
        loaded.loadFromJSON(saved);

        const UnitInfo* loadedUnit = loaded.getUnit(id);
        require(loadedUnit != nullptr, "Round-trip load failed for bridge mode");
        require(loadedUnit->bridgeMode == mode, "Bridge mode did not round-trip");
        require(loadedUnit->targetMixerRoute == 2, "routeId drifted during bridge round-trip");
        require(loadedUnit->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
                "routeMode drifted during bridge round-trip");
    }

    // Missing bridgeMode loads safely using routeId-compatible fallback.
    {
        UnitManager legacy;
        Aestra::JSON root = Aestra::JSON::object();
        root.set("nextId", Aestra::JSON(2.0));
        Aestra::JSON units = Aestra::JSON::array();
        Aestra::JSON u = Aestra::JSON::object();
        u.set("id", Aestra::JSON(1.0));
        u.set("name", Aestra::JSON("Legacy"));
        u.set("enabled", Aestra::JSON(true));
        u.set("targetMixerRoute", Aestra::JSON(-1.0));
        units.push(u);
        root.set("units", units);
        legacy.loadFromJSON(root);
        const UnitInfo* loaded = legacy.getUnit(1);
        require(loaded != nullptr, "Missing bridgeMode legacy load failed");
        require(loaded->bridgeMode == ArsenalBridgeMode::PreviewToMaster,
                "Missing bridgeMode fallback mismatch for preview route");
        require(loaded->routeMode == ArsenalRouteMode::PreviewToMaster,
                "routeMode changed in missing bridgeMode fallback");
    }

    // Invalid bridgeMode loads safely with routeId-compatible fallback.
    {
        UnitManager invalid;
        Aestra::JSON root = Aestra::JSON::object();
        root.set("nextId", Aestra::JSON(2.0));
        Aestra::JSON units = Aestra::JSON::array();
        Aestra::JSON u = Aestra::JSON::object();
        u.set("id", Aestra::JSON(1.0));
        u.set("name", Aestra::JSON("InvalidBridge"));
        u.set("enabled", Aestra::JSON(true));
        u.set("targetMixerRoute", Aestra::JSON(5.0));
        u.set("bridgeMode", Aestra::JSON("unknown-future-mode"));
        units.push(u);
        root.set("units", units);
        invalid.loadFromJSON(root);
        const UnitInfo* loaded = invalid.getUnit(1);
        require(loaded != nullptr, "Invalid bridgeMode load failed");
        require(loaded->bridgeMode == ArsenalBridgeMode::LinkedRack,
                "Invalid bridgeMode fallback mismatch for timeline route");
        require(loaded->targetMixerRoute == 5, "routeId changed in invalid bridge fallback");
        require(loaded->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
                "routeMode changed in invalid bridge fallback");
    }

    // Repeated save/load round-trips should not drift bridge metadata.
    {
        UnitManager manager;
        const UnitID id = manager.createUnit("NoDrift", UnitType::Sampler);
        manager.assignUnitToTimelineLane(id, 1);
        auto* unit = manager.getUnit(id);
        require(unit != nullptr, "NoDrift unit creation failed");
        unit->bridgeMode = ArsenalBridgeMode::RenderedAudio;

        Aestra::JSON current = manager.saveToJSON();
        for (int i = 0; i < 3; ++i) {
            UnitManager next;
            next.loadFromJSON(current);
            const UnitInfo* loaded = next.getUnit(id);
            require(loaded != nullptr, "NoDrift load failed");
            require(loaded->bridgeMode == ArsenalBridgeMode::RenderedAudio, "Bridge mode drifted across round-trips");
            require(loaded->targetMixerRoute == 1, "routeId drifted across round-trips");
            require(loaded->routeMode == ArsenalRouteMode::RoutedToTimelineTrack,
                    "routeMode drifted across round-trips");
            current = next.saveToJSON();
        }
    }

    std::cout << "[PASS] ArsenalBridgeModeRoundTripTest\n";
    return 0;
}
