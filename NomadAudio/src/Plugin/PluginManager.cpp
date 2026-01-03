// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginManager.h"
#include <algorithm>

#ifdef NOMAD_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#ifdef NOMAD_HAS_CLAP
#include "Plugin/CLAPHost.h"
#endif

#ifdef NOMAD_HAS_PLUGINS
#include <RumbleInstance.h>
#endif

#ifdef _WIN32
#include <objbase.h>  // For COM initialization (VST3)
#endif

namespace Nomad {
namespace Audio {

// Singleton instance
PluginManager& PluginManager::getInstance() {
    static PluginManager instance;
    return instance;
}

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    shutdown();
}

bool PluginManager::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        return true;
    }
    
#ifdef _WIN32
    // Initialize COM for VST3 plugin hosting
    // Use COINIT_APARTMENTTHREADED for UI compatibility
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        // S_FALSE means already initialized, RPC_E_CHANGED_MODE means different mode
        // Both are acceptable
        return false;
    }
#endif
    
    // Add default search paths
    m_scanner.addDefaultSearchPaths();
    
    m_initialized = true;
    return true;
}

void PluginManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        return;
    }
    
    // Cancel any ongoing scan
    m_scanner.cancelScan();
    
    // Clear all active instances
    m_activeInstances.clear();
    
#ifdef _WIN32
    // Uninitialize COM
    CoUninitialize();
#endif
    
    m_initialized = false;
}

// ==============================
// Instance Management
// ==============================

PluginInstancePtr PluginManager::createInstance(const PluginInfo& info) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        return nullptr;
    }
    
    PluginInstancePtr instance;
    
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
    }
    
    if (instance) {
        // Track the instance
        cleanupExpiredInstances();
        m_activeInstances.push_back(instance);
    }
    
    return instance;
}

PluginInstancePtr PluginManager::createInstanceById(const std::string& pluginId) {
    const PluginInfo* info = findPlugin(pluginId);
    if (!info) {
        return nullptr;
    }
    return createInstance(*info);
}

void PluginManager::destroyInstance(PluginInstancePtr instance) {
    if (!instance) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Instance will be cleaned up when all shared_ptrs are released
    // Just mark for cleanup
    cleanupExpiredInstances();
}

std::vector<PluginInstancePtr> PluginManager::getActiveInstances() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<PluginInstancePtr> result;
    for (const auto& weak : m_activeInstances) {
        if (auto ptr = weak.lock()) {
            result.push_back(ptr);
        }
    }
    return result;
}

size_t PluginManager::getActiveInstanceCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t count = 0;
    for (const auto& weak : m_activeInstances) {
        if (!weak.expired()) {
            ++count;
        }
    }
    return count;
}

// ==============================
// Plugin Lookup
// ==============================

const PluginInfo* PluginManager::findPlugin(const std::string& id) const {
    return m_scanner.findPlugin(id);
}

std::vector<PluginInfo> PluginManager::getPluginsByType(PluginType type) const {
    return m_scanner.getPluginsByType(type);
}

std::vector<PluginInfo> PluginManager::getPluginsByFormat(PluginFormat format) const {
    return m_scanner.getPluginsByFormat(format);
}

// ==============================
// Factory Methods (Stubs for now)
// ==============================

PluginInstancePtr PluginManager::createVST3Instance(const PluginInfo& info) {
#ifdef NOMAD_HAS_VST3
    auto instance = VST3PluginFactory::createInstance(info);
    if (instance) {
        return instance;
    }
#endif
    (void)info;
    return nullptr;
}

PluginInstancePtr PluginManager::createCLAPInstance(const PluginInfo& info) {
#ifdef NOMAD_HAS_CLAP
    auto instance = CLAPPluginFactory::createInstance(info);
    if (instance) {
        return instance;
    }
#endif
    (void)info;
    return nullptr;
}

PluginInstancePtr PluginManager::createInternalInstance(const PluginInfo& info) {
    // Create built-in NOMAD plugins
    
#ifdef NOMAD_HAS_PLUGINS
    // Nomad Rumble 808 Bass Synthesizer
    if (info.id == "com.nomadstudios.rumble") {
        return std::make_shared<Nomad::Plugins::RumbleInstance>();
    }
#endif
    
    (void)info;
    return nullptr;
}

void PluginManager::cleanupExpiredInstances() {
    // Remove expired weak_ptrs
    m_activeInstances.erase(
        std::remove_if(m_activeInstances.begin(), m_activeInstances.end(),
            [](const std::weak_ptr<IPluginInstance>& w) { return w.expired(); }),
        m_activeInstances.end()
    );
}

} // namespace Audio
} // namespace Nomad
