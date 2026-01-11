// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include "PluginScanner.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Nomad {
namespace Audio {

class IPluginFactory; // [NEW] Forward declaration

/**
 * @brief Central manager for plugin lifecycle and instances
 * 
 * The PluginManager is the main entry point for plugin operations:
 * - Plugin discovery via the PluginScanner
 * - Creating and destroying plugin instances
 * - Tracking active plugin instances
 * - Plugin state management
 * 
 * Thread Safety:
 * - All methods are thread-safe
 * - Plugin instances themselves manage their own thread safety
 * 
 * Usage:
 * @code
 *   auto& manager = PluginManager::getInstance();
 *   
 *   // Scan for plugins
 *   manager.getScanner().addDefaultSearchPaths();
 *   manager.getScanner().scanAsync();
 *   
 *   // Create an instance
 *   if (auto* info = manager.findPlugin("com.vendor.coolreverb")) {
 *       auto instance = manager.createInstance(*info);
 *       instance->initialize(44100.0, 512);
 *       // ... use the plugin ...
 *   }
 * @endcode
 */
class PluginManager {
public:
    /**
     * @brief Get the singleton instance
     */
    static PluginManager& getInstance();
    
    /**
     * @brief Initialize the plugin manager
     * 
     * Call once at application startup. Sets up format-specific
     * factories and initializes COM (on Windows for VST3).
     * 
     * @return true on success
     */
    bool initialize();
    
    /**
     * @brief Shutdown the plugin manager
     * 
     * Call at application exit. Destroys all active instances
     * and cleans up format-specific resources.
     */
    void shutdown();
    
    /**
     * @brief Check if manager is initialized
     */
    bool isInitialized() const { return m_initialized; }
    
    // ==============================
    // Plugin Scanner
    // ==============================
    
    /**
     * @brief Get the plugin scanner
     */
    PluginScanner& getScanner() { return m_scanner; }
    const PluginScanner& getScanner() const { return m_scanner; }
    
    // ==============================
    // Instance Management
    // ==============================
    
    /**
     * @brief Create a new plugin instance
     * 
     * @param info Plugin info from scanner
     * @return Shared pointer to plugin instance, or nullptr on failure
     */
    PluginInstancePtr createInstance(const PluginInfo& info);
    
    /**
     * @brief Create instance by plugin ID
     * 
     * @param pluginId Unique plugin ID
     * @return Shared pointer to plugin instance, or nullptr if not found
     */
    PluginInstancePtr createInstanceById(const std::string& pluginId);
    
    /**
     * @brief Destroy a plugin instance
     * 
     * This is typically not needed as instances are shared_ptr,
     * but can be used for explicit cleanup.
     * 
     * @param instance Instance to destroy
     */
    void destroyInstance(PluginInstancePtr instance);
    
    /**
     * @brief Get all active plugin instances
     */
    std::vector<PluginInstancePtr> getActiveInstances() const;
    
    /**
     * @brief Get count of active instances
     */
    size_t getActiveInstanceCount() const;
    
    // ==============================
    // Plugin Lookup
    // ==============================
    
    /**
     * @brief Find plugin by ID
     * @return Pointer to plugin info, or nullptr if not found
     */
    const PluginInfo* findPlugin(const std::string& id) const;
    
    /**
     * @brief Get plugins by type
     */
    std::vector<PluginInfo> getPluginsByType(PluginType type) const;
    
    /**
     * @brief Get plugins by format
     */
    std::vector<PluginInfo> getPluginsByFormat(PluginFormat format) const;
    
    /**
     * @brief Get all effects plugins
     */
    std::vector<PluginInfo> getEffectPlugins() const {
        return getPluginsByType(PluginType::Effect);
    }
    
    /**
     * @brief Get all instrument plugins
     */
    std::vector<PluginInfo> getInstrumentPlugins() const {
        return getPluginsByType(PluginType::Instrument);
    }
    
    // ==============================
    // Configuration
    // ==============================
    
    /**
     * @brief Set default sample rate for new instances
     */
    void setDefaultSampleRate(double sampleRate) { m_defaultSampleRate = sampleRate; }
    double getDefaultSampleRate() const { return m_defaultSampleRate; }
    
    /**
     * @brief Set default block size for new instances
     */
    void setDefaultBlockSize(uint32_t blockSize) { m_defaultBlockSize = blockSize; }
    uint32_t getDefaultBlockSize() const { return m_defaultBlockSize; }
    
    // ==============================
    // Factory Management
    // ==============================

    /**
     * @brief Create a new plugin instance asynchronously (or pseudo-async)
     * 
     * @param info Plugin info from scanner
     * @param callback Callback to receive the instance
     */
    void createInstanceAsync(const PluginInfo& info, std::function<void(PluginInstancePtr)> callback);

    /**
     * @brief Create instance by plugin ID asynchronously
     */
    void createInstanceByIdAsync(const std::string& pluginId, std::function<void(PluginInstancePtr)> callback);

private:
    PluginManager();
    ~PluginManager();
    
    // Non-copyable
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    
    mutable std::mutex m_mutex;
    bool m_initialized = false;
    
    PluginScanner m_scanner;
    std::vector<std::weak_ptr<IPluginInstance>> m_activeInstances;
    std::unique_ptr<IPluginFactory> m_factory; // [NEW] Abstract factory

    double m_defaultSampleRate = 44100.0;
    uint32_t m_defaultBlockSize = 512;
    
    // Cleanup expired weak_ptrs
    void cleanupExpiredInstances();
};

} // namespace Audio
} // namespace Nomad
