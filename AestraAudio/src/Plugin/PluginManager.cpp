// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginManager.h"

#include "AestraLog.h"
#include "AestraPlatform.h" // For getAppDataPath
#include "PluginFactory.h"  // [NEW]

#include <algorithm>
#include <filesystem>
#include <future> // For sync wrapper

// NOTE: Individual Host headers (VST3Host.h etc) moved to PluginFactory.cpp

#ifdef _WIN32
#include <objbase.h> // For COM initialization (VST3)
#endif

namespace Aestra {
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

    // Initialize default factory (In-Process)
    m_factory = std::make_unique<InProcessPluginFactory>();

    // Load Plugin Cache
    if (auto* utils = Platform::getUtils()) {
        std::string appData = utils->getAppDataPath("AESTRA");
        if (!appData.empty()) {
            std::filesystem::path cachePath = std::filesystem::path(appData) / "plugin_cache.bin";

            // Ensure directory exists
            std::error_code ec;
            std::filesystem::create_directories(cachePath.parent_path(), ec);

            if (m_scanner.loadScanCache(cachePath)) {
                Aestra::Log::info("Plugin cache loaded: " + std::to_string(m_scanner.getScannedPlugins().size()) +
                                  " plugins");
            } else {
                Aestra::Log::warning("Plugin cache not found or invalid. Triggering initial scan...");
                m_scanner.scanAsync();
            }
        } else {
            Aestra::Log::error("AppData path is empty in PluginManager::initialize");
            m_scanner.scanAsync();
        }
    } else {
        Aestra::Log::error("Platform::getUtils() returned null in PluginManager::initialize");
        m_scanner.scanAsync();
    }

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

    // Save Plugin Cache
    if (auto* utils = Platform::getUtils()) {
        std::string appData = utils->getAppDataPath("AESTRA");
        if (!appData.empty()) {
            std::filesystem::path cachePath = std::filesystem::path(appData) / "plugin_cache.bin";

            // Ensure directory exists (just in case)
            std::error_code ec;
            std::filesystem::create_directories(cachePath.parent_path(), ec);

            if (m_scanner.saveScanCache(cachePath)) {
                Aestra::Log::info("Plugin cache saved to: " + cachePath.string());
            } else {
                Aestra::Log::error("Failed to save plugin cache to: " + cachePath.string());
            }
        } else {
            Aestra::Log::error("AppData path is empty in PluginManager::shutdown");
        }
    } else {
        Aestra::Log::error("Platform::getUtils() returned null in PluginManager::shutdown");
    }

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
    if (!m_initialized || !m_factory) {
        return nullptr;
    }

    // Adapt Async Factory to Synchronous API (Blocking)
    // This allows gradual migration while using the new abstraction
    std::promise<PluginInstancePtr> promise;
    auto future = promise.get_future();

    m_factory->createPluginAsync(info, [&](PluginInstancePtr instance) { promise.set_value(instance); });

    // Block until complete (In-Process factory is synchronous anyway, so this is safe)
    // For true async factories, this would hang the UI, which is why we must migrate consumers.
    PluginInstancePtr instance = future.get();

    if (instance) {
        // Track the instance
        std::lock_guard<std::mutex> lock(m_mutex);
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

void PluginManager::createInstanceAsync(const PluginInfo& info, std::function<void(PluginInstancePtr)> callback) {
    if (!m_initialized || !m_factory) {
        if (callback)
            callback(nullptr);
        return;
    }

    m_factory->createPluginAsync(info, [this, callback](PluginInstancePtr instance) {
        if (instance) {
            std::lock_guard<std::mutex> lock(m_mutex);
            cleanupExpiredInstances();
            m_activeInstances.push_back(instance);
        }
        if (callback)
            callback(instance);
    });
}

void PluginManager::createInstanceByIdAsync(const std::string& pluginId,
                                            std::function<void(PluginInstancePtr)> callback) {
    const PluginInfo* info = findPlugin(pluginId);
    if (!info) {
        if (callback)
            callback(nullptr);
        return;
    }
    createInstanceAsync(*info, callback);
}

void PluginManager::destroyInstance(PluginInstancePtr instance) {
    if (!instance)
        return;

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

// ==============================
// Factory Methods (Delegated)
// ==============================

// Helper methods removed, delegated to InProcessPluginFactory

void PluginManager::cleanupExpiredInstances() {
    // Remove expired weak_ptrs
    m_activeInstances.erase(std::remove_if(m_activeInstances.begin(), m_activeInstances.end(),
                                           [](const std::weak_ptr<IPluginInstance>& w) { return w.expired(); }),
                            m_activeInstances.end());
}

} // namespace Audio
} // namespace Aestra
