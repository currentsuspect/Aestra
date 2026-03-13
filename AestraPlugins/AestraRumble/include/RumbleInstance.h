#pragma once

#include "Plugin/PluginHost.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Plugins {

class RumbleInstance : public Aestra::Audio::IPluginInstance {
public:
    RumbleInstance();
    ~RumbleInstance() override = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override;
    void shutdown() override;
    void activate() override;
    void deactivate() override;
    bool isActive() const override { return m_active.load(std::memory_order_acquire); }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const Aestra::Audio::MidiBuffer* midiInput = nullptr,
                 Aestra::Audio::MidiBuffer* midiOutput = nullptr) override;

    std::vector<Aestra::Audio::PluginParameter> getParameters() const override;
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

    const Aestra::Audio::PluginInfo& getInfo() const override;
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override;

    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

private:
    enum ParamID : uint32_t {
        kParamDecay = 0,
        kParamDrive,
        kParamTone,
        kParamOutputGain,
        kParamCount,
    };

    struct Voice {
        bool active = false;
        uint8_t note = 36;
        float velocity = 1.0f;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        double envelope = 0.0;
        double releaseCoeff = 0.0;
    };

    void handleMidiEvent(const Aestra::Audio::MidiBuffer::Event& event);
    static float midiNoteToFrequency(uint8_t note);
    static float clamp01(float value);
    float getDecaySeconds() const;
    float getDriveAmount() const;
    float getToneHz() const;
    float getOutputGainLinear() const;
    void updateVoiceTuning();
    void updateEnvelopeRate();
    void updateToneCoefficient();
    float applyDrive(float input) const;
    float processToneFilter(float input);

    std::atomic<bool> m_active{false};
    double m_sampleRate = 44100.0;
    uint32_t m_maxBlockSize = 512;

    std::array<std::atomic<float>, kParamCount> m_params;
    Voice m_voice;

    float m_filterState = 0.0f;
    float m_filterAlpha = 0.0f;
};

} // namespace Plugins
} // namespace Aestra
