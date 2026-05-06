#include "Core/AudioEngine.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "RealtimeThreadGuard.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

std::atomic<int> g_rtMisuseCount{0};

void countRtMisuse(const char*) noexcept {
    g_rtMisuseCount.fetch_add(1, std::memory_order_relaxed);
}

class GainTestPlugin final : public IPluginInstance {
public:
    explicit GainTestPlugin(float gain) : m_gain(gain) {
        m_info.id = "test.gain";
        m_info.name = "Gain Test";
        m_info.vendor = "Aestra Tests";
        m_info.version = "1.0";
        m_info.category = "Test";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 2;
        m_info.numAudioOutputs = 2;
    }

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_initialized = true;
        return true;
    }

    void shutdown() override { m_active = false; }
    void activate() override { m_active = true; }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* = nullptr, MidiBuffer* = nullptr) override {
        ++processCalls;
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            const float* in = (ch < numInputChannels && inputs[ch]) ? inputs[ch] : outputs[ch];
            for (uint32_t i = 0; i < numFrames; ++i) {
                outputs[ch][i] = in[i] * m_gain;
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
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    uint32_t initializedMaxBlockSize() const { return m_maxBlockSize; }
    double initializedSampleRate() const { return m_sampleRate; }

    int processCalls{0};

private:
    PluginInfo m_info;
    float m_gain{1.0f};
    double m_sampleRate{0.0};
    uint32_t m_maxBlockSize{0};
    bool m_initialized{false};
    bool m_active{false};
};

void attachPlugin(MixerChannel& channel, const std::shared_ptr<GainTestPlugin>& plugin) {
    const bool inserted = channel.getEffectChain().insertPlugin(0, plugin);
    assert(inserted);
    channel.setEffectChainSnapshot(channel.getEffectChain().getSnapshot());
}

std::vector<float> makeStereoBlock(uint32_t frames, float value) {
    return std::vector<float>(static_cast<size_t>(frames) * 2, value);
}

void assertBuffersNear(const std::vector<float>& actual, const std::vector<float>& expected) {
    assert(actual.size() == expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        assert(std::abs(actual[i] - expected[i]) < 0.0001f);
    }
}

void assertBuffersNearScaled(const std::vector<float>& actual, const std::vector<float>& expected, float scale) {
    assert(actual.size() == expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        assert(std::abs(actual[i] - expected[i] * scale) < 0.0001f);
    }
}

void preparedChannelProcessesInsert() {
    constexpr uint32_t kFrames = 64;
    MixerChannel channel("Prepared", 1);
    channel.prepareProcessingBuffers(kFrames);
    channel.getEffectChain().prepare(48000.0, kFrames);
    auto plugin = std::make_shared<GainTestPlugin>(2.0f);
    attachPlugin(channel, plugin);

    auto buffer = makeStereoBlock(kFrames, 0.25f);
    MixerChannel baselineChannel("Prepared Baseline", 1);
    baselineChannel.prepareProcessingBuffers(kFrames);
    auto baseline = makeStereoBlock(kFrames, 0.25f);
    baselineChannel.processAudio(baseline.data(), kFrames, 0.0, 48000.0);

    channel.processAudio(buffer.data(), kFrames, 0.0, 48000.0);

    assert(plugin->processCalls == 1);
    assertBuffersNearScaled(buffer, baseline, 2.0f);
    std::cout << "PASS: preparedChannelProcessesInsert\n";
}

void unpreparedChannelSkipsInsertFailSafe() {
    constexpr uint32_t kFrames = 64;
    MixerChannel channel("Unprepared", 1);
    auto plugin = std::make_shared<GainTestPlugin>(2.0f);
    attachPlugin(channel, plugin);

    auto buffer = makeStereoBlock(kFrames, 0.25f);
    MixerChannel baselineChannel("Unprepared Baseline", 1);
    auto baseline = makeStereoBlock(kFrames, 0.25f);
    baselineChannel.processAudio(baseline.data(), kFrames, 0.0, 48000.0);

    channel.processAudio(buffer.data(), kFrames, 0.0, 48000.0);

    assert(plugin->processCalls == 0);
    assertBuffersNear(buffer, baseline);
    std::cout << "PASS: unpreparedChannelSkipsInsertFailSafe\n";
}

void oversizedBlockSkipsInsertFailSafe() {
    constexpr uint32_t kPreparedFrames = 64;
    constexpr uint32_t kOversizedFrames = 128;
    MixerChannel channel("Oversized", 1);
    channel.prepareProcessingBuffers(kPreparedFrames);
    channel.getEffectChain().prepare(48000.0, kPreparedFrames);
    auto plugin = std::make_shared<GainTestPlugin>(2.0f);
    attachPlugin(channel, plugin);

    auto buffer = makeStereoBlock(kOversizedFrames, 0.25f);
    MixerChannel baselineChannel("Oversized Baseline", 1);
    baselineChannel.prepareProcessingBuffers(kPreparedFrames);
    auto baseline = makeStereoBlock(kOversizedFrames, 0.25f);
    baselineChannel.processAudio(baseline.data(), kOversizedFrames, 0.0, 48000.0);

    channel.processAudio(buffer.data(), kOversizedFrames, 0.0, 48000.0);

    assert(plugin->processCalls == 0);
    assertBuffersNear(buffer, baseline);
    std::cout << "PASS: oversizedBlockSkipsInsertFailSafe\n";
}

void channelAddedAfterEngineConfigIsPrepared() {
    constexpr uint32_t kFrames = 64;
    auto trackManager = std::make_shared<TrackManager>();
    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(48000);
    engine.setBufferConfig(kFrames, 2);

    auto* channel = trackManager->addChannel("Post Config");
    assert(channel != nullptr);
    auto plugin = std::make_shared<GainTestPlugin>(2.0f);
    attachPlugin(*channel, plugin);

    auto buffer = makeStereoBlock(kFrames, 0.25f);
    MixerChannel baselineChannel("Post Config Baseline", 1);
    baselineChannel.prepareProcessingBuffers(kFrames);
    auto baseline = makeStereoBlock(kFrames, 0.25f);
    baselineChannel.processAudio(baseline.data(), kFrames, 0.0, 48000.0);

    channel->processAudio(buffer.data(), kFrames, 0.0, 48000.0);

    assert(plugin->processCalls == 1);
    assertBuffersNearScaled(buffer, baseline, 2.0f);
    std::cout << "PASS: channelAddedAfterEngineConfigIsPrepared\n";
}

void addChannelRejectedFromRealtimeScope() {
    TrackManager trackManager;
    g_rtMisuseCount.store(0, std::memory_order_relaxed);
    auto previousHandler = setRealtimeMisuseHandler(&countRtMisuse);
    {
        ScopedRealtimeAudioThread realtimeScope;
        auto* channel = trackManager.addChannel("RT Add");
        assert(channel == nullptr);
    }
    setRealtimeMisuseHandler(previousHandler);
    assert(g_rtMisuseCount.load(std::memory_order_relaxed) == 1);
    assert(trackManager.getChannelCount() == 0);
    std::cout << "PASS: addChannelRejectedFromRealtimeScope\n";
}

void bufferReconfigurationPreparesExistingAndFutureChannels() {
    constexpr uint32_t kFramesA = 4096;
    constexpr uint32_t kFramesB = 8192;
    constexpr uint32_t kFramesC = 12288;

    auto trackManager = std::make_shared<TrackManager>();
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setTrackManager(trackManager);
    engine.setBufferConfig(kFramesA, 2);

    auto* first = trackManager->addChannel("First");
    assert(first != nullptr);
    auto firstPlugin = std::make_shared<GainTestPlugin>(2.0f);
    attachPlugin(*first, firstPlugin);
    assert(firstPlugin->initializedMaxBlockSize() == kFramesA);
    assert(std::abs(firstPlugin->initializedSampleRate() - 48000.0) < 0.0001);

    engine.setBufferConfig(kFramesB, 2);
    assert(firstPlugin->initializedMaxBlockSize() == kFramesB);

    auto* second = trackManager->addChannel("Second");
    assert(second != nullptr);
    auto secondPlugin = std::make_shared<GainTestPlugin>(2.0f);
    attachPlugin(*second, secondPlugin);
    assert(secondPlugin->initializedMaxBlockSize() == kFramesB);

    engine.setBufferConfig(kFramesC, 2);
    assert(firstPlugin->initializedMaxBlockSize() == kFramesC);
    assert(secondPlugin->initializedMaxBlockSize() == kFramesC);

    auto* third = trackManager->addChannel("Third");
    assert(third != nullptr);
    auto thirdPlugin = std::make_shared<GainTestPlugin>(2.0f);
    attachPlugin(*third, thirdPlugin);
    assert(thirdPlugin->initializedMaxBlockSize() == kFramesC);

    auto firstBuffer = makeStereoBlock(kFramesC, 0.25f);
    first->processAudio(firstBuffer.data(), kFramesC, 0.0, 48000.0);
    assert(firstPlugin->processCalls == 1);

    auto secondBuffer = makeStereoBlock(kFramesC, 0.25f);
    second->processAudio(secondBuffer.data(), kFramesC, 0.0, 48000.0);
    assert(secondPlugin->processCalls == 1);

    auto thirdBuffer = makeStereoBlock(kFramesC, 0.25f);
    third->processAudio(thirdBuffer.data(), kFramesC, 0.0, 48000.0);
    assert(thirdPlugin->processCalls == 1);

    std::cout << "PASS: bufferReconfigurationPreparesExistingAndFutureChannels\n";
}

} // namespace

int main() {
    preparedChannelProcessesInsert();
    unpreparedChannelSkipsInsertFailSafe();
    oversizedBlockSkipsInsertFailSafe();
    channelAddedAfterEngineConfigIsPrepared();
    addChannelRejectedFromRealtimeScope();
    bufferReconfigurationPreparesExistingAndFutureChannels();
    return 0;
}
