// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RoutingSendPanParityTest — acceptance tests for routing BUG-3 (#261) and BUG-5 (#262).
//
// #261: Send pan must balance an existing stereo signal without attenuating
// it at centre. A centred unity post-fader send must therefore match the
// source's direct centred output exactly.
//
// #262: Batched send processing must match a naive per-sample reference.
//   * The test recomputes the expected send output sample-by-sample from the
//     known deterministic source signal and the documented constant-power law,
//     then compares against the engine's block-batched send path.
//
// Both tests drive the real RT path (AudioEngine::processBlock) with a
// deterministic generator plugin as the signal source — no clips, no audio
// hardware, no file I/O on the verification path.

#include "../AestraAudio/GoldenAudio/GoldenAudioHarness.h"
#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/AudioGraphState.h"
#include "Core/AudioRenderer.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 256;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kWarmupBlocks = 8;  // Let gain smoothers + any startup fade converge
constexpr uint32_t kMeasureBlocks = 4; // Steady-state frames compared per config
constexpr uint32_t kMasterId = 0xFFFFFFFFu;

int g_failures = 0;

void reportFailure(const char* what, const std::string& detail) {
    std::fprintf(stderr, "[FAIL] %s — %s\n", what, detail.c_str());
    ++g_failures;
}

#define EXPECT_TRUE(expr)                                                                 \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            reportFailure(#expr, std::string(__FILE__) + ":" + std::to_string(__LINE__)); \
        }                                                                                 \
    } while (0)

// Deterministic stateless noise: pure function of the global sample index so
// the reference path can regenerate the exact source signal. splitmix64-style
// finalizer keeps it platform-independent.
double noiseAt(uint64_t n) {
    uint64_t x = n * 6364136223846793005ULL + 1442695040888963407ULL;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    // Map to [-0.25, 0.25]: headroom below master hard-clip and limiter range.
    return (static_cast<double>(x >> 11) / 9007199254740992.0 - 0.5) * 0.5;
}

// L carries the noise, R carries an inverted half-scale copy so channel swaps
// or L/R gain mixups cannot cancel out in the comparisons.
float sourceL(uint64_t n) {
    return static_cast<float>(noiseAt(n));
}
float sourceR(uint64_t n) {
    return static_cast<float>(noiseAt(n)) * -0.5f;
}

// Replicates the stereo balance law used by sends and routed destination
// channels. Centre is unity on both legs; moving away from centre attenuates
// only the opposite leg.
void referenceBalanceGains(double pan, double gain, double& gainL, double& gainR) {
    const double clampedPan = std::clamp(pan, -1.0, 1.0);
    if (clampedPan < 0.0) {
        gainL = gain;
        gainR = std::cos(-clampedPan * 1.57079632679) * gain;
    } else {
        gainL = std::cos(clampedPan * 1.57079632679) * gain;
        gainR = gain;
    }
}

// Minimal signal-generator plugin: ignores input, writes the deterministic
// source signal. Keeps its own sample counter; each config uses a fresh
// instance so counters stay block-aligned across configs.
class GeneratorPlugin : public IPluginInstance {
public:
    GeneratorPlugin() {
        m_info.id = "aestra.test.pan_parity_generator";
        m_info.name = "PanParityGenerator";
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

    void process(const float* const* /*inputs*/, float** outputs, uint32_t /*numInputChannels*/,
                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* = nullptr,
                 MidiBuffer* = nullptr) override {
        if (!outputs || numOutputChannels < 2 || !outputs[0] || !outputs[1]) {
            return;
        }
        for (uint32_t k = 0; k < numFrames; ++k) {
            outputs[0][k] = sourceL(m_sampleIndex + k);
            outputs[1][k] = sourceR(m_sampleIndex + k);
        }
        m_sampleIndex += numFrames;
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
    PluginInfo m_info{};
    uint64_t m_sampleIndex{0};
    bool m_active{false};
};

struct SendSpec {
    bool enabled{false};
    float gain{1.0f};
    float pan{0.0f};
    bool postFader{true};
};

// Renders one two-track config (source + muted sink) and returns the master
// output of the measurement window, interleaved stereo float.
// If the send is enabled, the source's main output is routed to the muted sink
// so the master receives *only* the send contribution.
std::vector<float> renderConfig(float mainPan, const SendSpec& send) {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));

    MixerChannel* src = trackManager->addChannel("src");
    MixerChannel* sink = trackManager->addChannel("sink");
    MixerChannel* bus = trackManager->addChannel("bus");
    EXPECT_TRUE(src != nullptr);
    EXPECT_TRUE(sink != nullptr);
    EXPECT_TRUE(bus != nullptr);
    if (!src || !sink || !bus) {
        return {};
    }

    sink->setMute(true);
    src->setPan(mainPan);
    EXPECT_TRUE(src->getEffectChain().insertPlugin(0, std::make_shared<GeneratorPlugin>()));

    if (send.enabled) {
        src->setMainOutputId(sink->getChannelId()); // Keep the direct path out of the master mix
        // Sends to master are illegal (Contract D4): route the send into a
        // bus that feeds master, preserving the measurement path.
        AudioRoute route;
        route.targetChannelId = bus->getChannelId();
        route.gain = send.gain;
        route.pan = send.pan;
        route.postFader = send.postFader;
        route.mute = false;
        route.sidechainOnly = false;
        src->addSend(route);
    }

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockFrames, kChannels);
    trackManager->buildAndShareSlotMap();
    if (auto slotMap = trackManager->getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    engine.setSafetyLimiterEnabled(false);

    std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    for (uint32_t b = 0; b < kWarmupBlocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, kBlockFrames, static_cast<double>(b * kBlockFrames) / kSampleRate);
    }

    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(kMeasureBlocks) * kBlockFrames * kChannels);
    for (uint32_t b = 0; b < kMeasureBlocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0f);
        const uint32_t absBlock = kWarmupBlocks + b;
        engine.processBlock(block.data(), nullptr, kBlockFrames,
                            static_cast<double>(absBlock * kBlockFrames) / kSampleRate);
        captured.insert(captured.end(), block.begin(), block.end());
    }
    return captured;
}

// Render a source through N centred mixer destinations before Master. The
// first channel still uses the source pan law; every destination is balancing
// an already-stereo route and must not add another centre attenuation.
std::vector<float> renderMainChain(size_t destinationCount) {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));

    MixerChannel* source = trackManager->addChannel("source");
    EXPECT_TRUE(source != nullptr);
    if (!source) {
        return {};
    }
    EXPECT_TRUE(source->getEffectChain().insertPlugin(0, std::make_shared<GeneratorPlugin>()));

    MixerChannel* previous = source;
    for (size_t index = 0; index < destinationCount; ++index) {
        MixerChannel* destination = trackManager->addChannel("destination");
        EXPECT_TRUE(destination != nullptr);
        if (!destination) {
            return {};
        }
        previous->setMainOutputId(destination->getChannelId());
        previous = destination;
    }

    AudioEngine engine;
    engine.setTrackManager(trackManager);
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockFrames, kChannels);
    trackManager->buildAndShareSlotMap();
    if (auto slotMap = trackManager->getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    engine.setSafetyLimiterEnabled(false);

    std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    for (uint32_t b = 0; b < kWarmupBlocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, kBlockFrames, static_cast<double>(b * kBlockFrames) / kSampleRate);
    }

    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(kMeasureBlocks) * kBlockFrames * kChannels);
    for (uint32_t b = 0; b < kMeasureBlocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0f);
        const uint32_t absBlock = kWarmupBlocks + b;
        engine.processBlock(block.data(), nullptr, kBlockFrames,
                            static_cast<double>(absBlock * kBlockFrames) / kSampleRate);
        captured.insert(captured.end(), block.begin(), block.end());
    }
    return captured;
}

std::shared_ptr<TrackManager> buildClipMainChain(size_t destinationCount, uint32_t totalFrames) {
    GoldenAudio::SessionConfig config;
    config.sampleRate = kSampleRate;
    config.blockSize = kBlockFrames;
    config.channels = kChannels;

    std::vector<float> source(static_cast<size_t>(totalFrames) * kChannels, 0.0f);
    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        source[static_cast<size_t>(frame) * 2] = sourceL(frame);
        source[static_cast<size_t>(frame) * 2 + 1] = sourceR(frame);
    }

    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));
    GoldenAudio::addAudioTrack(*trackManager, "renderer-route-source", source, totalFrames, config);

    MixerChannel* previous = trackManager->getChannel(0);
    EXPECT_TRUE(previous != nullptr);
    for (size_t index = 0; previous && index < destinationCount; ++index) {
        MixerChannel* destination = trackManager->addChannel("renderer-route-destination");
        EXPECT_TRUE(destination != nullptr);
        if (!destination) {
            break;
        }
        previous->setMainOutputId(destination->getChannelId());
        previous = destination;
    }
    return trackManager;
}

std::vector<float> renderClipMainChainLive(size_t destinationCount, uint32_t totalFrames) {
    GoldenAudio::SessionConfig config;
    config.sampleRate = kSampleRate;
    config.blockSize = kBlockFrames;
    config.channels = kChannels;

    auto trackManager = buildClipMainChain(destinationCount, totalFrames);
    AudioEngine engine;
    GoldenAudio::prepareEngine(engine, trackManager, config);
    engine.setTransportPlaying(true);
    return GoldenAudio::renderBlocks(engine, totalFrames, config);
}

std::vector<float> renderClipMainChainOffline(size_t destinationCount, uint32_t totalFrames) {
    GoldenAudio::SessionConfig config;
    config.sampleRate = kSampleRate;
    config.blockSize = kBlockFrames;
    config.channels = kChannels;

    auto trackManager = buildClipMainChain(destinationCount, totalFrames);
    AudioEngine engine;
    GoldenAudio::prepareEngine(engine, trackManager, config);
    AudioGraph graph = AudioGraphBuilder::buildFromTrackManager(*trackManager);

    std::vector<std::vector<double>> trackBuffers(
        graph.tracks.size(), std::vector<double>(static_cast<size_t>(kBlockFrames) * kChannels, 0.0));
    std::vector<double> masterBuffer(static_cast<size_t>(kBlockFrames) * kChannels, 0.0);
    AudioGraphState state;
    state.trackStates.resize(graph.tracks.size());
    state.renderTracks.reserve(graph.tracks.size());

    for (const size_t orderedIndex : graph.topologicalOrder) {
        EXPECT_TRUE(orderedIndex < graph.tracks.size());
        if (orderedIndex >= graph.tracks.size()) {
            continue;
        }
        const auto& graphTrack = graph.tracks[orderedIndex];
        RenderTrack renderTrack;
        renderTrack.trackIndex = graphTrack.trackIndex;
        renderTrack.selfBuffer = trackBuffers[graphTrack.trackIndex].data();

        RuntimeConnection output;
        output.stride = 2;
        if (graphTrack.mainOutputId == kMasterId) {
            output.destinationBufferL = masterBuffer.data();
            output.destinationBufferR = masterBuffer.data() + 1;
        } else {
            const size_t destinationIndex = graphTrack.mainOutputId < graph.trackIndexById.size()
                                                ? graph.trackIndexById[graphTrack.mainOutputId]
                                                : AudioGraph::kInvalidTrackIndex;
            EXPECT_TRUE(destinationIndex != AudioGraph::kInvalidTrackIndex);
            if (destinationIndex == AudioGraph::kInvalidTrackIndex) {
                continue;
            }
            output.destinationBufferL = trackBuffers[destinationIndex].data();
            output.destinationBufferR = trackBuffers[destinationIndex].data() + 1;
        }
        renderTrack.activeConnections.push_back(output);
        state.renderTracks.push_back(std::move(renderTrack));
    }

    AudioRenderer renderer;
    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(totalFrames) * kChannels);
    for (uint64_t rendered = 0; rendered < totalFrames; rendered += kBlockFrames) {
        const uint32_t frames = static_cast<uint32_t>(std::min<uint64_t>(kBlockFrames, totalFrames - rendered));
        std::fill(masterBuffer.begin(), masterBuffer.end(), 0.0);
        AudioRenderer::Context context;
        context.masterBuffer = masterBuffer.data();
        context.numFrames = frames;
        context.bufferOffset = 0;
        context.globalPos = rendered;
        context.sampleRate = kSampleRate;
        context.graph = &graph;
        context.isOffline = true;
        renderer.renderBlock(context, state, engine);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            captured.push_back(static_cast<float>(masterBuffer[static_cast<size_t>(frame) * 2]));
            captured.push_back(static_cast<float>(masterBuffer[static_cast<size_t>(frame) * 2 + 1]));
        }
    }
    return captured;
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) {
        return 1e9;
    }
    double maxDiff = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    }
    return maxDiff;
}

double peakAbs(const std::vector<float>& v) {
    double peak = 0.0;
    for (float s : v) {
        peak = std::max(peak, std::abs(static_cast<double>(s)));
    }
    return peak;
}

// A pre-fader send reads the raw stereo source, so compare it directly with
// the balance-law reference rather than the source channel's pan stage.
void testPreFaderSendMatchesBalanceReference() {
    std::printf("[RoutingSendPanParityTest] pre-fader send matches stereo balance reference...\n");
    const float sweep[] = {-1.0f, -0.5f, 0.0f, 0.25f, 1.0f};
    for (float pan : sweep) {
        SendSpec preSend;
        preSend.enabled = true;
        preSend.gain = 1.0f;
        preSend.pan = pan;
        preSend.postFader = false;
        std::vector<float> sendOut = renderConfig(0.7f, preSend);

        EXPECT_TRUE(!sendOut.empty());
        EXPECT_TRUE(peakAbs(sendOut) > 1e-4);
        double gainL = 0.0;
        double gainR = 0.0;
        referenceBalanceGains(pan, 1.0, gainL, gainR);
        double maxDiff = 0.0;
        const uint64_t firstSample = static_cast<uint64_t>(kWarmupBlocks) * kBlockFrames;
        for (uint32_t i = 0; i < kMeasureBlocks * kBlockFrames; ++i) {
            const uint64_t n = firstSample + i;
            maxDiff = std::max(maxDiff, std::abs(static_cast<double>(sendOut[static_cast<size_t>(i) * 2]) -
                                                 static_cast<double>(sourceL(n)) * gainL));
            maxDiff = std::max(maxDiff, std::abs(static_cast<double>(sendOut[static_cast<size_t>(i) * 2 + 1]) -
                                                 static_cast<double>(sourceR(n)) * gainR));
        }
        if (maxDiff > 1e-6) {
            reportFailure("pre-fader send balance reference",
                          "pan " + std::to_string(pan) + " max abs diff " + std::to_string(maxDiff));
        }
    }
}

void testCentredPostFaderSendPreservesLevel() {
    std::printf("[RoutingSendPanParityTest] centred post-fader send preserves source level...\n");
    SendSpec none;
    const std::vector<float> direct = renderConfig(0.0f, none);

    SendSpec postSend;
    postSend.enabled = true;
    postSend.gain = 1.0f;
    postSend.pan = 0.0f;
    postSend.postFader = true;
    const std::vector<float> routed = renderConfig(0.0f, postSend);

    EXPECT_TRUE(!direct.empty() && !routed.empty());
    EXPECT_TRUE(peakAbs(direct) > 1e-4);
    const double diff = maxAbsDiff(direct, routed);
    if (diff > 1e-9) {
        reportFailure("centred post-fader send preserves level", "max abs diff " + std::to_string(diff));
    }
}

void testCentredMainRoutingPreservesLevelAcrossHops() {
    std::printf("[RoutingSendPanParityTest] centred main routing preserves level across hops...\n");
    const std::vector<float> direct = renderMainChain(0);
    EXPECT_TRUE(!direct.empty());
    EXPECT_TRUE(peakAbs(direct) > 1e-4);
    for (size_t destinations = 1; destinations <= 3; ++destinations) {
        const std::vector<float> routed = renderMainChain(destinations);
        const double diff = maxAbsDiff(direct, routed);
        if (diff > 1e-9) {
            reportFailure("centred main route hop parity",
                          std::to_string(destinations) + " destination(s), max abs diff " + std::to_string(diff));
        }
    }
}

void testOfflineRendererMatchesLiveCentredRouting() {
    std::printf("[RoutingSendPanParityTest] offline renderer matches live centred routing...\n");
    constexpr uint32_t totalBlocks = kWarmupBlocks + kMeasureBlocks;
    constexpr uint32_t totalFrames = totalBlocks * kBlockFrames;
    const size_t firstMeasuredSample = static_cast<size_t>(kWarmupBlocks) * kBlockFrames * kChannels;

    for (const size_t destinations : {size_t{0}, size_t{2}}) {
        const std::vector<float> live = renderClipMainChainLive(destinations, totalFrames);
        const std::vector<float> offline = renderClipMainChainOffline(destinations, totalFrames);
        EXPECT_TRUE(live.size() == offline.size());
        if (live.size() != offline.size() || live.size() <= firstMeasuredSample) {
            continue;
        }
        const std::vector<float> liveMeasured(live.begin() + static_cast<ptrdiff_t>(firstMeasuredSample), live.end());
        const std::vector<float> offlineMeasured(offline.begin() + static_cast<ptrdiff_t>(firstMeasuredSample),
                                                 offline.end());
        const double diff = maxAbsDiff(liveMeasured, offlineMeasured);
        if (diff > 1e-6) {
            reportFailure("offline renderer/live centred route parity",
                          std::to_string(destinations) + " destination(s), max abs diff " + std::to_string(diff));
        }
    }
}

// #262 acceptance: the engine's batched send loop must match a naive
// per-sample reference computed directly from the deterministic source.
void testBatchedSendMatchesPerSampleReference() {
    std::printf("[RoutingSendPanParityTest] batched send output matches per-sample reference...\n");
    const float sendPan = -0.6f;
    const float sendGain = 0.8f;

    SendSpec preSend;
    preSend.enabled = true;
    preSend.gain = sendGain;
    preSend.pan = sendPan;
    preSend.postFader = false;
    std::vector<float> sendOut = renderConfig(0.7f, preSend);
    EXPECT_TRUE(!sendOut.empty());
    EXPECT_TRUE(peakAbs(sendOut) > 1e-4);

    double gainL = 0.0;
    double gainR = 0.0;
    referenceBalanceGains(static_cast<double>(sendPan), static_cast<double>(sendGain), gainL, gainR);

    double maxDiff = 0.0;
    const uint64_t firstSample = static_cast<uint64_t>(kWarmupBlocks) * kBlockFrames;
    for (uint32_t i = 0; i < kMeasureBlocks * kBlockFrames; ++i) {
        const uint64_t n = firstSample + i;
        const double refL = static_cast<double>(sourceL(n)) * gainL;
        const double refR = static_cast<double>(sourceR(n)) * gainR;
        maxDiff = std::max(maxDiff, std::abs(static_cast<double>(sendOut[static_cast<size_t>(i) * 2]) - refL));
        maxDiff = std::max(maxDiff, std::abs(static_cast<double>(sendOut[static_cast<size_t>(i) * 2 + 1]) - refR));
    }
    if (maxDiff > 1e-6) {
        reportFailure("batched send == per-sample reference", "max abs diff " + std::to_string(maxDiff));
    }
}

} // namespace

int main() {
    std::printf("=== RoutingSendPanParityTest (routing BUG-3 #261, BUG-5 #262) ===\n");

    testPreFaderSendMatchesBalanceReference();
    testCentredPostFaderSendPreservesLevel();
    testCentredMainRoutingPreservesLevelAcrossHops();
    testOfflineRendererMatchesLiveCentredRouting();
    testBatchedSendMatchesPerSampleReference();

    if (g_failures == 0) {
        std::printf("=== RoutingSendPanParityTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== RoutingSendPanParityTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
