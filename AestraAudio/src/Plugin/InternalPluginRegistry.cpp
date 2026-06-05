// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/InternalPluginRegistry.h"

namespace Aestra {
namespace Audio {

InternalPluginRegistry& InternalPluginRegistry::instance() {
    static InternalPluginRegistry registry;
    return registry;
}

void InternalPluginRegistry::registerPlugin(Registration registration) {
    if (!registration.info.isValid() || !registration.createInstance) {
        return;
    }

    if (!registration.isAvailable) {
        registration.isAvailable = [] { return true; };
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_registrations[registration.info.id] = std::move(registration);
}

std::vector<PluginInfo> InternalPluginRegistry::listAvailablePlugins() const {
    std::vector<PluginInfo> plugins;

    std::lock_guard<std::mutex> lock(m_mutex);
    plugins.reserve(m_registrations.size());
    for (const auto& entry : m_registrations) {
        const auto& registration = entry.second;
        if (registration.isAvailable && !registration.isAvailable()) {
            continue;
        }
        plugins.push_back(registration.info);
    }

    return plugins;
}

bool InternalPluginRegistry::isRegisteredPlugin(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_registrations.find(pluginId) != m_registrations.end();
}

bool InternalPluginRegistry::isPluginAvailable(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_registrations.find(pluginId);
    if (it == m_registrations.end()) {
        return false;
    }
    return !it->second.isAvailable || it->second.isAvailable();
}

PluginInstancePtr InternalPluginRegistry::createInstance(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_registrations.find(pluginId);
    if (it == m_registrations.end()) {
        return nullptr;
    }

    const auto& registration = it->second;
    if (registration.isAvailable && !registration.isAvailable()) {
        return nullptr;
    }

    return registration.createInstance ? registration.createInstance() : nullptr;
}

} // namespace Audio
} // namespace Aestra
