// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h" // For PluginInstancePtr and PluginInfo
#include <functional>
#include <memory>

namespace Aestra {
namespace Audio {

/**
 * @brief Interface for creating plugin instances.
 * 
 * Abstraction layer to support different loading strategies:
 * - In-Process (Direct DLL loading)
 * - Out-of-Process (IPC-based loading)
 * - Remote/Networked
 */
class IPluginFactory {
public:
    virtual ~IPluginFactory() = default;

    /**
     * @brief Request asynchronous creation of a plugin instance.
     * 
     * @param info The plugin descriptor.
     * @param callback Function called when creation completes (success or failure). 
     *                 Returns nullptr on failure.
     */
    virtual void createPluginAsync(const PluginInfo& info, 
                                   std::function<void(PluginInstancePtr)> callback) = 0;
};

/**
 * @brief Default factory for loading plugins in the current process.
 * 
 * Implements the legacy loading behavior using VST3Host, CLAPHost, etc.
 * Note: While the API is async, the current implementation may execute
 * the callback synchronously on the calling thread if the underlying
 * host libraries do not support async loading.
 */
class InProcessPluginFactory : public IPluginFactory {
public:
    void createPluginAsync(const PluginInfo& info, 
                           std::function<void(PluginInstancePtr)> callback) override;

private:
    PluginInstancePtr createVST3Instance(const PluginInfo& info);
    PluginInstancePtr createCLAPInstance(const PluginInfo& info);
    PluginInstancePtr createInternalInstance(const PluginInfo& info);
};

} // namespace Audio
} // namespace Aestra
