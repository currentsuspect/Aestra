// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

class InternalPluginRegistry {
public:
    using CreateInstanceFn = std::function<PluginInstancePtr()>;
    using IsAvailableFn = std::function<bool()>;

    struct Registration {
        PluginInfo info;
        CreateInstanceFn createInstance;
        IsAvailableFn isAvailable;
    };

    static InternalPluginRegistry& instance();

    void registerPlugin(Registration registration);

    std::vector<PluginInfo> listAvailablePlugins() const;
    bool isRegisteredPlugin(const std::string& pluginId) const;
    bool isPluginAvailable(const std::string& pluginId) const;
    PluginInstancePtr createInstance(const std::string& pluginId) const;

private:
    InternalPluginRegistry() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Registration> m_registrations;
};

} // namespace Audio
} // namespace Aestra
