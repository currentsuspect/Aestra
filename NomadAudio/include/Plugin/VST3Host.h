// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations from VST3 SDK
namespace Steinberg {
    class IPluginFactory;
    namespace Vst {
        class IComponent;
        class IAudioProcessor;
        class IEditController;
    }
}

namespace Nomad {
namespace Audio {

/**
 * @brief VST3 plugin instance implementation
 * 
 * Wraps a VST3 plugin module and provides the IPluginInstance interface.
 * Handles:
 * - Module loading (.vst3 bundles)
 * - COM/IPluginFactory management
 * - IComponent and IAudioProcessor lifecycle
 * - IEditController for parameters and GUI
 * - ProcessData setup for audio callbacks
 */
class VST3PluginInstance : public IPluginInstance {
public:
    VST3PluginInstance();
    ~VST3PluginInstance() override;
    
    // Non-copyable
    VST3PluginInstance(const VST3PluginInstance&) = delete;
    VST3PluginInstance& operator=(const VST3PluginInstance&) = delete;

    friend class VST3HostTestAccess;
    
    /**
     * @brief Load a VST3 plugin from file
     * @param path Path to .vst3 bundle
     * @param classIndex Index of plugin class if module has multiple (usually 0)
     * @return true if loaded successfully
     */
    bool load(const std::filesystem::path& path, int classIndex = 0);
    
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

    // Watchdog implementation
    WatchdogStats getWatchdogStats() const override { return m_watchdogStats; }
    void resetWatchdog() override;
    bool isBypassedByWatchdog() const override { return m_watchdogStats.isBypassed; }
    bool isCrashed() const override { return m_crashed; }

private:
    bool m_loaded = false;
    bool m_active = false;
    bool m_editorOpen = false;
    bool m_crashed = false;
    
    // Watchdog state
    WatchdogStats m_watchdogStats;
    static constexpr uint64_t WATCHDOG_VIOLATION_LIMIT = 50; // Bypass after 50 violations
    
    PluginInfo m_info;
    
    double m_sampleRate = 44100.0;
    uint32_t m_maxBlockSize = 512;
    
    // VST3 SDK objects (using void* to avoid header pollution, cast in .cpp)
    void* m_module = nullptr;           // VST3::Hosting::Module
    void* m_factory = nullptr;          // IPluginFactory*
    void* m_component = nullptr;        // IComponent*
    void* m_processor = nullptr;        // IAudioProcessor*
    void* m_controller = nullptr;       // IEditController*
    void* m_plugView = nullptr;         // IPlugView* (editor)
    
    // Audio buffers for VST3 ProcessData
    std::vector<float*> m_inputBuffers;
    std::vector<float*> m_outputBuffers;
    std::vector<float> m_tempBuffer;
    
    // Parameter cache
    mutable std::vector<PluginParameter> m_parameterCache;
    mutable bool m_parameterCacheValid = false;
    
    // Internal helpers
    void buildParameterCache() const;
    void setupProcessing();
};

/**
 * @brief Factory for creating VST3 plugin instances
 * 
 * Handles VST3-specific plugin discovery and instantiation.
 */
class VST3PluginFactory {
public:
    /**
     * @brief Scan a VST3 plugin file and extract info
     * @param path Path to .vst3 bundle
     * @return Vector of PluginInfo (VST3 modules can contain multiple plugins)
     */
    static std::vector<PluginInfo> scanPlugin(const std::filesystem::path& path);
    
    /**
     * @brief Create a VST3 plugin instance
     * @param info PluginInfo from scan
     * @return Shared pointer to plugin instance, or nullptr on failure
     */
    static std::shared_ptr<VST3PluginInstance> createInstance(const PluginInfo& info);
};

// Expose for testing
class VST3HostTestAccess {
public:
    static void setProcessor(VST3PluginInstance& instance, void* processor) {
        instance.m_processor = processor;
        instance.m_active = true; // Force active
        instance.m_sampleRate = 48000.0;
    }
};

} // namespace Audio
} // namespace Nomad
