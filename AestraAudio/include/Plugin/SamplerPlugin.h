// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include <vector>
#include <atomic>
#include <mutex>
#include <string>
#include <array>
#include <memory>

namespace Aestra {
namespace Audio {
namespace Plugins {

class SamplerPlugin : public IPluginInstance {
public:
    SamplerPlugin();
    ~SamplerPlugin() override = default;

    // IPluginInstance Implementation
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

    bool hasEditor() const override { return false; }
    bool openEditor(void* parentWindow) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return { 0, 0 }; }
    bool resizeEditor(int width, int height) override { return false; }

    const PluginInfo& getInfo() const override;
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 0; }

    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    // Custom Methods
    bool loadSample(const std::string& path);
    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }

    // RT-safe: requests an immediate voice reset on the next process() call.
    void requestHardResetVoices() noexcept {
        m_resetVoicesRequested.store(true, std::memory_order_release);
    }

private:
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_resetVoicesRequested{false};
    double m_sampleRate = 44100.0;
    
    // Sample Data encapsulated for Atomic Swap
    struct SampleData {
        std::vector<float> data; // Interleaved
        uint32_t channels = 2;
        uint32_t rate = 44100;
        std::string path;
    };

    // Shared Ptr held by main thread to keep data alive
    std::shared_ptr<SampleData> m_dataHolder;
    // Raw pointer for RT thread access (lock-free)
    std::atomic<SampleData*> m_activeData{nullptr};

    // Parameters
    enum ParamID {
        kParamAttack = 0,
        kParamDecay,
        kParamSustain,
        kParamRelease,
        kParamPitch,
        kParamCount
    };
    std::array<std::atomic<float>, kParamCount> m_params;

    // Voice Architecture
    enum class EnvStage { Attack, Decay, Sustain, Release, Off };
    
    struct Voice {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        double position = 0.0; // Sample index
        EnvStage stage = EnvStage::Off;
        double stageTime = 0.0; // Seconds in current stage
        float currentGain = 0.0f;
        float releaseGain = 0.0f; // Gain at start of release
    };

    static constexpr int kMaxVoices = 32;
    std::array<Voice, kMaxVoices> m_voices;

    // Helpers
    void handleMidiEvent(const MidiBuffer::Event& event);
    void renderVoice(Voice& voice, float* outL, float* outR, uint32_t frames);
    float getEnvelopeLevel(Voice& voice, double dt);
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
