// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// RtMisusePerEngineTest (#885)
// ─────────────────────────────────────────────────────────────────────────────
// A non-real-time API reached from engine A's audio thread must be counted on
// engine A and nowhere else. The misuse is committed from a plugin's process()
// running inside the engine's real processBlock path, so the test exercises
// the same attribution route a real violation takes.
//
//   1. Two live engines: a violation under A is on A only; B stays at zero.
//   2. The reverse, with B constructed last (the old code credited whichever
//      engine was constructed last, so A-only would have been misattributed).
//   3. A fresh engine starts at zero after a previous one is destroyed.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "RealtimeThreadGuard.h"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr uint32_t kChannels = 2;

// Effect that, when armed, calls a non-real-time API's guard from process().
class MisusingPlugin final : public IPluginInstance {
public:
    MisusingPlugin() {
        m_info.id = "aestra.test.rtmisuse";
        m_info.name = "RtMisuse";
        m_info.vendor = "Aestra Test";
        m_info.version = "1.0";
        m_info.category = "Test";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 2;
        m_info.numAudioOutputs = 2;
    }
    void arm(bool on) { m_armed = on; }

    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override {}
    void deactivate() override {}
    bool isActive() const override { return true; }
    void process(const float* const* inputs, float** outputs, uint32_t, uint32_t numOut, uint32_t frames,
                 const MidiBuffer*, MidiBuffer*) override {
        if (m_armed) {
            (void)reportRealtimeMisuse("RtMisusePerEngineTest::plugin");
        }
        for (uint32_t c = 0; c < numOut; ++c) {
            if (!outputs[c])
                continue;
            for (uint32_t i = 0; i < frames; ++i)
                outputs[c][i] = (inputs && inputs[c]) ? inputs[c][i] : 0.0f;
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
    PluginInfo m_info{};
    bool m_armed = false;
};

struct Rig {
    std::shared_ptr<TrackManager> tm = std::make_shared<TrackManager>();
    AudioEngine engine;
    std::shared_ptr<MisusingPlugin> plugin = std::make_shared<MisusingPlugin>();

    Rig() {
        tm->setOutputSampleRate(kSampleRate);
        CHECK(tm->addChannel("src") != nullptr);
        engine.setTrackManager(tm);
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kFrames, kChannels);
        const bool inserted = tm->getMasterChannel()->getEffectChain().insertPlugin(0, plugin);
        CHECK(inserted);
        tm->buildAndShareSlotMap();
        if (auto slotMap = tm->getChannelSlotMapShared()) {
            engine.setChannelSlotMap(slotMap);
        }
        engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
        CHECK(engine.initialize());
        engine.setTransportPlaying(true);
    }

    void block() {
        std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f);
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    }
    uint64_t misuse() { return engine.telemetry().getRtMisuseViolations(); }
};

} // namespace

int main() {
    {
        Rig a;
        Rig b; // constructed last: the old process-wide pointer targeted this one
        a.plugin->arm(true);
        a.block();
        a.block();
        b.block();
        CHECK(a.misuse() == 2);
        CHECK(b.misuse() == 0);

        a.plugin->arm(false);
        b.plugin->arm(true);
        b.block();
        CHECK(a.misuse() == 2);
        CHECK(b.misuse() == 1);
    }
    {
        // Counters do not outlive the engine that produced them.
        {
            Rig first;
            first.plugin->arm(true);
            first.block();
            CHECK(first.misuse() == 1);
        }
        Rig second;
        second.block();
        CHECK(second.misuse() == 0);
    }

    if (g_failures == 0) {
        std::puts("PASS: RtMisusePerEngineTest");
        return 0;
    }
    return 1;
}
