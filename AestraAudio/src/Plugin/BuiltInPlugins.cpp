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
        p.hasEditor = true;
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

const PluginInfo& eqInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.eq";
        p.name = "Aestra EQ";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Equalizer";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = false;
        return p;
    }();
    return info;
}

const PluginInfo& compInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.comp";
        p.name = "Aestra Comp";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Dynamics";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = false;
        return p;
    }();
    return info;
}

const PluginInfo& verbInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.verb";
        p.name = "Aestra Verb";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Reverb";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = false;
        return p;
    }();
    return info;
}

const PluginInfo& delayInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.delay";
        p.name = "Aestra Delay";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Delay";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
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
    plugins.push_back(eqInfo());
    plugins.push_back(compInfo());
    plugins.push_back(verbInfo());
    plugins.push_back(delayInfo());
    return plugins;
}

} // namespace BuiltInPlugins
} // namespace Audio
} // namespace Aestra
