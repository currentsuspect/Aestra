// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <array>
#include <atomic>
#include <memory>
#include "AestraAtomicSharedPtr.h"
#include <mutex>
#include <string>
#include <vector>

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

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) override;

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
    std::pair<int, int> getEditorSize() const override { return {0, 0}; }
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
    bool loadSampleData(const std::string& path, std::vector<float> data, uint32_t rate, uint32_t channels);
    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }
    void setEnvelope(float attack, float decay, float sustain, float release) noexcept;
    void setCoarseSemitones(float semitones) noexcept;
    void setFineTuneCents(float cents) noexcept;
    void setSampleWindow(float startNorm, float endNorm) noexcept;
    void setLoopEnabled(bool enabled) noexcept;
    void setMaxVoices(int maxVoices) noexcept;
    void setRootMidiNote(int note) noexcept;
    void setMonoMode(bool mono) noexcept;
    void setGlideTimeMs(float glideTimeMs) noexcept;
    bool normalizeSample(float targetPeak = 0.95f);
    bool reverseSample();

    float getCoarseSemitones() const noexcept;
    float getFineTuneCents() const noexcept;
    float getLoopStartNorm() const noexcept { return m_loopStartNorm.load(std::memory_order_relaxed); }
    float getLoopEndNorm() const noexcept { return m_loopEndNorm.load(std::memory_order_relaxed); }
    bool isLoopEnabled() const noexcept { return m_loopEnabled.load(std::memory_order_relaxed); }
    int getMaxVoices() const noexcept { return m_maxVoices.load(std::memory_order_relaxed); }
    int getRootMidiNote() const noexcept { return m_rootMidiNote.load(std::memory_order_relaxed); }
    bool isMonoMode() const noexcept { return m_monoMode.load(std::memory_order_relaxed); }
    float getGlideTimeMs() const noexcept { return m_glideTimeMs.load(std::memory_order_relaxed); }
    float getAttack() const noexcept { return m_params[kParamAttack].load(std::memory_order_relaxed); }
    float getDecay() const noexcept { return m_params[kParamDecay].load(std::memory_order_relaxed); }
    float getSustain() const noexcept { return m_params[kParamSustain].load(std::memory_order_relaxed); }
    float getRelease() const noexcept { return m_params[kParamRelease].load(std::memory_order_relaxed); }

    // RT-safe: requests an immediate voice reset on the next process() call.
    void requestHardResetVoices() noexcept { m_resetVoicesRequested.store(true, std::memory_order_release); }

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

    // Shared Ptr accessed atomically (C++11/17 free functions)
    // No mutex needed for access anymore!
    AtomicSharedPtr<SampleData> m_data;

    // Parameters
    enum ParamID { kParamAttack = 0, kParamDecay, kParamSustain, kParamRelease, kParamPitch, kParamCount };
    std::array<std::atomic<float>, kParamCount> m_params;
    std::atomic<float> m_fineTuneCents{0.0f};
    std::atomic<float> m_loopStartNorm{0.0f};
    std::atomic<float> m_loopEndNorm{1.0f};
    std::atomic<bool> m_loopEnabled{false}; // one-shot default
    std::atomic<int> m_maxVoices{4};
    std::atomic<int> m_rootMidiNote{60}; // C3 default
    std::atomic<bool> m_monoMode{false};
    std::atomic<float> m_glideTimeMs{80.0f};

    // Voice Architecture
    enum class EnvStage { Attack, Decay, Sustain, Release, Off };

    struct Voice {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        double position = 0.0; // Sample index
        double playbackRate = 1.0; // sample index increment/frame
        double targetPlaybackRate = 1.0;
        bool glideActive = false;
        EnvStage stage = EnvStage::Off;
        double stageTime = 0.0; // Seconds in current stage
        float currentGain = 0.0f;
        float releaseGain = 0.0f; // Gain at start of release
    };

    static constexpr int kMaxVoices = 32;
    std::array<Voice, kMaxVoices> m_voices;

    // Helpers
    void handleMidiEvent(const MidiBuffer::Event& event, double baseRate,
                         const std::shared_ptr<SampleData>& currentData);
    void renderVoice(Voice& voice, float* outL, float* outR, uint32_t frames);
    float getEnvelopeLevel(Voice& voice, double dt, float attack, float decay, float sustain, float release);
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
