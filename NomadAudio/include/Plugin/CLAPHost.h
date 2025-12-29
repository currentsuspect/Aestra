// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Forward declarations for CLAP types
struct clap_plugin;
struct clap_plugin_entry;
struct clap_host;

namespace Nomad {
namespace Audio {

/**
 * @brief CLAP plugin instance implementation
 * 
 * Wraps a CLAP plugin and provides the IPluginInstance interface.
 * CLAP is a simpler API than VST3 with a C-based interface.
 * 
 * Key differences from VST3:
 * - C-based API (not COM)
 * - Single plugin descriptor per entry
 * - Simpler audio processing setup
 * - Built-in thread-safety annotations
 */
class CLAPPluginInstance : public IPluginInstance {
public:
    CLAPPluginInstance();
    ~CLAPPluginInstance() override;
    
    // Non-copyable
    CLAPPluginInstance(const CLAPPluginInstance&) = delete;
    CLAPPluginInstance& operator=(const CLAPPluginInstance&) = delete;
    
    /**
     * @brief Load a CLAP plugin from file
     * @param path Path to .clap file
     * @param pluginIndex Index of plugin in multi-plugin file (usually 0)
     * @return true if loaded successfully
     */
    bool load(const std::filesystem::path& path, int pluginIndex = 0);
    
    /**
     * @brief Unload the plugin
     */
    void unload();
    
    /**
     * @brief Check if plugin is loaded
     */
    bool isLoaded() const { return m_loaded; }
    
    // ==============================
    // IPluginInstance Implementation
    // ==============================
    
    bool initialize(double sampleRate, uint32_t maxBlockSize) override;
    void shutdown() override;
    void activate() override;
    void deactivate() override;
    bool isActive() const override { return m_active; }
    
    void process(
        const float* const* inputs,
        float** outputs,
        uint32_t numInputChannels,
        uint32_t numOutputChannels,
        uint32_t numFrames,
        const MidiBuffer* midiInput = nullptr,
        MidiBuffer* midiOutput = nullptr
    ) override;
    
    std::vector<PluginParameter> getParameters() const override;
    uint32_t getParameterCount() const override;
    float getParameter(uint32_t id) const override;
    void setParameter(uint32_t id, float value) override;
    std::string getParameterDisplay(uint32_t id) const override;
    
    std::vector<uint8_t> saveState() const override;
    bool loadState(const std::vector<uint8_t>& state) override;
    
    bool hasEditor() const override;
    bool openEditor(void* parentWindow) override;
    void closeEditor() override;
    bool isEditorOpen() const override { return m_editorOpen; }
    std::pair<int, int> getEditorSize() const override;
    bool resizeEditor(int width, int height) override;
    
    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override;
    uint32_t getTailSamples() const override;

    // Watchdog stubs (not yet implemented for CLAP)
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

private:
    bool m_loaded = false;
    bool m_active = false;
    bool m_editorOpen = false;
    
    PluginInfo m_info;
    
    double m_sampleRate = 44100.0;
    uint32_t m_maxBlockSize = 512;
    
    // CLAP types (using void* to avoid header pollution)
    void* m_library = nullptr;          // DLL/dylib handle
    const clap_plugin_entry* m_entry = nullptr;
    const clap_plugin* m_plugin = nullptr;
    clap_host* m_host = nullptr;
    
    // Parameter cache
    mutable std::vector<PluginParameter> m_parameterCache;
    mutable bool m_parameterCacheValid = false;
    
    void buildParameterCache() const;
    static clap_host* createHost();
};

/**
 * @brief Factory for creating CLAP plugin instances
 */
class CLAPPluginFactory {
public:
    /**
     * @brief Scan a CLAP plugin file and extract info
     * @param path Path to .clap file
     * @return Vector of PluginInfo (CLAP files can contain multiple plugins)
     */
    static std::vector<PluginInfo> scanPlugin(const std::filesystem::path& path);
    
    /**
     * @brief Create a CLAP plugin instance
     * @param info PluginInfo from scan
     * @return Shared pointer to plugin instance, or nullptr on failure
     */
    static std::shared_ptr<CLAPPluginInstance> createInstance(const PluginInfo& info);
};

} // namespace Audio
} // namespace Nomad
