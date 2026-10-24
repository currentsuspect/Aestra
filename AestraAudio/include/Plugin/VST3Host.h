// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <atomic>
#include <filesystem>
#include <functional>
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
} // namespace Vst
} // namespace Steinberg

namespace Aestra {
namespace Audio {

class EditorLoopPump;
struct Vst3ParamState; // pending parameter values and the controller's host (Vst3ParamBridge.h)

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

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) override;

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

    /**
     * @brief Linux (V82-H3): open the editor in an X11 window the caller creates.
     *
     * The plugin's view is created first so its preferred size is known; @p makeWindow is then called with that size and
     * whether the plugin can be resized, and returns the X11 window id (0 = could not) the view is attached to
     * ("X11EmbedWindowID"). The plugin drives itself through Linux::IRunLoop, which feeds @p pump: the caller's GUI
     * thread polls the descriptors and timers the pump holds. When the plugin asks for another size
     * (IPlugFrame::resizeView) @p requestResize runs, then the view is told the size took. Returns false, with nothing
     * left open, if the plugin has no X11 editor. Call on the thread that owns the window and runs the pump.
     */
    bool openEditorX11(const std::function<unsigned long(int width, int height, bool resizable)>& makeWindow,
                       EditorLoopPump& pump, std::function<void(int width, int height)> requestResize);
    /// Whether the open editor accepts being resized by the host.
    bool canResizeEditor() const;

    /**
     * @brief Hear about knobs moved in the plugin's own window (IComponentHandler::performEdit), normalized 0..1.
     * Called on the thread that runs the editor, so the listener must be quick and thread-safe. The value has already
     * been queued for the processor; this is for telling the rest of the app (automation recording, parameter UI).
     */
    void setEditListener(std::function<void(uint32_t id, double normalized)> listener);
    /**
     * @brief Bring the controller up to date with what the processor reported (its own parameter moves) and act on
     * restartComponent() flags. Call from the UI side (the editor loop), not the audio thread.
     */
    void pumpControllerSync();

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override;
    uint32_t getTailSamples() const override;

    // Watchdog implementation
    WatchdogStats getWatchdogStats() const override;
    void resetWatchdog() override;
    bool isBypassedByWatchdog() const override;
    bool isCrashed() const override;

private:
    struct ModuleHolder;

    bool m_loaded = false;
    bool m_active = false;
    bool m_editorOpen = false;

    // Watchdog state
    static constexpr uint64_t WATCHDOG_VIOLATION_LIMIT = 50; // Bypass after 50 violations
    std::atomic<uint64_t> m_watchdogMaxExecutionTimeNs{0};
    std::atomic<uint64_t> m_watchdogAvgExecutionTimeNs{0};
    std::atomic<uint64_t> m_watchdogViolationCount{0};
    std::atomic<bool> m_watchdogBypassed{false};
    std::atomic<bool> m_crashed{false};

    PluginInfo m_info;

    double m_sampleRate = 44100.0;
    uint32_t m_maxBlockSize = 512;
    /// Audio buses the processor is handed each block: an instrument has no input bus.
    int32_t m_audioInputBuses = 1;
    int32_t m_audioOutputBuses = 1;

    // VST3 SDK objects. ModuleHolder keeps the SDK module type out of this
    // public header while retaining typed RAII ownership in VST3Host.cpp.
    std::unique_ptr<ModuleHolder> m_module;
    void* m_factory = nullptr;         // IPluginFactory*
    void* m_hostApplication = nullptr; // VST3::HostApplication
    void* m_component = nullptr;       // IComponent*
    void* m_processor = nullptr;       // IAudioProcessor*
    void* m_controller = nullptr;      // IEditController*
    void* m_plugView = nullptr;        // IPlugView* (editor)
    void* m_plugFrame = nullptr;       // host frame + run loop handed to the editor (Linux)

    // Audio buffers for VST3 ProcessData
    std::vector<float*> m_inputBuffers;
    std::vector<float*> m_outputBuffers;
    std::vector<float> m_tempBuffer;

    // Parameter cache
    mutable std::vector<PluginParameter> m_parameterCache;
    mutable bool m_parameterCacheValid = false;
    std::unique_ptr<Vst3ParamState> m_params;

    // Internal helpers
    void buildParameterCache() const;
    void setupProcessing();
    void reportDeferredRealtimeEvents() const;

    mutable std::atomic<bool> m_watchdogReportPending{false};
    mutable std::atomic<bool> m_crashReportPending{false};
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
} // namespace Aestra
