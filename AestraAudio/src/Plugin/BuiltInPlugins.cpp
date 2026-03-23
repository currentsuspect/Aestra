// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/BuiltInPlugins.h"

namespace Aestra {
namespace Audio {
namespace BuiltInPlugins {

const PluginInfo& rumbleInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.rumble";
        p.name = "Aestra Rumble";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Instrument";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Instrument;
        p.numAudioInputs = 0;
        p.numAudioOutputs = 2;
        p.hasMidiInput = true;
        p.hasMidiOutput = false;
        p.hasEditor = false;
        return p;
    }();
    return info;
}

const PluginInfo& samplerInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.sampler";
        p.name = "Aestra Sampler";
        p.vendor = "Aestra Studios";
        p.version = "1.0.0";
        p.category = "Instrument";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Instrument;
        p.numAudioInputs = 0;
        p.numAudioOutputs = 2;
        p.hasMidiInput = true;
        p.hasMidiOutput = false;
        p.hasEditor = false;
        return p;
    }();
    return info;
}

std::vector<PluginInfo> all() {
    std::vector<PluginInfo> plugins;
#ifdef AESTRA_HAS_PLUGINS
    plugins.push_back(rumbleInfo());
#endif
    plugins.push_back(samplerInfo());
    return plugins;
}

} // namespace BuiltInPlugins
} // namespace Audio
} // namespace Aestra
