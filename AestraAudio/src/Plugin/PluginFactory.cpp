// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

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
#include <RumbleInstance.h>
#include "Plugin/SamplerPlugin.h"
#endif

namespace Aestra {
namespace Audio {

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
    if (info.id == "com.nomadstudios.rumble") {
        return std::make_shared<Aestra::Plugins::RumbleInstance>();
    }
    
    // Aestra Sampler
    if (info.id == "com.nomadstudios.sampler") {
        return std::make_shared<Aestra::Audio::Plugins::SamplerPlugin>();
    }
#endif
    (void)info;
    return nullptr;
}

} // namespace Audio
} // namespace Aestra
