// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/ArsenalBridgeMode.h"
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

    // Stable mode set and stable string mapping contract.
    require(toString(ArsenalBridgeMode::DraftOnly) == "DraftOnly", "DraftOnly bridge token changed");
    require(toString(ArsenalBridgeMode::PreviewToMaster) == "PreviewToMaster", "PreviewToMaster bridge token changed");
    require(toString(ArsenalBridgeMode::LinkedRack) == "LinkedRack", "LinkedRack bridge token changed");
    require(toString(ArsenalBridgeMode::LocalCopy) == "LocalCopy", "LocalCopy bridge token changed");
    require(toString(ArsenalBridgeMode::RenderedAudio) == "RenderedAudio", "RenderedAudio bridge token changed");
    require(toString(ArsenalBridgeMode::FrozenAudio) == "FrozenAudio", "FrozenAudio bridge token changed");

    require(arsenalBridgeModeFromString("DraftOnly") == ArsenalBridgeMode::DraftOnly, "DraftOnly parse failed");
    require(arsenalBridgeModeFromString("PreviewToMaster") == ArsenalBridgeMode::PreviewToMaster,
            "PreviewToMaster parse failed");
    require(arsenalBridgeModeFromString("LinkedRack") == ArsenalBridgeMode::LinkedRack, "LinkedRack parse failed");
    require(arsenalBridgeModeFromString("LocalCopy") == ArsenalBridgeMode::LocalCopy, "LocalCopy parse failed");
    require(arsenalBridgeModeFromString("RenderedAudio") == ArsenalBridgeMode::RenderedAudio,
            "RenderedAudio parse failed");
    require(arsenalBridgeModeFromString("FrozenAudio") == ArsenalBridgeMode::FrozenAudio, "FrozenAudio parse failed");
    require(!arsenalBridgeModeFromString("invalid").has_value(), "Invalid bridge token must parse non-fatally");

    // Bridge metadata must remain independent from route-mode compatibility.
    require(arsenalRouteModeFromRouteId(-1) == ArsenalRouteMode::PreviewToMaster,
            "Route mode compatibility changed for preview routing");
    require(arsenalRouteModeFromRouteId(0) == ArsenalRouteMode::RoutedToTimelineTrack,
            "Route mode compatibility changed for timeline routing");

    std::cout << "[PASS] ArsenalBridgeContractTest\n";
    return 0;
}
