// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RoutingSendPanParityTest — acceptance tests for routing BUG-3 (#261) and BUG-5 (#262).
//
// #261: Send pan must use the same pan law as main mixer pan.
//   * A pre-fader send at pan P, unity gain, must produce sample-identical
//     master output to routing the track's main output at pan P.
//   * A post-fader send stacks the source's main pan stage on top; with the
//     source panned center the send output must equal the direct path scaled
//     by exactly the center gain of the shared law (cos(π/4)).
//
// #262: Batched send processing must match a naive per-sample reference.
//   * The test recomputes the expected send output sample-by-sample from the
//     known deterministic source signal and the documented constant-power law,
//     then compares against the engine's block-batched send path.
//
// Both tests drive the real RT path (AudioEngine::processBlock) with a
// deterministic generator plugin as the signal source — no clips, no audio
// hardware, no file I/O on the verification path.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"

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

#define EXPECT_TRUE(expr)                                                                                              \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            reportFailure(#expr, std::string(__FILE__) + ":" + std::to_string(__LINE__));                              \
        }                                                                                                              \
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

// Replicates the engine's shared pan law (fastPanGainsD in AudioEngine.cpp)
// including its float intermediate, so reference gains match to double
// precision. Any future divergence between main/send law breaks the tests.
void referencePanGains(double pan, double vol, double& gainL, double& gainR) {
    float p = (static_cast<float>(pan) + 1.0f) * 0.5f;
    gainL = static_cast<double>(std::cos(p * 1.57079632679f)) * vol;
    gainR = static_cast<double>(std::sin(p * 1.57079632679f)) * vol;
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
    EXPECT_TRUE(src != nullptr);
    EXPECT_TRUE(sink != nullptr);
    if (!src || !sink) {
        return {};
    }

    sink->setMute(true);
    src->setPan(mainPan);
    EXPECT_TRUE(src->getEffectChain().insertPlugin(0, std::make_shared<GeneratorPlugin>()));

    if (send.enabled) {
        src->setMainOutputId(sink->getChannelId()); // Keep the direct path out of the master mix
        AudioRoute route;
        route.targetChannelId = kMasterId;
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
        engine.processBlock(block.data(), nullptr, kBlockFrames,
                            static_cast<double>(b * kBlockFrames) / kSampleRate);
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

// #261 acceptance: a pre-fader unity send at pan P must be sample-identical to
// the main path at pan P — same taps, same law, same smoothers.
void testPreFaderSendMatchesMainPan() {
    std::printf("[RoutingSendPanParityTest] pre-fader send pan matches main pan across sweep...\n");
    // Extremes, center, one symmetric pair, one asymmetric point. Each pan
    // value costs two full engine fixtures (~1.6s), so keep the sweep lean.
    const float sweep[] = {-1.0f, -0.5f, 0.0f, 0.25f, 1.0f};
    for (float pan : sweep) {
        SendSpec none;
        std::vector<float> mainOut = renderConfig(pan, none);

        SendSpec preSend;
        preSend.enabled = true;
        preSend.gain = 1.0f;
        preSend.pan = pan;
        preSend.postFader = false;
        // Deliberately different main pan on the send config: the pre-fader
        // send must not be affected by the source's own pan stage.
        std::vector<float> sendOut = renderConfig(0.7f, preSend);

        EXPECT_TRUE(!mainOut.empty() && !sendOut.empty());
        EXPECT_TRUE(peakAbs(mainOut) > 1e-4); // Signal actually flowed
        const double diff = maxAbsDiff(mainOut, sendOut);
        if (diff > 1e-9) {
            reportFailure("pre-fader send == main pan",
                          "pan " + std::to_string(pan) + " max abs diff " + std::to_string(diff));
        }
    }
}

// #261 acceptance (post-fader): with the source panned center, a post-fader
// unity send at pan P equals the direct path at pan P scaled by exactly the
// shared law's center gain. Any law mismatch changes that scale factor.
void testPostFaderSendStacksSameLaw() {
    std::printf("[RoutingSendPanParityTest] post-fader send stacks the same law on top of center main pan...\n");
    double centerL = 0.0;
    double centerR = 0.0;
    referencePanGains(0.0, 1.0, centerL, centerR);

    const float sweep[] = {-0.5f, 0.25f};
    for (float pan : sweep) {
        SendSpec none;
        std::vector<float> mainOut = renderConfig(pan, none);

        SendSpec postSend;
        postSend.enabled = true;
        postSend.gain = 1.0f;
        postSend.pan = pan;
        postSend.postFader = true;
        std::vector<float> sendOut = renderConfig(0.0f, postSend);

        EXPECT_TRUE(!mainOut.empty() && !sendOut.empty());
        EXPECT_TRUE(peakAbs(mainOut) > 1e-4);

        double maxDiff = 0.0;
        for (size_t i = 0; i + 1 < mainOut.size(); i += 2) {
            const double expectL = static_cast<double>(mainOut[i]) * centerL;
            const double expectR = static_cast<double>(mainOut[i + 1]) * centerR;
            maxDiff = std::max(maxDiff, std::abs(static_cast<double>(sendOut[i]) - expectL));
            maxDiff = std::max(maxDiff, std::abs(static_cast<double>(sendOut[i + 1]) - expectR));
        }
        if (maxDiff > 1e-6) {
            reportFailure("post-fader send == main pan * center gain",
                          "pan " + std::to_string(pan) + " max abs diff " + std::to_string(maxDiff));
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
    referencePanGains(static_cast<double>(sendPan), static_cast<double>(sendGain), gainL, gainR);

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

    testPreFaderSendMatchesMainPan();
    testPostFaderSendStacksSameLaw();
    testBatchedSendMatchesPerSampleReference();

    if (g_failures == 0) {
        std::printf("=== RoutingSendPanParityTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== RoutingSendPanParityTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
