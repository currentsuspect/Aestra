// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "RumblePluginRegistration.h"

#include "Plugin/InternalPluginRegistry.h"
#include "Presets/PresetLibrary.h"
#include "RumblePresetBank.h"
#include "RumbleInstance.h"

#include <array>
#include <mutex>
#include <string>

namespace Aestra {
namespace Plugins {

const Aestra::Audio::PluginInfo& rumblePluginInfo() {
    static const Aestra::Audio::PluginInfo info = [] {
        Aestra::Audio::PluginInfo pluginInfo;
        pluginInfo.id = "com.Aestrastudios.rumble";
        pluginInfo.name = "Aestra Rumble";
        pluginInfo.vendor = "Aestra Studios";
        pluginInfo.version = "0.3.0"; // the approved sketch voice (FD-80)
        pluginInfo.category = "Instrument";
        pluginInfo.format = Aestra::Audio::PluginFormat::Internal;
        pluginInfo.type = Aestra::Audio::PluginType::Instrument;
        pluginInfo.numAudioInputs = 0;
        pluginInfo.numAudioOutputs = 2;
        pluginInfo.hasMidiInput = true;
        pluginInfo.hasMidiOutput = false;
        pluginInfo.hasEditor = true;
        return pluginInfo;
    }();
    return info;
}

void registerRumblePlugin() {
    static std::once_flag once;
    std::call_once(once, [] {
        auto createInstance = [] { return std::make_shared<RumbleInstance>(); };
        auto canAccess = [] { return true; }; // Aestra's own plugins are free (FD-57)
        Aestra::Audio::InternalPluginRegistry::instance().registerPlugin({
            rumblePluginInfo(),
            createInstance,
            canAccess,
        });
        registerRumbleFactoryPresets();
    });
}

// The shared preset bar's factory list for Rumble: the seven starting sounds of the approved sketch (FD-80).
// Parameters are matched by display name, like every other plugin's. The click's noise seed is not a parameter,
// so a shared preset plays with the instance's own seed; the click differs only in its random grain.
void registerRumbleFactoryPresets() {
    std::vector<Aestra::Audio::Presets::Preset> presets;
    for (const auto& bank : kRumbleFactoryPresets) {
        Aestra::Audio::Presets::Preset p;
        p.name = std::string(bank.name);
        p.category = std::string(bank.category);
        p.description = std::string(bank.description);
        p.tags = {"808", "bass", "sub"};
        // The state carries the sound's click grain, which is not a parameter, so a factory sound picked from the
        // shared preset bar is the same sound everywhere.
        p.state = RumbleInstance::stateOfSound(rumbleSoundOf(bank));
        for (uint32_t i = 0; i < kRumblePresetParameterCount; ++i)
            p.params.push_back({Aestra::Audio::Presets::PresetParam::kNoId, RumbleInstance::parameterName(i),
                                bank.values[i]});
        presets.push_back(std::move(p));
    }
    Aestra::Audio::Presets::registerFactoryPresets(rumblePluginInfo().id, std::move(presets));
}

namespace {
struct RumblePluginAutoRegistrar {
    RumblePluginAutoRegistrar() { registerRumblePlugin(); }
};

RumblePluginAutoRegistrar kRumblePluginAutoRegistrar;
} // namespace

} // namespace Plugins
} // namespace Aestra
