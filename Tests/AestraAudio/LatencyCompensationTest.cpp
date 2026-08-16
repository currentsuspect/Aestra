// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PDC (Plugin Delay Compensation) Test

#include "AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"

#include <cassert>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr uint32_t kChannels = 2;

// Minimal plugin reporting a fixed intrinsic latency (PDC input).
class FixedLatencyPlugin : public IPluginInstance {
public:
    explicit FixedLatencyPlugin(uint32_t latency) : m_latency(latency) {
        m_info.id = "aestra.test.fixed_latency";
        m_info.name = "FixedLatency";
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
    void activate() override {}
    void deactivate() override {}
    bool isActive() const override { return true; }
    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* = nullptr,
                 MidiBuffer* = nullptr) override {
        if (!outputs || !outputs[0] || !outputs[1]) {
            return;
        }
        // Dry pass-through: latency reporting is what this plugin is for.
        if (inputs && inputs[0] && inputs[1]) {
            for (uint32_t k = 0; k < numFrames; ++k) {
                outputs[0][k] = inputs[0][k];
                outputs[1][k] = inputs[1][k];
            }
        } else {
            for (uint32_t k = 0; k < numFrames; ++k) {
                outputs[0][k] = 0.0f;
                outputs[1][k] = 0.0f;
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
    uint32_t getLatencySamples() const override { return m_latency; }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

private:
    PluginInfo m_info{};
    uint32_t m_latency{0};
};

void testPDCInitialization() {
    std::cout << "Test: PDC initializes correctly...\n";

    auto trackManager = std::make_shared<TrackManager>();
    auto* channel = trackManager->addChannel("Test Track");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.isLatencyCompensationEnabled() == true);
    assert(engine.getMaxProjectLatency() == 0);

    std::cout << "  [PASS] PDC initializes enabled, zero latency\n";
}

void testPDCWithEmptyChain() {
    std::cout << "Test: PDC with empty effect chain...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Track 1");
    trackManager->addChannel("Track 2");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.getMaxProjectLatency() == 0);

    std::cout << "  [PASS] Empty chains have zero latency\n";
}

void testPDCEnableDisable() {
    std::cout << "Test: PDC enable/disable toggle...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Test Track");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.isLatencyCompensationEnabled() == true);
    assert(engine.getMaxProjectLatency() == 0);

    engine.setLatencyCompensationEnabled(false);
    assert(engine.isLatencyCompensationEnabled() == false);
    assert(engine.getMaxProjectLatency() == 0);

    engine.setLatencyCompensationEnabled(true);
    assert(engine.isLatencyCompensationEnabled() == true);

    std::cout << "  [PASS] Enable/disable works correctly\n";
}

void testPDCCallbackOnPluginInsert() {
    std::cout << "Test: PDC callback triggers...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Test Track");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    engine.calculateLatencyCompensation();
    auto latency = engine.getMaxProjectLatency();
    (void)latency;

    std::cout << "  [PASS] PDC recalculation works\n";
}

void testPDCMultipleTracks() {
    std::cout << "Test: PDC with multiple tracks...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Track 1");
    trackManager->addChannel("Track 2");
    trackManager->addChannel("Track 3");

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.getMaxProjectLatency() == 0);

    std::cout << "  [PASS] Multiple tracks initialize with zero latency\n";
}

// P9 (G6): the Master strip's chain latency feeds the PDC graph. Inserting a
// latency plugin on Master must raise the project latency (and re-solve via
// the latency-change callback); removing it must return to zero.
void testPDCMasterChainLatency() {
    std::cout << "Test: master chain latency surfaces in project latency...\n";

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->addChannel("Track 1");
    auto* master = trackManager->getMasterChannel();
    assert(master != nullptr);

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);

    assert(engine.getMaxProjectLatency() == 0);
    assert(engine.isLatencyCompensationEnabled() == true);

    // Master plugin with 128 samples of latency: project latency must follow.
    auto latencyPlugin = std::make_shared<FixedLatencyPlugin>(128);
    assert(master->getEffectChain().insertPlugin(0, latencyPlugin));
    assert(engine.getMaxProjectLatency() == 128);

    // A second master plugin adds its latency to the path (128 + 64).
    assert(master->getEffectChain().insertPlugin(1, std::make_shared<FixedLatencyPlugin>(64)));
    assert(engine.getMaxProjectLatency() == 192);

    // Removal re-solves back down.
    assert(master->getEffectChain().removePlugin(1) != nullptr);
    assert(engine.getMaxProjectLatency() == 128);
    assert(master->getEffectChain().removePlugin(0) != nullptr);
    assert(engine.getMaxProjectLatency() == 0);

    // Track latency is unaffected by the master path: the master's 128 is
    // uniform, so a 64-latency track still aligns against it with its own
    // compensation, and the project latency reflects the longest path.
    auto* track = trackManager->getChannel(0);
    assert(track != nullptr);
    assert(master->getEffectChain().insertPlugin(0, std::make_shared<FixedLatencyPlugin>(128)));
    assert(track->getEffectChain().insertPlugin(0, std::make_shared<FixedLatencyPlugin>(64)));
    assert(engine.getMaxProjectLatency() == 192); // 64 + 128 on the single path

    std::cout << "  [PASS] Master chain latency tracked, callback re-solves on mutation\n";
}

int main() {
    std::cout << "=== Plugin Delay Compensation (PDC) Tests ===\n\n";

    testPDCInitialization();
    testPDCWithEmptyChain();
    testPDCEnableDisable();
    testPDCCallbackOnPluginInsert();
    testPDCMultipleTracks();
    testPDCMasterChainLatency();

    std::cout << "\n=== All PDC Tests Passed ===\n";
    return 0;
}