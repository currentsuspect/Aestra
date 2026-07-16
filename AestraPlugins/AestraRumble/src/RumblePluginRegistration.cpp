// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "RumblePluginRegistration.h"

#include "EntitlementStore.h"
#include "Plugin/InternalPluginRegistry.h"
#include "RumbleInstance.h"

#include <mutex>

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
    });
}

namespace {
struct RumblePluginAutoRegistrar {
    RumblePluginAutoRegistrar() { registerRumblePlugin(); }
};

RumblePluginAutoRegistrar kRumblePluginAutoRegistrar;
} // namespace

} // namespace Plugins
} // namespace Aestra
