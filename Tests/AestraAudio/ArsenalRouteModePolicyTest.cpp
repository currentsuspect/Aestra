// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/ArsenalProcessingContext.h"
#include "Models/UnitManager.h"
#include "Plugin/PluginManager.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

void writeString(std::ofstream& file, const std::string& value) {
    const auto len = static_cast<uint32_t>(value.size());
    file.write(reinterpret_cast<const char*>(&len), sizeof(len));
    file.write(value.data(), len);
}

std::filesystem::path writeExternalPluginCache() {
    using namespace Aestra::Audio;

    const auto dir = std::filesystem::temp_directory_path() / "aestra-arsenal-policy-test";
    std::filesystem::create_directories(dir);
    const auto pluginPath = dir / "external-instrument.vst3";
    {
        std::ofstream plugin(pluginPath, std::ios::binary);
        plugin << "synthetic";
    }

    const auto cachePath = dir / "plugin-cache.bin";
    std::ofstream file(cachePath, std::ios::binary);
    const char magic[4] = {'N', 'P', 'S', 'C'};
    file.write(magic, sizeof(magic));
    const uint32_t version = 2;
    const uint32_t count = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    writeString(file, "com.vendor.external-instrument");
    writeString(file, "External Instrument");
    writeString(file, "Vendor");
    writeString(file, "1.0.0");
    writeString(file, "Instrument");
    writeString(file, pluginPath.string());

    const PluginFormat format = PluginFormat::VST3;
    const PluginType type = PluginType::Instrument;
    const uint32_t numAudioInputs = 0;
    const uint32_t numAudioOutputs = 2;
    const bool hasMidiInput = true;
    const bool hasMidiOutput = false;
    const bool hasEditor = false;
    file.write(reinterpret_cast<const char*>(&format), sizeof(format));
    file.write(reinterpret_cast<const char*>(&type), sizeof(type));
    file.write(reinterpret_cast<const char*>(&numAudioInputs), sizeof(numAudioInputs));
    file.write(reinterpret_cast<const char*>(&numAudioOutputs), sizeof(numAudioOutputs));
    file.write(reinterpret_cast<const char*>(&hasMidiInput), sizeof(hasMidiInput));
    file.write(reinterpret_cast<const char*>(&hasMidiOutput), sizeof(hasMidiOutput));
    file.write(reinterpret_cast<const char*>(&hasEditor), sizeof(hasEditor));

    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(pluginPath, ec);
    const uint64_t mtimeBits = ec ? 0 : static_cast<uint64_t>(mtime.time_since_epoch().count());
    file.write(reinterpret_cast<const char*>(&mtimeBits), sizeof(mtimeBits));
    file.close();
    return cachePath;
}

void verifyExternalPluginJsonPreservesMetadataWithoutInstance() {
    using namespace Aestra::Audio;

    const auto cachePath = writeExternalPluginCache();
    require(PluginManager::getInstance().getScanner().loadScanCache(cachePath),
            "Failed to load synthetic external plugin cache");

    UnitManager manager;
    Aestra::JSON root = Aestra::JSON::object();
    root.set("nextId", Aestra::JSON(2.0));
    Aestra::JSON units = Aestra::JSON::array();
    Aestra::JSON unit = Aestra::JSON::object();
    unit.set("id", Aestra::JSON(1.0));
    unit.set("name", Aestra::JSON("External"));
    unit.set("enabled", Aestra::JSON(true));
    unit.set("targetMixerRoute", Aestra::JSON(-1.0));
    unit.set("pluginId", Aestra::JSON("com.vendor.external-instrument"));
    unit.set("pluginStateHex", Aestra::JSON("0102FE"));
    units.push(unit);
    root.set("units", units);

    manager.loadFromJSON(root);

    const UnitInfo* loaded = manager.getUnit(1);
    require(loaded != nullptr, "External plugin unit failed to load");
    require(loaded->pluginId == "com.vendor.external-instrument", "External plugin ID should be preserved");
    require(loaded->pluginState == std::vector<uint8_t>({0x01, 0x02, 0xFE}),
            "External plugin state should be preserved");
    require(!loaded->plugin, "External plugin must not auto-instantiate from project JSON");
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
    unit.set("targetMixerRoute", Aestra::JSON(4.0)); // legacy timeline ownership metadata
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

    verifyExternalPluginJsonPreservesMetadataWithoutInstance();

    std::cout << "[PASS] ArsenalRouteModePolicyTest\n";
    return 0;
}
