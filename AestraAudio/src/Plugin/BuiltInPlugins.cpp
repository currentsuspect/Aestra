// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/BuiltInPlugins.h"

#include "Plugin/InternalPluginRegistry.h"
#include "Plugin/SamplerPlugin.h"
#include "Plugin/AestraEQ.h"
#include "Plugin/AestraComp.h"
#include "Plugin/AestraVerb.h"
#include "Plugin/AestraDelay.h"
#include "Plugin/AestraDrift.h"
#include "Plugin/AestraLimit.h"
#include "Plugin/AestraSat.h"
#include "Plugin/AestraFilter.h"
#include "Plugin/AestraLFO.h"
#include "Plugin/AestraOTT.h"
#include "Plugin/AestraTransient.h"

#include <mutex>

namespace Aestra {
namespace Audio {
namespace BuiltInPlugins {

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
        p.version = "1.0.0";
        p.category = "Equalizer";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& compInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.comp";
        p.name = "Aestra Compressor";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Dynamics";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 4;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
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
        p.hasEditor = true;
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
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& driftInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.drift";
        p.name = "Aestra Drift";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Pitch Shifter";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& limiterInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.limiter";
        p.name = "Aestra Limit";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Dynamics";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& satInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.sat";
        p.name = "Aestra Sat";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Distortion";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& filterInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.filter";
        p.name = "Aestra Filter";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Filter";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& ottInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.ott";
        p.name = "Aestra OTT";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Dynamics";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& lfoInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.lfo";
        p.name = "Aestra LFO";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Modulation";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

const PluginInfo& transientInfo() {
    static const PluginInfo info = [] {
        PluginInfo p;
        p.id = "com.Aestrastudios.transient";
        p.name = "Aestra Transient";
        p.vendor = "Aestra Studios";
        p.version = "0.1.0";
        p.category = "Dynamics";
        p.format = PluginFormat::Internal;
        p.type = PluginType::Effect;
        p.numAudioInputs = 2;
        p.numAudioOutputs = 2;
        p.hasMidiInput = false;
        p.hasMidiOutput = false;
        p.hasEditor = true;
        return p;
    }();
    return info;
}

namespace {
void applyInfo(const PluginInfo& info, const PluginInstancePtr& instance) {
    if (!instance) {
        return;
    }

    for (const auto& param : instance->getParameters()) {
        instance->setParameter(param.id, param.defaultValue);
    }

    if (auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(instance)) {
        eq->setInfo(info);
    } else if (auto comp = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraComp>(instance)) {
        comp->setInfo(info);
    } else if (auto verb = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraVerb>(instance)) {
        verb->setInfo(info);
    } else if (auto delay = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraDelay>(instance)) {
        delay->setInfo(info);
    } else if (auto drift = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraDrift>(instance)) {
        drift->setInfo(info);
    } else if (auto limit = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraLimit>(instance)) {
        limit->setInfo(info);
    } else if (auto sat = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraSat>(instance)) {
        sat->setInfo(info);
    } else if (auto filter = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraFilter>(instance)) {
        filter->setInfo(info);
    } else if (auto ott = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraOTT>(instance)) {
        ott->setInfo(info);
    } else if (auto lfo = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraLFO>(instance)) {
        lfo->setInfo(info);
    } else if (auto transient = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraTransient>(instance)) {
        transient->setInfo(info);
    }
}

template <typename PluginT>
InternalPluginRegistry::Registration makeRegistration(const PluginInfo& info) {
    return InternalPluginRegistry::Registration{
        info,
        [info]() {
            auto instance = std::make_shared<PluginT>();
            applyInfo(info, instance);
            return instance;
        },
        [] { return true; },
    };
}
} // namespace

void registerCoreBuiltIns() {
    static std::once_flag once;
    std::call_once(once, [] {
        auto& registry = InternalPluginRegistry::instance();
        registry.registerPlugin(makeRegistration<Plugins::SamplerPlugin>(samplerInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraEQ>(eqInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraComp>(compInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraVerb>(verbInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraDelay>(delayInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraDrift>(driftInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraLimit>(limiterInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraSat>(satInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraFilter>(filterInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraOTT>(ottInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraLFO>(lfoInfo()));
        registry.registerPlugin(makeRegistration<Plugins::AestraTransient>(transientInfo()));
    });
}

std::vector<PluginInfo> all() {
    registerCoreBuiltIns();
    return InternalPluginRegistry::instance().listAvailablePlugins();
}

} // namespace BuiltInPlugins
} // namespace Audio
} // namespace Aestra
