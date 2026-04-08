// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginFactory.h"

#include "AestraLog.h"

// Re-using the same host includes as PluginManager
#ifdef AESTRA_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#ifdef AESTRA_HAS_CLAP
#include "Plugin/CLAPHost.h"
#endif

#ifdef AESTRA_HAS_PLUGINS
#include "Plugin/SamplerPlugin.h"
#include "Plugin/AestraEQ.h"
#include "Plugin/AestraComp.h"
#include "Plugin/AestraVerb.h"
#include "Plugin/AestraDelay.h"
#include "RumbleInstance.h"
#endif

namespace Aestra {
namespace Audio {

namespace {
void applyInternalPluginDefaults(const PluginInstancePtr& instance) {
    if (!instance) {
        return;
    }

    for (const auto& param : instance->getParameters()) {
        instance->setParameter(param.id, param.defaultValue);
    }
}
} // namespace

void InProcessPluginFactory::createPluginAsync(const PluginInfo& info,
                                               std::function<void(PluginInstancePtr)> callback) {
    // Current In-Process implementation acts synchronously "simulating" async completion
    // This allows the API to be async-ready without forcing a thread context switch
    // that might be unsafe for VST3 initialization (which often requires Main Thread).

    PluginInstancePtr instance = nullptr;

    try {
        switch (info.format) {
        case PluginFormat::VST3:
            instance = createVST3Instance(info);
            break;
        case PluginFormat::CLAP:
            instance = createCLAPInstance(info);
            break;
        case PluginFormat::Internal:
            instance = createInternalInstance(info);
            break;
        default:
            Log::error("Unknown plugin format for: " + info.name);
            break;
        }
    } catch (const std::exception& e) {
        Log::error("Exception during plugin creation: " + std::string(e.what()));
    } catch (...) {
        Log::error("Unknown exception during plugin creation");
    }

    if (callback) {
        callback(instance);
    }
}

PluginInstancePtr InProcessPluginFactory::createVST3Instance(const PluginInfo& info) {
#ifdef AESTRA_HAS_VST3
    // Assuming VST3PluginFactory is available via VST3Host.h
    auto instance = VST3PluginFactory::createInstance(info);
    if (instance) {
        return instance;
    }
#endif
    (void)info; // suppress unused warning ifdef
    return nullptr;
}

PluginInstancePtr InProcessPluginFactory::createCLAPInstance(const PluginInfo& info) {
#ifdef AESTRA_HAS_CLAP
    // Assuming CLAPPluginFactory is available via CLAPHost.h
    auto instance = CLAPPluginFactory::createInstance(info);
    if (instance) {
        return instance;
    }
#endif
    (void)info;
    return nullptr;
}

PluginInstancePtr InProcessPluginFactory::createInternalInstance(const PluginInfo& info) {
#ifdef AESTRA_HAS_PLUGINS
    // Aestra Rumble 808 Bass Synthesizer
    if (info.id == "com.Aestrastudios.rumble") {
        auto rumble = std::make_shared<Aestra::Plugins::RumbleInstance>();
        applyInternalPluginDefaults(rumble);
        return rumble;
    }

    // Aestra Sampler
    if (info.id == "com.Aestrastudios.sampler") {
        auto sampler = std::make_shared<Aestra::Audio::Plugins::SamplerPlugin>();
        applyInternalPluginDefaults(sampler);
        return sampler;
    }

    // Aestra EQ — 8-Band Parametric Equalizer
    if (info.id == "com.Aestrastudios.eq") {
        auto eq = std::make_shared<Aestra::Audio::Plugins::AestraEQ>();
        eq->setInfo(info);
        applyInternalPluginDefaults(eq);
        return eq;
    }

    // Aestra Comp — Dynamics Compressor
    if (info.id == "com.Aestrastudios.comp") {
        auto comp = std::make_shared<Aestra::Audio::Plugins::AestraComp>();
        comp->setInfo(info);
        applyInternalPluginDefaults(comp);
        return comp;
    }

    // Aestra Verb — Algorithmic Reverb
    if (info.id == "com.Aestrastudios.verb") {
        auto verb = std::make_shared<Aestra::Audio::Plugins::AestraVerb>();
        verb->setInfo(info);
        applyInternalPluginDefaults(verb);
        return verb;
    }

    // Aestra Delay — Stereo Delay with Modulation
    if (info.id == "com.Aestrastudios.delay") {
        auto delay = std::make_shared<Aestra::Audio::Plugins::AestraDelay>();
        delay->setInfo(info);
        applyInternalPluginDefaults(delay);
        return delay;
    }
#endif
    (void)info;
    return nullptr;
}

} // namespace Audio
} // namespace Aestra
