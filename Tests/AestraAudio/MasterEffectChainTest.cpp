// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MasterEffectChainTest — regression for "Master channel does not accept
// plugins" (triage 2026-08-14). The Master strip is now a plugin host like
// any other mixer channel:
//   * TrackManager owns a Master MixerChannel (id 0) outside the routable
//     channel list (terminal-sink contract preserved).
//   * Its chain snapshot rides AudioGraph::masterEffectChainSnapshot and is
//     processed on the summed master buffer before the master fader and the
//     safety limiter.
//   * ProjectSerializer persists/restores the master chain ("master" node).
//
// Covers: insert + audible processing on Master, bypass transparency,
// remove restores the baseline, graph snapshot publication, and a
// save/load roundtrip (plus old-project compat: a file without a master
// node must load with an empty Master chain).

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/InternalPluginRegistry.h"
#include "Plugin/PluginManager.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 256;
constexpr uint32_t kChannels = 2;

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

// Constant-amplitude sine on both channels: gain changes read directly as
// RMS ratios.
class SineGeneratorPlugin : public IPluginInstance {
public:
    SineGeneratorPlugin() {
        m_info.id = "aestra.test.master_chain_generator";
        m_info.name = "MasterChainGenerator";
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
            // 1000 Hz at 48 kHz = exactly 48 samples/period, so any RMS window
            // that is a multiple of 48 frames is phase-independent.
            const double phase =
                2.0 * 3.14159265358979 * 1000.0 * (static_cast<double>(m_sampleIndex + k) / kSampleRate);
            const float s = 0.25f * static_cast<float>(std::sin(phase));
            outputs[0][k] = s;
            outputs[1][k] = s;
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

// Gain plugin whose single parameter (id 0) scales the signal.
class GainParamPlugin : public IPluginInstance {
public:
    GainParamPlugin() {
        m_info.id = "aestra.test.master_chain_gain";
        m_info.name = "MasterChainGain";
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
        const float g = m_gain.load(std::memory_order_relaxed);
        for (uint32_t k = 0; k < numFrames; ++k) {
            outputs[0][k] *= g;
            outputs[1][k] *= g;
        }
    }
    std::vector<PluginParameter> getParameters() const override { return {{0, "Gain", "Gn", "", 1.0f, 0.0f, 1.0f}}; }
    uint32_t getParameterCount() const override { return 1; }
    float getParameter(uint32_t id) const override { return id == 0 ? m_gain.load(std::memory_order_relaxed) : 0.0f; }
    void setParameter(uint32_t id, float value) override {
        if (id == 0) {
            m_gain.store(value, std::memory_order_relaxed);
        }
    }
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
    std::atomic<float> m_gain{1.0f};
    bool m_active{false};
};

struct Session {
    std::shared_ptr<TrackManager> tm;
    std::shared_ptr<GainParamPlugin> masterGain;
};

Session buildSession() {
    Session s;
    s.tm = std::make_shared<TrackManager>();
    s.tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    MixerChannel* src = s.tm->addChannel("src");
    require(src != nullptr, "addChannel failed");
    require(src->getEffectChain().insertPlugin(0, std::make_shared<SineGeneratorPlugin>()), "generator insert failed");
    return s;
}

std::vector<float> render(const std::shared_ptr<TrackManager>& tm, uint32_t blocks) {
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

    std::vector<float> out;
    out.reserve(static_cast<size_t>(blocks) * kBlockFrames * kChannels);
    std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    for (uint32_t b = 0; b < blocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, kBlockFrames, static_cast<double>(b * kBlockFrames) / kSampleRate);
        out.insert(out.end(), block.begin(), block.end());
    }
    engine.setTransportPlaying(false);
    return out;
}

double rmsRange(const std::vector<float>& interleaved, size_t firstFrame, size_t endFrame, uint32_t channels) {
    if (channels == 0 || firstFrame >= endFrame || endFrame * channels > interleaved.size()) {
        return 0.0;
    }
    double sumSq = 0.0;
    const size_t firstSample = firstFrame * channels;
    const size_t endSample = endFrame * channels;
    for (size_t i = firstSample; i < endSample; ++i) {
        sumSq += static_cast<double>(interleaved[i]) * static_cast<double>(interleaved[i]);
    }
    return std::sqrt(sumSq / static_cast<double>(endSample - firstSample));
}

void testMasterChainProcessing() {
    std::cout << "[MasterEffectChainTest] master chain processing...\n";
    constexpr uint32_t kWarmup = 8;
    constexpr uint32_t kMeasure = 4;

    // Baseline: no master plugins.
    Session base = buildSession();
    const std::vector<float> baseline = render(base.tm, kWarmup + kMeasure);
    const size_t first = static_cast<size_t>(kWarmup) * kBlockFrames;
    const size_t last = baseline.size() / kChannels - 256;
    const double baseRms = rmsRange(baseline, first, last, kChannels);
    require(baseRms > 1e-4, "baseline is silent");

    // Master chain with a unity-gain plugin must be transparent.
    Session unity = buildSession();
    auto* masterChannel = unity.tm->getMasterChannel();
    require(masterChannel != nullptr, "getMasterChannel() returned null");
    unity.masterGain = std::make_shared<GainParamPlugin>();
    require(masterChannel->getEffectChain().insertPlugin(0, unity.masterGain), "master insertPlugin failed");
    {
        AudioGraph graph = AudioGraphBuilder::buildFromTrackManager(*unity.tm);
        require(graph.masterEffectChainSnapshot != nullptr, "master snapshot missing from graph");
        require(graph.masterEffectChainSnapshot->getActiveSlotCount() >= 1,
                "master snapshot has no active slots after insert");
    }
    const std::vector<float> unityOut = render(unity.tm, kWarmup + kMeasure);
    const double unityRms = rmsRange(unityOut, first, last, kChannels);
    require(std::abs(unityRms - baseRms) < 1e-3 * baseRms, "unity master plugin changed the level");

    // Gain 0.5 on the master chain halves the level.
    unity.masterGain->setParameter(0, 0.5f);
    const std::vector<float> halfOut = render(unity.tm, kWarmup + kMeasure);
    const double halfRms = rmsRange(halfOut, first, last, kChannels);
    require(std::abs(halfRms - 0.5 * baseRms) < 0.02 * baseRms, "master chain gain 0.5 did not halve the level");

    // Bypass restores the baseline exactly.
    masterChannel->getEffectChain().setSlotBypassed(0, true);
    const std::vector<float> bypassOut = render(unity.tm, kWarmup + kMeasure);
    const double bypassRms = rmsRange(bypassOut, first, last, kChannels);
    require(std::abs(bypassRms - baseRms) < 1e-3 * baseRms, "bypassed master plugin changed the level");

    // Remove restores the baseline exactly.
    masterChannel->getEffectChain().setSlotBypassed(0, false);
    require(masterChannel->getEffectChain().removePlugin(0) != nullptr, "master removePlugin failed");
    const std::vector<float> removedOut = render(unity.tm, kWarmup + kMeasure);
    const double removedRms = rmsRange(removedOut, first, last, kChannels);
    require(std::abs(removedRms - baseRms) < 1e-3 * baseRms, "master chain not empty after remove");

    std::cout << "  baseline=" << baseRms << " unity=" << unityRms << " half=" << halfRms
              << " bypass=" << bypassRms << " removed=" << removedRms << "\n";
}

void testMasterChainPersistence() {
    std::cout << "[MasterEffectChainTest] master chain save/load roundtrip...\n";
    // Use a real built-in plugin (available in core mode, resolves through the
    // scanner) so loadState can restore the chain end to end.
    auto& pluginManager = PluginManager::getInstance();
    require(pluginManager.initialize(), "PluginManager failed to initialize");
    auto eq = pluginManager.createInstanceById("com.Aestrastudios.eq");
    require(eq != nullptr, "failed to create built-in EQ instance");
    require(eq->initialize(static_cast<double>(kSampleRate), kBlockFrames), "EQ initialize failed");
    const float kProbeParam = 0.75f;
    if (eq->getParameterCount() > 0) {
        eq->setParameter(0, kProbeParam);
    }
    eq->activate();

    const Aestra::Tests::ScopedTempDirectory tempDirScope{"MasterEffectChain"};
    const auto projectPath = tempDirScope.path() / "master-chain-project.aes";

    auto tm1 = std::make_shared<TrackManager>();
    tm1->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm1->addChannel("src");
    auto* master1 = tm1->getMasterChannel();
    require(master1 != nullptr, "getMasterChannel() returned null (save arm)");
    require(master1->getEffectChain().insertPlugin(0, eq), "master insertPlugin failed (save arm)");
    require(ProjectSerializer::save(projectPath.string(), tm1, 120.0, 1.0), "save failed");

    auto tm2 = std::make_shared<TrackManager>();
    ProjectSerializer::LoadResult loadResult = ProjectSerializer::load(projectPath.string(), tm2);
    require(loadResult.ok, "load failed");
    auto* master2 = tm2->getMasterChannel();
    require(master2 != nullptr, "getMasterChannel() returned null (load arm)");
    require(master2->getEffectChain().getActiveSlotCount() == 1, "master chain slot count not restored");
    require(master2->getEffectChain().getPlugin(0) != nullptr, "master slot 0 plugin missing after load");
    require(master2->getEffectChain().getPlugin(0)->getInfo().id == "com.Aestrastudios.eq",
            "master slot 0 plugin id mismatch after load");
    if (master2->getEffectChain().getPlugin(0)->getParameterCount() > 0) {
        const float restored = master2->getEffectChain().getPlugin(0)->getParameter(0);
        require(std::abs(restored - kProbeParam) < 1e-4f, "master slot 0 parameter state not restored");
    }

    // Old-project compat: a project without a "master" node must load with an
    // empty Master chain and no error.
    auto tm3 = std::make_shared<TrackManager>();
    const auto legacyPath = tempDirScope.path() / "no-master-node.aes";
    {
        // Minimal v3 project written by hand (no master node).
        const std::string legacy =
            "{\"version\":3,\"tempo\":120.0,\"mixerChannels\":[{\"id\":1,\"name\":\"Ch\",\"volume\":1.0,\"pan\":0.0,"
            "\"mute\":false,\"solo\":false}],\"lanes\":[],\"patterns\":[],\"sources\":[],\"arsenal\":{\"units\":[]}}";
        std::ofstream f(legacyPath.string());
        f << legacy;
    }
    ProjectSerializer::LoadResult legacyResult = ProjectSerializer::load(legacyPath.string(), tm3);
    require(legacyResult.ok, "legacy (no master node) load failed");
    require(tm3->getMasterChannel() != nullptr, "getMasterChannel() null on legacy load");
    require(tm3->getMasterChannel()->getEffectChain().getActiveSlotCount() == 0,
            "legacy load must leave the master chain empty");
}

} // namespace

int main() {
    std::cout << "=== MasterEffectChainTest (master-as-plugin-host triage 2026-08-14) ===\n";
    testMasterChainProcessing();
    testMasterChainPersistence();

    if (g_failures == 0) {
        std::cout << "=== MasterEffectChainTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== MasterEffectChainTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}
