// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Automation Lifecycle Test (Automation Identity Contract I1-I11):
//
//   create -> reorder -> automate -> save -> load -> reorder -> undo -> redo
//
// with (trackId, deviceInstanceId, parameterId) asserted at every stage, plus
// a rendered-output check after the whole cycle. Both structural identity and
// rendered audio are verified so a false failure localizes to one level.

#include "Commands/EffectCommands.h"
#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/AutomationCurve.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/EffectChain.h"
#include "Plugin/InternalPluginRegistry.h"
#include "Plugin/PluginHost.h"
#include "Plugin/PluginManager.h"
#include "../../Source/Core/ProjectSerializer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 256;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kRenderFrames = static_cast<uint32_t>(1.0 * kSampleRate);
constexpr uint32_t kSpbAt120 = kSampleRate * 60 / 120;

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

// --- registered test plugins: project load restores them LIVE --------------

class LifecycleGeneratorPlugin : public IPluginInstance {
public:
    LifecycleGeneratorPlugin() {
        m_info.id = "aestra.test.lifecycle.gen";
        m_info.name = "LifecycleGen";
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

// Gain on param 0.
class LifecycleGainPlugin : public LifecycleGeneratorPlugin {
public:
    LifecycleGainPlugin() { m_info.id = "aestra.test.lifecycle.gain"; }
    void setParameter(uint32_t id, float value) override {
        if (id == 0) {
            m_gain = value;
        }
    }
    float getParameter(uint32_t id) const override { return id == 0 ? m_gain : 0.0f; }
    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* m = nullptr,
                 MidiBuffer* o = nullptr) override {
        LifecycleGeneratorPlugin::process(inputs, outputs, numInputChannels, numOutputChannels, numFrames, m, o);
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

class LifecycleInertPlugin : public LifecycleGeneratorPlugin {
public:
    LifecycleInertPlugin() { m_info.id = "aestra.test.lifecycle.inert"; }
};

void registerTestPlugins() {
    static bool s_registered = false;
    if (s_registered) {
        return;
    }
    auto& registry = InternalPluginRegistry::instance();
    registry.registerPlugin(InternalPluginRegistry::Registration{
        LifecycleGeneratorPlugin{}.getInfo(),
        [] { return std::make_shared<LifecycleGeneratorPlugin>(); },
        [] { return true; },
    });
    registry.registerPlugin(InternalPluginRegistry::Registration{
        LifecycleGainPlugin{}.getInfo(),
        [] { return std::make_shared<LifecycleGainPlugin>(); },
        [] { return true; },
    });
    registry.registerPlugin(InternalPluginRegistry::Registration{
        LifecycleInertPlugin{}.getInfo(),
        [] { return std::make_shared<LifecycleInertPlugin>(); },
        [] { return true; },
    });
    s_registered = true;
}

// --- structural helpers ------------------------------------------------------

// Slot order as a vector of plugin ids (empty slots skipped).
std::vector<std::string> chainOrder(EffectChain& chain) {
    std::vector<std::string> order;
    for (size_t i = 0; i < EffectChain::MAX_SLOTS; ++i) {
        const auto* slot = chain.getSlot(i);
        if (slot && slot->plugin) {
            order.push_back(slot->plugin->getInfo().id);
        }
    }
    return order;
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) {
        out += s + ",";
    }
    return out;
}

uint64_t instanceIdAt(EffectChain& chain, const std::string& pluginId) {
    for (size_t i = 0; i < EffectChain::MAX_SLOTS; ++i) {
        const auto* slot = chain.getSlot(i);
        if (slot && slot->plugin && slot->plugin->getInfo().id == pluginId) {
            return chain.getSlotInstanceId(i);
        }
    }
    return 0;
}

// --- render helper (interleaved windows: sample index = 2 * position) --------

struct Rendered {
    double early = 0.0;
    double late = 0.0;
};

Rendered renderFadeCheck(std::shared_ptr<TrackManager> tm) {
    AudioEngine engine;
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

    std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(kRenderFrames) * kChannels);
    for (uint32_t rendered = 0; rendered < kRenderFrames; rendered += kBlockFrames) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, kBlockFrames, static_cast<double>(rendered) / kSampleRate);
        samples.insert(samples.end(), block.begin(), block.end());
    }
    engine.setTransportPlaying(false);
    // Curve drops to 0 at beat 1.0 = position 24000 = sample index 48000.
    Rendered out;
    out.early = rmsOf(samples, 4096, 32000);
    out.late = rmsOf(samples, 72000, 92000);
    return out;
}

} // namespace

int main() {
    if (!PluginManager::getInstance().initialize()) {
        std::cerr << "FAIL: plugin manager initialize\n";
        return 1;
    }
    registerTestPlugins();

    const std::string kGen = "aestra.test.lifecycle.gen";
    const std::string kGain = "aestra.test.lifecycle.gain";
    const std::string kInert = "aestra.test.lifecycle.inert";

    // =========================================================================
    // Stage 1: create — channel + generator/gain/inert; capture identities.
    // =========================================================================
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    auto& playlist = tm->getPlaylistModel();
    playlist.setProjectSampleRate(static_cast<double>(kSampleRate));
    playlist.setBPM(120.0);
    auto* channel = tm->addChannelWithId("Lifecycle", 41);
    require(channel != nullptr, "channel created");
    auto& chain = channel->getEffectChain();
    require(chain.insertPlugin(0, std::make_shared<LifecycleGeneratorPlugin>()), "gen inserted");
    require(chain.insertPlugin(1, std::make_shared<LifecycleGainPlugin>()), "gain inserted");
    require(chain.insertPlugin(2, std::make_shared<LifecycleInertPlugin>()), "inert inserted");
    const uint64_t genId = instanceIdAt(chain, kGen);
    const uint64_t gainId = instanceIdAt(chain, kGain);
    const uint64_t inertId = instanceIdAt(chain, kInert);
    require(genId != 0 && gainId != 0 && inertId != 0, "stage1: identities minted");
    require(genId != gainId && gainId != inertId, "stage1: identities distinct");
    require(join(chainOrder(chain)) == kGen + "," + kGain + "," + kInert + ",",
            "stage1: initial order gen,gain,inert");

    // =========================================================================
    // Stage 2: reorder (move to empty) via the command seam.
    // =========================================================================
    auto& history = tm->getCommandHistory();
    history.pushAndExecute(
        std::make_shared<MovePluginCommand>(*tm, *channel, 1, 3)); // gain 1 -> 3 (empty)
    require(join(chainOrder(chain)) == kGen + "," + kInert + "," + kGain + ",",
            "stage2: order after move-to-empty is gen,inert,gain");
    require(instanceIdAt(chain, kGain) == gainId, "stage2: gain identity unchanged by move");

    // =========================================================================
    // Stage 3: automate — curve targets (trackId, deviceInstanceId, parameterId).
    // =========================================================================
    const auto laneId = playlist.createLane("Lane 1");
    auto* lane = playlist.getLane(laneId);
    require(lane != nullptr, "lane created");
    AutomationCurve curve("gain", AutomationTarget::Custom);
    curve.deviceInstanceId = gainId;
    curve.paramId = 0;
    curve.setDefaultValue(1.0f);
    curve.addPoint(0.0, 1.0f, kSpbAt120, 0.5f);
    curve.addPoint(0.5, 1.0f, kSpbAt120, 0.5f);
    curve.addPoint(1.0, 0.0f, kSpbAt120, 0.5f);
    lane->automationCurves.push_back(curve);
    lane->automationCurves.back().mixerChannelId = 41;

    // =========================================================================
    // Stage 4: save -> load. (trackId, deviceInstanceId, parameterId) must be
    // exact; the chain restores live (registered) plugins with their ids.
    // =========================================================================
    const auto tempRoot = std::filesystem::temp_directory_path() /
                          ("aestra_automation_lifecycle_" + std::to_string(::getpid()));
    std::filesystem::create_directories(tempRoot);
    const auto projectPath = tempRoot / "lifecycle.aes";
    require(ProjectSerializer::save(projectPath.string(), tm, 120.0, 0.0), "stage4: save");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->setOutputSampleRate(static_cast<double>(kSampleRate));
    auto& playlist2 = tm2->getPlaylistModel();
    playlist2.setProjectSampleRate(static_cast<double>(kSampleRate));
    playlist2.setBPM(120.0);
    auto result = ProjectSerializer::load(projectPath.string(), tm2);
    require(result.ok, "stage4: load");

    auto* channel2 = tm2->getChannelById(41);
    require(channel2 != nullptr, "stage4: channel restored (trackId)");
    auto& chain2 = channel2->getEffectChain();
    require(join(chainOrder(chain2)) == kGen + "," + kInert + "," + kGain + ",",
            "stage4: chain order survives the round trip");
    require(instanceIdAt(chain2, kGen) == genId && instanceIdAt(chain2, kGain) == gainId &&
                instanceIdAt(chain2, kInert) == inertId,
            "stage4: every instance identity survives the round trip exactly");
    const auto laneIds2 = playlist2.getLaneIDs();
    require(!laneIds2.empty(), "stage4: lane restored");
    auto* lane2 = playlist2.getLane(laneIds2[0]);
    require(lane2 != nullptr && lane2->automationCurves.size() == 1, "stage4: curve restored");
    const auto& curve2 = lane2->automationCurves[0];
    require(curve2.mixerChannelId == 41, "stage4: trackId exact");
    require(curve2.deviceInstanceId == gainId, "stage4: deviceInstanceId exact");
    require(curve2.paramId == 0, "stage4: parameterId exact");

    // =========================================================================
    // Stage 5: reorder the LOADED project (swap path: 3 <-> 1 via occupied move).
    // =========================================================================
    auto& history2 = tm2->getCommandHistory();
    history2.pushAndExecute(
        std::make_shared<MovePluginCommand>(*tm2, *channel2, 3, 1)); // gain 3 -> 1 (empty now)
    require(join(chainOrder(chain2)) == kGen + "," + kGain + "," + kInert + ",",
            "stage5: order after move-back is gen,gain,inert");
    require(instanceIdAt(chain2, kGain) == gainId, "stage5: identity follows the instance");

    history2.pushAndExecute(
        std::make_shared<MovePluginCommand>(*tm2, *channel2, 1, 2)); // occupied -> swap
    require(join(chainOrder(chain2)) == kGen + "," + kInert + "," + kGain + ",",
            "stage5: swap gives gen,inert,gain");
    require(instanceIdAt(chain2, kGain) == gainId && instanceIdAt(chain2, kInert) == inertId,
            "stage5: swap carries identities with the instances");

    // =========================================================================
    // Stage 6: undo restores the EXACT pre-operation ordering and identities.
    // =========================================================================
    history2.undo(); // undo the swap
    require(join(chainOrder(chain2)) == kGen + "," + kGain + "," + kInert + ",",
            "stage6: undo of swap restores gen,gain,inert");
    require(instanceIdAt(chain2, kGain) == gainId && instanceIdAt(chain2, kInert) == inertId,
            "stage6: undo restores every identity");
    history2.undo(); // undo the move-back
    require(join(chainOrder(chain2)) == kGen + "," + kInert + "," + kGain + ",",
            "stage6: undo of move restores gen,inert,gain");

    // =========================================================================
    // Stage 7: redo reapplies the exact post-operation ordering and identities.
    // =========================================================================
    history2.redo();
    require(join(chainOrder(chain2)) == kGen + "," + kGain + "," + kInert + ",",
            "stage7: redo of move reapplies gen,gain,inert");
    history2.redo();
    require(join(chainOrder(chain2)) == kGen + "," + kInert + "," + kGain + ",",
            "stage7: redo of swap reapplies gen,inert,gain");
    require(instanceIdAt(chain2, kGain) == gainId && instanceIdAt(chain2, kInert) == inertId &&
                instanceIdAt(chain2, kGen) == genId,
            "stage7: redo leaves every identity untouched");

    // =========================================================================
    // Stage 8: a rejected move leaves chain AND history untouched.
    // =========================================================================
    const auto orderBeforeReject = chainOrder(chain2);
    history2.pushAndExecute(std::make_shared<MovePluginCommand>(*tm2, *channel2, 2, 2));
    require(join(chainOrder(chain2)) == join(orderBeforeReject),
            "stage8: rejected move leaves the chain untouched");
    history2.undo(); // must undo the last GOOD command (the swap), not a phantom
    require(join(chainOrder(chain2)) == kGen + "," + kGain + "," + kInert + ",",
            "stage8: rejected move recorded no undo entry");
    history2.redo();
    require(join(chainOrder(chain2)) == kGen + "," + kInert + "," + kGain + ",",
            "stage8: redo returns to the pre-reject state");

    // =========================================================================
    // Stage 9: rendered output — after the whole lifecycle the curve still
    // drives the GAIN instance (fade follows identity, not position).
    // =========================================================================
    const Rendered rendered = renderFadeCheck(tm2);
    std::cout << "render: early=" << rendered.early << " late=" << rendered.late << "\n";
    require(rendered.early > 1e-3, "stage9: early region is silent");
    require(rendered.late < 0.02 * rendered.early,
            "stage9: automation still drives the instance after the full lifecycle");

    std::cout << "[PASS] AutomationLifecycleTest\n";
    return 0;
}
