// © 2025 Aestra Studios — All Rights Reserved.
// Regression tests for insert-effect graph invalidation without clip movement.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginHost.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kBlockFrames = 512;
constexpr double kBpm = 120.0;
constexpr double kDurationSeconds = 0.25;
constexpr double kDurationBeats = 1.0;

class TestGainPlugin final : public IPluginInstance {
public:
    explicit TestGainPlugin(float gain)
        : m_gain(gain) {
        m_info.id = "test.aestra.gain";
        m_info.name = "Test Gain";
        m_info.vendor = "Aestra Tests";
        m_info.version = "1";
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
                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* = nullptr,
                 MidiBuffer* = nullptr) override {
        assert(inputs != nullptr);
        assert(outputs != nullptr);
        const uint32_t processChannels = std::min(numInputChannels, numOutputChannels);
        for (uint32_t ch = 0; ch < processChannels; ++ch) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                outputs[ch][i] = inputs[ch][i] * m_gain;
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

private:
    float m_gain{1.0f};
    bool m_active{false};
    PluginInfo m_info{};
};

std::shared_ptr<AudioBufferData> makeConstantBuffer(float value) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = kSampleRate;
    buffer->numChannels = kChannels;
    buffer->numFrames = static_cast<uint32_t>(kSampleRate * kDurationSeconds);
    buffer->interleavedData.resize(static_cast<size_t>(buffer->numFrames) * kChannels, value);
    return buffer;
}

std::shared_ptr<TrackManager> makeTrackManagerWithClip() {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));
    trackManager->getPlaylistModel().setBPM(kBpm);
    auto* track = trackManager->addChannel("Track 1");
    assert(track != nullptr);

    const PlaylistLaneID laneId = trackManager->getPlaylistModel().createLane("Track 1");
    auto buffer = makeConstantBuffer(0.5f);
    const ClipSourceID sourceId =
        trackManager->getSourceManager().createRecordedSource("/tmp/aestra_plugin_graph_source.wav",
                                                              "aestra_plugin_graph_source", buffer);
    assert(sourceId.isValid());

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = kDurationSeconds;
    payload.slices.push_back({0.0, kDurationSeconds, 0.0, static_cast<double>(buffer->numFrames)});
    const PatternID patternId =
        trackManager->getPatternManager().createAudioPattern("clip", kDurationBeats, payload);
    assert(patternId.isValid());

    const ClipInstanceID clipId =
        trackManager->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, kDurationBeats);
    assert(clipId.isValid());
    trackManager->rebuildAndPushSnapshot();
    return trackManager;
}

void configureEngine(AudioEngine& engine, const std::shared_ptr<TrackManager>& trackManager) {
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockFrames, kChannels);
    engine.setTrackManager(trackManager);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    if (auto slotMap = trackManager->getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager, static_cast<double>(kSampleRate)));
    engine.initialize();
}

void drainGraphDirty(TrackManager& trackManager, AudioEngine& engine) {
    if (!trackManager.consumeGraphDirty()) {
        return;
    }
    if (auto slotMap = trackManager.getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(trackManager, static_cast<double>(kSampleRate)));
    trackManager.rebuildAndPushSnapshot();
}

float renderActiveTrackSnapshotMean(AudioEngine& engine) {
    const auto& activeGraph = engine.engineState().activeGraph();
    assert(!activeGraph.tracks.empty());
    std::vector<float> left(kBlockFrames, 1.0f);
    std::vector<float> right(kBlockFrames, 1.0f);
    std::vector<float> dry(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    float* channels[2] = {left.data(), right.data()};

    if (activeGraph.tracks[0].effectChainSnapshot) {
        activeGraph.tracks[0].effectChainSnapshot->process(channels, kChannels, kBlockFrames, nullptr, 0, dry.data());
    }

    float sum = 0.0f;
    for (uint32_t i = 0; i < kBlockFrames; ++i) {
        sum += std::abs(left[i]);
        sum += std::abs(right[i]);
    }
    return sum / static_cast<float>(kBlockFrames * kChannels);
}

void PluginInsertTakesEffectWithoutClipMoveTest() {
    auto trackManager = makeTrackManagerWithClip();
    AudioEngine engine;
    configureEngine(engine, trackManager);

    const float dryMean = renderActiveTrackSnapshotMean(engine);
    const uint64_t generationBefore = engine.graphGeneration();

    auto* channel = trackManager->getChannel(0);
    assert(channel != nullptr);
    auto plugin = std::make_shared<TestGainPlugin>(0.5f);
    channel->getEffectChain().prepare(static_cast<double>(kSampleRate), kBlockFrames);
    const bool inserted = channel->getEffectChain().insertPlugin(0, plugin);
    assert(inserted);
    trackManager->requestAudioGraphRebuild(TrackManager::GraphDirtyReason::EffectChainChanged);
    drainGraphDirty(*trackManager, engine);

    const auto& activeGraph = engine.engineState().activeGraph();
    assert(engine.graphGeneration() > generationBefore);
    assert(!activeGraph.tracks.empty());
    assert(activeGraph.tracks[0].effectChainSnapshot != nullptr);
    assert(activeGraph.tracks[0].effectChainSnapshot->getActiveSlotCount() == 1);

    const float wetMean = renderActiveTrackSnapshotMean(engine);
    assert(wetMean < dryMean * 0.6f);
    assert(wetMean > dryMean * 0.4f);
    std::cout << "PASS: PluginInsertTakesEffectWithoutClipMoveTest\n";
}

void PluginRemoveTakesEffectWithoutClipMoveTest() {
    auto trackManager = makeTrackManagerWithClip();
    AudioEngine engine;
    configureEngine(engine, trackManager);

    auto* channel = trackManager->getChannel(0);
    assert(channel != nullptr);
    auto plugin = std::make_shared<TestGainPlugin>(0.5f);
    channel->getEffectChain().prepare(static_cast<double>(kSampleRate), kBlockFrames);
    assert(channel->getEffectChain().insertPlugin(0, plugin));
    trackManager->requestAudioGraphRebuild(TrackManager::GraphDirtyReason::EffectChainChanged);
    drainGraphDirty(*trackManager, engine);
    const float wetMean = renderActiveTrackSnapshotMean(engine);
    const uint64_t generationBeforeRemove = engine.graphGeneration();

    auto removed = channel->getEffectChain().removePlugin(0);
    assert(removed != nullptr);
    trackManager->requestAudioGraphRebuild(TrackManager::GraphDirtyReason::EffectChainChanged);
    drainGraphDirty(*trackManager, engine);

    const auto& activeGraph = engine.engineState().activeGraph();
    assert(engine.graphGeneration() > generationBeforeRemove);
    assert(!activeGraph.tracks.empty());
    assert(activeGraph.tracks[0].effectChainSnapshot != nullptr);
    assert(activeGraph.tracks[0].effectChainSnapshot->getActiveSlotCount() == 0);

    const float dryMean = renderActiveTrackSnapshotMean(engine);
    assert(dryMean > wetMean * 1.3f);
    std::cout << "PASS: PluginRemoveTakesEffectWithoutClipMoveTest\n";
}

void PluginBypassPublishesSnapshotAndRequestsGraphDirtyTest() {
    auto trackManager = makeTrackManagerWithClip();
    auto* channel = trackManager->getChannel(0);
    assert(channel != nullptr);
    auto plugin = std::make_shared<TestGainPlugin>(0.5f);
    auto& chain = channel->getEffectChain();
    chain.prepare(static_cast<double>(kSampleRate), kBlockFrames);
    assert(chain.insertPlugin(0, plugin));

    chain.setSlotBypassed(0, true);
    trackManager->requestAudioGraphRebuild(TrackManager::GraphDirtyReason::EffectChainChanged);
    assert(trackManager->consumeGraphDirty());
    auto snapshot = chain.getSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->slot(0).bypassed);
    std::cout << "PASS: PluginBypassPublishesSnapshotAndRequestsGraphDirtyTest\n";
}

} // namespace

int main() {
    PluginInsertTakesEffectWithoutClipMoveTest();
    PluginRemoveTakesEffectWithoutClipMoveTest();
    PluginBypassPublishesSnapshotAndRequestsGraphDirtyTest();
    std::cout << "All plugin graph invalidation tests passed\n";
    return 0;
}
