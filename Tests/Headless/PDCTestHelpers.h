// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDCTestHelpers — shared scaffolding for Plugin Delay Compensation tests (PDC-v2 P1+).
//
// See Aestra-Internals: aestra-docs/PDC-v2-Design.md §10 for the full test plan.
//
// Provides a minimal IPluginInstance mock that reports a configurable latency
// and passes audio through unmodified. Pass-through is intentional: PDC tests
// care about reported latency and graph topology, not DSP behavior.

#pragma once

#include "Plugin/PluginHost.h"

#include <atomic>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {
namespace PDCTest {

class MockLatencyPlugin : public IPluginInstance {
public:
    explicit MockLatencyPlugin(uint32_t latencySamples, std::string name = "MockLatency")
        : m_latency(latencySamples) {
        m_info.id = "aestra.test.mock_latency." + std::to_string(latencySamples);
        m_info.name = std::move(name);
        m_info.vendor = "Aestra Test";
        m_info.version = "1.0";
        m_info.category = "Test";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 2;
        m_info.numAudioOutputs = 2;
    }

    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override { m_active = true; }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                 uint32_t numOutputChannels, uint32_t numFrames,
                 const MidiBuffer* /*midiInput*/ = nullptr,
                 MidiBuffer* /*midiOutput*/ = nullptr) override {
        // Pass-through. PDC v2 tests care about reported latency, not DSP.
        const uint32_t channels = (numOutputChannels < numInputChannels) ? numOutputChannels : numInputChannels;
        for (uint32_t c = 0; c < channels; ++c) {
            if (inputs && inputs[c] && outputs && outputs[c]) {
                std::memcpy(outputs[c], inputs[c], sizeof(float) * numFrames);
            } else if (outputs && outputs[c]) {
                std::memset(outputs[c], 0, sizeof(float) * numFrames);
            }
        }
    }

    std::vector<PluginParameter> getParameters() const override { return {}; }
    uint32_t getParameterCount() const override { return 0; }
    float getParameter(uint32_t) const override { return 0.0f; }
    void setParameter(uint32_t, float) override {}
    std::string getParameterDisplay(uint32_t) const override { return {}; }

    std::vector<uint8_t> saveState() const override { return {}; }
    bool loadState(const std::vector<uint8_t>&) override { return true; }

    bool hasEditor() const override { return false; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {0, 0}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }

    uint32_t getLatencySamples() const override { return m_latency.load(std::memory_order_relaxed); }
    uint32_t getTailSamples() const override { return 0; }

    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    // Test-only knob: simulate a mid-session latency change (used by future P6 G3 tests).
    void setLatencySamples(uint32_t value) { m_latency.store(value, std::memory_order_relaxed); }

private:
    PluginInfo m_info{};
    std::atomic<uint32_t> m_latency{0};
    bool m_active{false};
};

} // namespace PDCTest
} // namespace Audio
} // namespace Aestra
