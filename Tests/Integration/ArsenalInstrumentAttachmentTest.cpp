// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

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

    auto& pluginManager = PluginManager::getInstance();
    require(pluginManager.initialize(), "PluginManager failed to initialize");

    TrackManager trackManager;
    auto& unitManager = trackManager.getUnitManager();

    auto rumble = pluginManager.createInstanceById("com.Aestrastudios.rumble");
    require(rumble != nullptr, "Failed to create Rumble instance");
    require(rumble->initialize(48000.0, 512), "Failed to initialize Rumble instance");

    UnitID unitId = unitManager.createUnit("Aestra Rumble", UnitGroup::Synth);
    unitManager.attachPlugin(unitId, "com.Aestrastudios.rumble", rumble);
    unitManager.setUnitEnabled(unitId, true);
    unitManager.setUnitMixerChannel(unitId, -1);

    auto snapshot = unitManager.getAudioSnapshot();
    require(snapshot != nullptr, "Audio snapshot is null");
    require(snapshot->units.size() == 1, "Audio snapshot unit count mismatch");
    require(snapshot->units[0].id == static_cast<int>(unitId), "Audio snapshot unit id mismatch");
    require(snapshot->units[0].enabled == true, "Audio snapshot enabled flag mismatch");
    require(snapshot->units[0].plugin != nullptr, "Audio snapshot plugin missing");
    require(snapshot->units[0].routeId == -1, "Audio snapshot route mismatch");

    std::cout << "[PASS] ArsenalInstrumentAttachmentTest\n";
    return 0;
}
