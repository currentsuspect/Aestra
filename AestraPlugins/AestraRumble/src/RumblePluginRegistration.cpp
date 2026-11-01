// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "RumblePluginRegistration.h"

#include "EntitlementStore.h"
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
        pluginInfo.version = "0.2.0";
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
#if defined(AESTRA_ENABLE_TEST_LICENSES) && AESTRA_ENABLE_TEST_LICENSES
        auto createInstance = [] { return std::make_shared<RumbleInstance>(RumbleInstance::TestLicense::GrantRumble); };
        auto canAccess = [] { return true; };
#else
        auto createInstance = [] { return std::make_shared<RumbleInstance>(); };
        auto canAccess = [] {
            return Aestra::License::EntitlementStore().canAccess(Aestra::License::ProductFeature::Rumble);
        };
#endif
        Aestra::Audio::InternalPluginRegistry::instance().registerPlugin({
            rumblePluginInfo(),
            createInstance,
            canAccess,
        });
        registerRumbleFactoryPresets();
    });
}

// The shared preset bar's factory list for Rumble (FD-72: 8 to 10 per plugin): ten of the editor's sixteen, covering
// all four of its categories. The editor keeps its full bank. Values are in parameter-id order, named here so the
// shared presets match by name like every other plugin's.
void registerRumbleFactoryPresets() {
    static constexpr std::array<const char*, kRumblePresetParameterCount> kNames = {
        "AmpDecay",      "Drive",       "Tone",      "OutputGain",      "PitchAmount",    "PitchDecay", "PitchCurve",
        "AmpAttack",     "Resonance",   "TransientAmount", "ClickLevel", "ClickDecay",    "ClickTone",  "GlideTime",
        "GlideCurve",    "GlideMode",   "RetriggerMode",   "FilterEnvAmount", "FilterKeytrack", "SatMode", "VelocityToAmp",
        "Tune",          "Fine",        "Harmonics", "SubClean"};
    static constexpr const char* kChosen[] = {"Pure Sub",  "Balanced 808", "Tight Punch", "Long Tail",  "Phone Ready",
                                              "Dark Trap", "Glide Lead",   "Tape Weight", "Hard Clip", "Kick Layer"};
    std::vector<Aestra::Audio::Presets::Preset> presets;
    for (const char* wanted : kChosen) {
        for (const auto& bank : kRumbleFactoryPresets) {
            if (bank.name != wanted) continue;
            Aestra::Audio::Presets::Preset p;
            p.name = std::string(bank.name);
            p.category = std::string(bank.category);
            p.description = std::string(bank.description);
            p.tags = {"808", "bass", "sub"};
            for (size_t i = 0; i < kNames.size(); ++i)
                p.params.push_back({Aestra::Audio::Presets::PresetParam::kNoId, kNames[i], bank.values[i]});
            presets.push_back(std::move(p));
        }
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
