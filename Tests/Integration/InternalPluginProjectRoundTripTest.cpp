// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "../../Source/Core/ProjectSerializer.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
std::filesystem::path makeTempDir() {
    auto base = std::filesystem::temp_directory_path() / "Aestra_tests";
    std::filesystem::create_directories(base);

    for (int i = 0; i < 1000; ++i) {
        auto candidate = base / ("InternalPluginProjectRoundTrip_" + std::to_string(i));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }

    auto fallback = base / "InternalPluginProjectRoundTrip_fallback";
    std::filesystem::create_directories(fallback);
    return fallback;
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

bool nearlyEqual(float a, float b, float epsilon = 1.0e-6f) {
    return std::fabs(a - b) <= epsilon;
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    const auto tempDir = makeTempDir();
    const auto projectPath = tempDir / "internal-plugin-project.aes";

    auto& pluginManager = PluginManager::getInstance();
    require(pluginManager.initialize(), "PluginManager failed to initialize");

    auto tm1 = std::make_shared<TrackManager>();
    auto& unitManager1 = tm1->getUnitManager();

    auto rumble = pluginManager.createInstanceById("com.Aestrastudios.rumble");
    require(rumble != nullptr, "Failed to create Rumble instance");
    require(rumble->initialize(48000.0, 512), "Failed to initialize Rumble instance");

    rumble->setParameter(0, 0.62f);
    rumble->setParameter(1, 0.27f);
    rumble->setParameter(2, 0.41f);
    rumble->setParameter(3, 0.53f);

    UnitID unitId = unitManager1.createUnit("Aestra Rumble", UnitGroup::Synth);
    unitManager1.setUnitEnabled(unitId, true);
    unitManager1.setUnitColor(unitId, 0xAA5500u);
    unitManager1.setUnitMixerChannel(unitId, 3);
    unitManager1.attachPlugin(unitId, "com.Aestrastudios.rumble", rumble);
    unitManager1.captureUnitPluginState(unitId);

    require(ProjectSerializer::save(projectPath.string(), tm1, 126.0, 2.5), "ProjectSerializer::save failed");

    auto tm2 = std::make_shared<TrackManager>();
    ProjectSerializer::LoadResult loadResult = ProjectSerializer::load(projectPath.string(), tm2);
    require(loadResult.ok, "ProjectSerializer::load failed");

    auto& unitManager2 = tm2->getUnitManager();
    require(unitManager2.getUnitCount() == 1, "Unit count mismatch after load");

    auto unitIds = unitManager2.getAllUnitIDs();
    require(unitIds.size() == 1, "Loaded unit ID list mismatch");

    const UnitInfo* loadedUnit = unitManager2.getUnit(unitIds[0]);
    require(loadedUnit != nullptr, "Loaded unit missing");
    require(loadedUnit->name == "Aestra Rumble", "Loaded unit name mismatch");
    require(loadedUnit->targetMixerRoute == 3, "Loaded unit route mismatch");
    require(loadedUnit->color == 0xAA5500u, "Loaded unit color mismatch");
    require(loadedUnit->enabled == true, "Loaded unit enabled flag mismatch");
    require(loadedUnit->pluginId == "com.Aestrastudios.rumble", "Loaded plugin ID mismatch");
    require(loadedUnit->plugin != nullptr, "Loaded plugin instance missing");
    require(!loadedUnit->pluginState.empty(), "Loaded plugin state missing");

    require(nearlyEqual(loadedUnit->plugin->getParameter(0), 0.62f), "Loaded decay parameter mismatch");
    require(nearlyEqual(loadedUnit->plugin->getParameter(1), 0.27f), "Loaded drive parameter mismatch");
    require(nearlyEqual(loadedUnit->plugin->getParameter(2), 0.41f), "Loaded tone parameter mismatch");
    require(nearlyEqual(loadedUnit->plugin->getParameter(3), 0.53f), "Loaded output gain parameter mismatch");

    std::cout << "[PASS] InternalPluginProjectRoundTripTest\n";
    return 0;
}
