// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <cstring>

namespace Nomad {
namespace Audio {

// Forward declarations
class MidiBuffer;

/**
 * @brief Plugin format type
 */
enum class PluginFormat : uint8_t {
    VST3,       ///< Steinberg VST3 format
    CLAP,       ///< CLAP (CLever Audio Plugin) format
    Internal    ///< Built-in NOMAD plugins
};

/**
 * @brief Plugin type/category
 */
enum class PluginType : uint8_t {
    Effect,      ///< Audio effect (EQ, compressor, reverb, etc.)
    Instrument,  ///< Virtual instrument (synth, sampler)
    MidiEffect,  ///< MIDI processor (arpeggiator, chord generator)
    Analyzer     ///< Analysis plugin (spectrum, loudness meter)
};

/**
 * @brief Plugin metadata information
 */
struct PluginInfo {
    std::string id;             ///< Unique identifier (VST3 UID / CLAP ID)
    std::string name;           ///< Display name
    std::string vendor;         ///< Vendor/manufacturer name
    std::string version;        ///< Version string
    std::string category;       ///< Category string (e.g., "Dynamics", "Reverb")
    PluginFormat format;        ///< Plugin format
    PluginType type;            ///< Plugin type
    std::filesystem::path path; ///< Path to plugin file
    
    uint32_t numAudioInputs = 2;   ///< Number of audio input channels
    uint32_t numAudioOutputs = 2;  ///< Number of audio output channels
    bool hasMidiInput = false;     ///< Accepts MIDI input
    bool hasMidiOutput = false;    ///< Produces MIDI output
    bool hasEditor = false;        ///< Has graphical editor
    
    bool isValid() const { return !id.empty() && !name.empty(); }
};

/**
 * @brief Plugin parameter descriptor
 */
struct PluginParameter {
    uint32_t id = 0;                ///< Unique parameter ID
    std::string name;               ///< Display name
    std::string shortName;          ///< Short name for compact displays
    std::string unit;               ///< Unit string (%, dB, Hz, etc.)
    
    float defaultValue = 0.0f;      ///< Default value (normalized 0-1)
    float minValue = 0.0f;          ///< Minimum value
    float maxValue = 1.0f;          ///< Maximum value
    
    bool isAutomatable = true;      ///< Can be automated
    bool isBypass = false;          ///< Is the bypass parameter
    bool isReadOnly = false;        ///< Read-only (output only)
    
    uint32_t stepCount = 0;         ///< 0 = continuous, >0 = stepped
};

/**
 * @brief Abstract plugin instance interface
 * 
 * This interface wraps format-specific plugin implementations (VST3, CLAP)
 * providing a unified API for the audio engine to interact with plugins.
 * 
 * Thread Safety:
 * - initialize(), shutdown(), activate(), deactivate() must be called from main thread
 * - process() is called from audio thread (RT-safe)
 * - Parameter getters/setters are thread-safe
 * - State save/load must be called from main thread
 * - Editor methods must be called from UI thread
 */
class IPluginInstance {
public:
    virtual ~IPluginInstance() = default;
    
    // ==============================
    // Lifecycle Management
    // ==============================
    
    /**
     * @brief Initialize the plugin for processing
     * @param sampleRate Audio sample rate in Hz
     * @param maxBlockSize Maximum frames per process() call
     * @return true on success
     */
    virtual bool initialize(double sampleRate, uint32_t maxBlockSize) = 0;
    
    /**
     * @brief Shutdown the plugin, releasing all resources
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Activate the plugin for processing
     * Called when audio engine starts or plugin is enabled
     */
    virtual void activate() = 0;
    
    /**
     * @brief Deactivate the plugin
     * Called when audio engine stops or plugin is disabled
     */
    virtual void deactivate() = 0;
    
    /**
     * @brief Check if plugin is currently active
     */
    virtual bool isActive() const = 0;
    
    // ==============================
    // Audio Processing (RT-safe)
    // ==============================
    
    /**
     * @brief Process audio through the plugin
     * 
     * This method is called from the audio thread and must be RT-safe:
     * - No memory allocations
     * - No locks (except lock-free structures)
     * - No system calls
     * 
     * @param inputs Array of input channel buffers
     * @param outputs Array of output channel buffers
     * @param numInputChannels Number of input channels
     * @param numOutputChannels Number of output channels
     * @param numFrames Number of frames to process
     * @param midiInput Optional MIDI input buffer
     * @param midiOutput Optional MIDI output buffer
     */
    virtual void process(
        const float* const* inputs,
        float** outputs,
        uint32_t numInputChannels,
        uint32_t numOutputChannels,
        uint32_t numFrames,
        const MidiBuffer* midiInput = nullptr,
        MidiBuffer* midiOutput = nullptr
    ) = 0;
    
    // ==============================
    // Parameters
    // ==============================
    
    /**
     * @brief Get all parameter descriptors
     */
    virtual std::vector<PluginParameter> getParameters() const = 0;
    
    /**
     * @brief Get parameter count
     */
    virtual uint32_t getParameterCount() const = 0;
    
    /**
     * @brief Get current parameter value (normalized 0-1)
     * @param id Parameter ID
     * @return Current value, or 0 if parameter not found
     */
    virtual float getParameter(uint32_t id) const = 0;
    
    /**
     * @brief Set parameter value (normalized 0-1)
     * @param id Parameter ID
     * @param value Normalized value (0-1)
     */
    virtual void setParameter(uint32_t id, float value) = 0;
    
    /**
     * @brief Get parameter value as display string
     * @param id Parameter ID
     * @return Formatted string for display
     */
    virtual std::string getParameterDisplay(uint32_t id) const = 0;
    
    // ==============================
    // State Management
    // ==============================
    
    /**
     * @brief Save plugin state to binary data
     * @return State data, empty vector on failure
     */
    virtual std::vector<uint8_t> saveState() const = 0;
    
    /**
     * @brief Load plugin state from binary data
     * @param state State data from previous saveState()
     * @return true on success
     */
    virtual bool loadState(const std::vector<uint8_t>& state) = 0;
    
    // ==============================
    // Editor (UI Thread)
    // ==============================
    
    /**
     * @brief Check if plugin has a graphical editor
     */
    virtual bool hasEditor() const = 0;
    
    /**
     * @brief Open the plugin editor window
     * @param parentWindow Native parent window handle (HWND on Windows)
     * @return true if editor opened successfully
     */
    virtual bool openEditor(void* parentWindow) = 0;
    
    /**
     * @brief Close the plugin editor window
     */
    virtual void closeEditor() = 0;
    
    /**
     * @brief Check if editor is currently open
     */
    virtual bool isEditorOpen() const = 0;
    
    /**
     * @brief Get preferred editor size
     * @return Width and height in pixels
     */
    virtual std::pair<int, int> getEditorSize() const = 0;
    
    /**
     * @brief Resize editor to new size
     * @param width New width in pixels
     * @param height New height in pixels
     * @return true if resize was successful
     */
    virtual bool resizeEditor(int width, int height) = 0;
    
    // ==============================
    // Plugin Info
    // ==============================
    
    /**
     * @brief Get plugin information
     */
    virtual const PluginInfo& getInfo() const = 0;
    
    /**
     * @brief Get latency introduced by the plugin in samples
     */
    virtual uint32_t getLatencySamples() const = 0;
    
    /**
     * @brief Get tail length in samples (for effects like reverb)
     */
    virtual uint32_t getTailSamples() const = 0;

    // ==============================
    // Watchdog (Real-Time Safety)
    // ==============================

    /**
     * @brief Watchdog statistics
     */
    struct WatchdogStats {
        double maxExecutionTimeNs = 0.0;     ///< Peak execution time in nanoseconds
        double avgExecutionTimeNs = 0.0;     ///< Exponential moving average
        uint64_t violationCount = 0;         ///< Number of budget violations
        bool isBypassed = false;             ///< Automatically bypassed by watchdog
    };

    /**
     * @brief Get current watchdog statistics
     */
    virtual WatchdogStats getWatchdogStats() const = 0;

    /**
     * @brief Reset watchdog statistics and re-enable plugin if bypassed
     */
    virtual void resetWatchdog() = 0;

    /**
     * @brief Check if plugin was bypassed by the watchdog
     */
    virtual bool isBypassedByWatchdog() const = 0;

    /**
     * @brief Check if plugin has crashed (fatal exception caught)
     */
    virtual bool isCrashed() const = 0;
};

/**
 * @brief Unique pointer type for plugin instances
 */
using PluginInstancePtr = std::shared_ptr<IPluginInstance>;

/**
 * @brief Simple MIDI buffer for plugin processing
 */
class MidiBuffer {
public:
    struct Event {
        uint32_t sampleOffset;  ///< Sample offset within buffer
        uint8_t data[4];        ///< MIDI data (3 bytes + padding)
        uint8_t size;           ///< Number of valid bytes (1-3)
    };
    
    void addEvent(uint32_t sampleOffset, const uint8_t* data, uint8_t size) {
        if (size <= 3) {
            uint32_t index = m_eventCount.fetch_add(1, std::memory_order_relaxed);
            if (index < MAX_EVENTS) {
                Event& e = m_events[index];
                e.sampleOffset = sampleOffset;
                e.size = size;
                std::memcpy(e.data, data, size);
            } else {
                // Buffer full, rollback count (not strictly necessary but cleaner)
                m_eventCount.store(MAX_EVENTS, std::memory_order_relaxed);
            }
        }
    }
    
    void clear() { m_eventCount.store(0, std::memory_order_release); }
    bool isEmpty() const { return m_eventCount.load(std::memory_order_acquire) == 0; }
    size_t getEventCount() const { 
        size_t count = m_eventCount.load(std::memory_order_acquire);
        return count > MAX_EVENTS ? MAX_EVENTS : count;
    }
    const Event& getEvent(size_t index) const { return m_events[index]; }
    
    // Iterator support
    const Event* begin() const { return &m_events[0]; }
    const Event* end() const { return &m_events[getEventCount()]; }
    
private:
    static constexpr size_t MAX_EVENTS = 1024;
    Event m_events[MAX_EVENTS];
    std::atomic<uint32_t> m_eventCount{0};
};

} // namespace Audio
} // namespace Nomad
