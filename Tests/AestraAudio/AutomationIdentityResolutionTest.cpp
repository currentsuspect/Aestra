// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Automation Identity Resolution (Contract I2/I3/I8/I10): the curve targets
// the INSTANCE, so reordering and swapping never retarget it, a removed
// target leaves the curve dangling (never re-pointed), and a placeholder
// target stays attached. All assertions are on rendered audio.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/AutomationCurve.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Aestra;
using namespace Aestra::Audio;

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 256;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kRenderFrames = static_cast<uint32_t>(1.0 * kSampleRate); // 1 s
constexpr uint32_t kSpbAt120 = kSampleRate * 60 / 120; // 24000 samples per beat

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

double rmsOf(const std::vector<float>& samples, size_t begin, size_t end) {
    double acc = 0.0;
    size_t n = 0;
    for (size_t i = begin; i < end && i < samples.size(); i += 2) {
        acc += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
        ++n;
    }
    return n > 0 ? std::sqrt(acc / static_cast<double>(n)) : 0.0;
}

// Generator on both channels (constant amplitude).
class GeneratorPlugin : public IPluginInstance {
public:
    GeneratorPlugin() {
        m_info.id = "aestra.test.identity.gen";
        m_info.name = "Gen";
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
    void process(const float* const* inputs, float** outputs, uint32_t, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* = nullptr, MidiBuffer* = nullptr) override {
        for (uint32_t c = 0; c < numOutputChannels; ++c) {
            if (!outputs[c]) {
                continue;
            }
            for (uint32_t i = 0; i < numFrames; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
                outputs[c][i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 220.0 * t) * 0.25);
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
protected:
    PluginInfo m_info{};
};

// Gain on param 0: setParameter(0, v) scales the output by v.
class GainPlugin : public GeneratorPlugin {
public:
    GainPlugin() { m_info.id = "aestra.test.identity.gain"; }
    void setParameter(uint32_t id, float value) override {
        if (id == 0) {
            m_gain = value;
        }
    }
    float getParameter(uint32_t id) const override { return id == 0 ? m_gain : 0.0f; }
    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* m = nullptr,
                 MidiBuffer* o = nullptr) override {
        GeneratorPlugin::process(inputs, outputs, numInputChannels, numOutputChannels, numFrames, m, o);
        for (uint32_t c = 0; c < numOutputChannels; ++c) {
            if (!outputs[c]) {
                continue;
            }
            for (uint32_t i = 0; i < numFrames; ++i) {
                outputs[c][i] *= m_gain;
            }
        }
    }

private:
    float m_gain = 1.0f;
};

// Inert: setParameter is a no-op. If automation were retargeted onto this
// plugin, the rendered output would NOT fade.
class InertPlugin : public GeneratorPlugin {
public:
    InertPlugin() { m_info.id = "aestra.test.identity.inert"; }
};

struct Fixture {
    std::shared_ptr<TrackManager> tm;
    MixerChannel* src{nullptr};
    AudioEngine engine;
    AutomationCurve curve;
    uint64_t gainId{0};
    uint64_t inertId{0};

    Fixture() {
        tm = std::make_shared<TrackManager>();
        tm->setOutputSampleRate(static_cast<double>(kSampleRate));
        src = tm->addChannel("src");
        require(src->getEffectChain().insertPlugin(0, std::make_shared<GeneratorPlugin>()), "generator inserted");
        require(src->getEffectChain().insertPlugin(1, std::make_shared<GainPlugin>()), "gain inserted");
        require(src->getEffectChain().insertPlugin(2, std::make_shared<InertPlugin>()), "inert inserted");
        gainId = src->getEffectChain().getSlotInstanceId(1);
        inertId = src->getEffectChain().getSlotInstanceId(2);

        // Curve: gain 1.0 for the first half, 0.0 for the second.
        curve = AutomationCurve("gain", AutomationTarget::Custom);
        curve.deviceInstanceId = gainId;
        curve.paramId = 0;
        curve.setDefaultValue(1.0f);
        curve.addPoint(0.0, 1.0f, kSpbAt120, 0.5f);
        curve.addPoint(0.5, 1.0f, kSpbAt120, 0.5f);
        curve.addPoint(1.0, 0.0f, kSpbAt120, 0.5f);

        auto& playlist = tm->getPlaylistModel();
        playlist.setProjectSampleRate(static_cast<double>(kSampleRate));
        playlist.setBPM(120.0);
        const auto laneId = playlist.createLane("lane0");
        auto* lane = playlist.getLane(laneId);
        require(lane != nullptr, "lane created");
        lane->automationCurves.push_back(curve);
        lane->automationCurves.back().mixerChannelId = src->getChannelId();
    }

    // Baseline render WITHOUT any automation: a flat constant tone rules out
    // global fades/mix artifacts before the identity cases are judged.
    void renderBaseline() {
        // Clear every lane's curves (the fixture created one lane with ours).
        auto& playlist = tm->getPlaylistModel();
        const auto laneIds = playlist.getLaneIDs();
        for (const auto& laneId : laneIds) {
            if (auto* lane = playlist.getLane(laneId)) {
                lane->automationCurves.clear();
            }
        }
        startEngine();
        std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
        std::vector<float> samples;
        for (uint32_t rendered = 0; rendered < kRenderFrames; rendered += kBlockFrames) {
            std::fill(block.begin(), block.end(), 0.0f);
            engine.processBlock(block.data(), nullptr, kBlockFrames,
                                static_cast<double>(rendered) / kSampleRate);
            samples.insert(samples.end(), block.begin(), block.end());
        }
        engine.setTransportPlaying(false);
        const double early = rmsOf(samples, 4096, 32000);
        const double late = rmsOf(samples, 72000, 92000);
        std::cout << "baseline: early=" << early << " late=" << late << "\n";
    }

    void startEngine() {
        engine.setTrackManager(tm);
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kBlockFrames, kChannels);
        tm->buildAndShareSlotMap();
        if (auto slotMap = tm->getChannelSlotMapShared()) {
            engine.setChannelSlotMap(slotMap);
        }
        engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
        engine.setSafetyLimiterEnabled(false);
        engine.setTransportPlaying(true);
    }

    // Render and compare early (gain 1.0) vs late (gain 0.0) RMS.
    void renderExpectFade(const std::string& tag) {
        std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(kRenderFrames) * kChannels);
        for (uint32_t rendered = 0; rendered < kRenderFrames; rendered += kBlockFrames) {
            std::fill(block.begin(), block.end(), 0.0f);
            engine.processBlock(block.data(), nullptr, kBlockFrames,
                                static_cast<double>(rendered) / kSampleRate);
            samples.insert(samples.end(), block.begin(), block.end());
        }
        engine.setTransportPlaying(false);
        const double early = rmsOf(samples, 4096, 32000);
        const double late = rmsOf(samples, 72000, 92000);
        std::cout << tag << ": early=" << early << " late=" << late << "\n";
        require(early > 1e-3, tag + ": early region is silent");
        require(late < 0.02 * early, tag + ": automation did not follow the instance");
    }

    void renderExpectNoFade(const std::string& tag) {
        std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(kRenderFrames) * kChannels);
        for (uint32_t rendered = 0; rendered < kRenderFrames; rendered += kBlockFrames) {
            std::fill(block.begin(), block.end(), 0.0f);
            engine.processBlock(block.data(), nullptr, kBlockFrames,
                                static_cast<double>(rendered) / kSampleRate);
            samples.insert(samples.end(), block.begin(), block.end());
        }
        engine.setTransportPlaying(false);
        const double early = rmsOf(samples, 4096, 24000);
        const double late = rmsOf(samples, 72000, 92000);
        std::cout << tag << ": early=" << early << " late=" << late << "\n";
        require(early > 1e-3, tag + ": early region is silent");
        require(late > 0.8 * early, tag + ": dangling curve must not retarget (output changed)");
    }
};

void testReorderDoesNotRetarget() {
    // Move the gain plugin from slot 1 to the empty slot 3. The curve targets
    // the instance, so it must still drive the gain and fade the output.
    Fixture fx;
    require(fx.src->getEffectChain().movePlugin(1, 3), "movePlugin to empty slot succeeds");
    require(fx.src->getEffectChain().getSlotInstanceId(3) == fx.gainId,
            "movePlugin carries the identity with the instance");
    fx.startEngine();
    fx.renderExpectFade("reorder");
}

void testSwapDoesNotRetarget() {
    // Exchange gain (slot 1) with inert (slot 2). Positional targeting would
    // now drive the inert plugin; instance targeting keeps driving the gain.
    Fixture fx;
    require(fx.src->getEffectChain().swapPlugins(1, 2), "swapPlugins succeeds");
    require(fx.src->getEffectChain().getSlotInstanceId(2) == fx.gainId,
            "swap carries the identity with the instance");
    fx.startEngine();
    fx.renderExpectFade("swap");
}

void testRemoveTargetLeavesCurveDangling() {
    // Remove the gain plugin. The curve keeps its (now dangling) instance id
    // and must not drive the inert plugin that now sits at slot 1.
    Fixture fx;
    require(fx.src->getEffectChain().removePlugin(1) != nullptr, "gain plugin removed");
    fx.startEngine();
    fx.renderExpectNoFade("remove");
    // The curve still names the removed instance — never re-pointed.
    require(fx.curve.deviceInstanceId == fx.gainId, "removed target keeps its identity");
}

void testPlaceholderTargetStaysAttached() {
    // A placeholder at slot 1 (unknown plugin restored from a crafted v2
    // blob). A curve on the placeholder's id must stay attached: the snapshot
    // resolves the target to the placeholder slot, and a save/load cycle
    // keeps it there. Resolution of a missing plugin is a skip, never a
    // retarget (covered by the remove case at render level).
    EffectChain chain;
    {
        std::vector<uint8_t> blob{'N', 'E', 'C', 2, static_cast<uint8_t>(EffectChain::MAX_SLOTS)};
        const auto put = [&blob](const void* data, size_t n) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            blob.insert(blob.end(), bytes, bytes + n);
        };
        for (size_t slot = 0; slot < EffectChain::MAX_SLOTS; ++slot) {
            const bool occupied = slot == 1;
            blob.push_back(occupied ? 1 : 0);
            if (!occupied) {
                continue;
            }
            const uint64_t wireId = 424242;
            put(&wireId, sizeof(wireId));
            const std::string missingId = "aestra.test.identity.missing";
            const uint32_t idLen = static_cast<uint32_t>(missingId.size());
            put(&idLen, sizeof(idLen));
            blob.insert(blob.end(), missingId.begin(), missingId.end());
            const uint8_t bypass = 0;
            blob.push_back(bypass);
            const float dryWet = 1.0f;
            put(&dryWet, sizeof(dryWet));
            const uint32_t stateLen = 0;
            put(&stateLen, sizeof(stateLen));
        }
        std::vector<std::string> missing;
        require(chain.loadState(blob, PluginManager::getInstance(), &missing),
                "placeholder chain loads");
        require(missing.size() == 1, "one placeholder created");
    }
    require(chain.getSlotInstanceId(1) == 424242, "placeholder restored the wire identity");

    AutomationCurve curve("gain", AutomationTarget::Custom);
    curve.deviceInstanceId = 424242;
    curve.paramId = 0;
    const auto snapshot = chain.getSnapshot();
    require(snapshot != nullptr, "snapshot published");
    require(snapshot->findSlotByInstanceId(curve.deviceInstanceId) == 1,
            "the curve's target resolves to the placeholder slot");

    // Save/load keeps the placeholder and its identity; the curve stays attached.
    EffectChain reloaded;
    std::vector<std::string> missing2;
    require(reloaded.loadState(chain.saveState(), PluginManager::getInstance(), &missing2),
            "resaved placeholder chain loads");
    require(reloaded.getSlotInstanceId(1) == 424242, "placeholder identity survived the round trip");
    const auto snapshot2 = reloaded.getSnapshot();
    require(snapshot2->findSlotByInstanceId(curve.deviceInstanceId) == 1,
            "the curve's target survives the round trip");
}

} // namespace

int main() {
    if (!PluginManager::getInstance().initialize()) {
        std::cerr << "FAIL: plugin manager initialize\n";
        return 1;
    }
    {
        Fixture fx;
        fx.renderBaseline();
    }
    testReorderDoesNotRetarget();
    testSwapDoesNotRetarget();
    testRemoveTargetLeavesCurveDangling();
    testPlaceholderTargetStaysAttached();
    std::cout << "[PASS] AutomationIdentityResolutionTest\n";
    return 0;
}
