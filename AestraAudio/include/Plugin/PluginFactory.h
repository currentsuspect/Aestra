// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h" // For PluginInstancePtr and PluginInfo
#include "InternalPluginRegistry.h"

#include <functional>
#include <memory>
#include <string>

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
    virtual void createPluginAsync(const PluginInfo& info, std::function<void(PluginInstancePtr)> callback) = 0;
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
    void createPluginAsync(const PluginInfo& info, std::function<void(PluginInstancePtr)> callback) override;

private:
    PluginInstancePtr createVST3Instance(const PluginInfo& info);
    PluginInstancePtr createCLAPInstance(const PluginInfo& info);
    PluginInstancePtr createInternalInstance(const PluginInfo& info);
};

/**
 * @brief Factory for loading native third-party plugins behind a helper process.
 *
 * This factory returns a parent-side proxy. The helper process owns the risky
 * native plugin module; if it crashes, the proxy is marked crashed and future
 * audio callbacks bypass without blocking the realtime thread.
 */
class OutOfProcessPluginFactory : public IPluginFactory {
public:
    explicit OutOfProcessPluginFactory(std::string hostExecutablePath = {});

    void createPluginAsync(const PluginInfo& info, std::function<void(PluginInstancePtr)> callback) override;

    const std::string& getHostExecutablePath() const { return m_hostExecutablePath; }

private:
    std::string m_hostExecutablePath;
};

class HybridPluginFactory : public IPluginFactory {
public:
    explicit HybridPluginFactory(std::string hostExecutablePath = {});

    void createPluginAsync(const PluginInfo& info, std::function<void(PluginInstancePtr)> callback) override;

private:
    InProcessPluginFactory m_inProcess;
    OutOfProcessPluginFactory m_outOfProcess;
};

} // namespace Audio
} // namespace Aestra
