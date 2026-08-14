// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AutomationPlaybackTest
// End-to-end regression for the automation playback pipe:
//
//   1. Drawn/loaded curves must actually reach the engine and shape output.
//      (PlaylistModel::buildRuntimeSnapshot never copied lane automation into
//      the snapshot, so automation persisted and displayed but NEVER played.)
//   2. Tempo changes must not shift automation in musical time (evaluation was
//      mixed-domain: stale add-time sample cache vs current-tempo target).
//   3. UI point drags edit point beats only; playback must follow the beat.
//   4. Pan automation pans; volume-only lanes keep the fader path intact.
//
// Drives the real RT path (AudioEngine::processBlock) with a deterministic
// generator plugin — no clips, no devices, no file I/O.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/AutomationCurve.h"
#include "Core/MixerChannel.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"

#include <algorithm>
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

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

// Constant-amplitude sine on both channels: gain changes read directly as
// RMS ratios, and pan gains read as L/R dominance.
class SineGeneratorPlugin : public IPluginInstance {
public:
    SineGeneratorPlugin() {
        m_info.id = "aestra.test.automation_generator";
        m_info.name = "AutomationGenerator";
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
            const double phase =
                2.0 * 3.14159265358979 * 220.0 * (static_cast<double>(m_sampleIndex + k) / kSampleRate);
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

// Gain effect whose single parameter (id 0) scales the signal. Parameter
// storage is atomic, mirroring the internal-plugin contract that makes
// per-block automation writes RT-safe. The reported format is configurable so
// the test can prove non-Internal formats are NOT driven by Custom curves.
class GainParamPlugin : public IPluginInstance {
public:
    explicit GainParamPlugin(PluginFormat format) {
        m_info.id = "aestra.test.automation_gain";
        m_info.name = "AutomationGain";
        m_info.vendor = "Aestra Test";
        m_info.version = "1.0";
        m_info.category = "Test";
        m_info.format = format;
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

struct Rendered {
    std::vector<float> left;
    std::vector<float> right;
    bool hasInvalid = false;
};

// Builds a one-insert project whose Playlist-hosted `curves` explicitly target
// that insert, sets the playlist BPM to `renderBpm`, and renders `beats` beats.
// An optional extra effect is inserted at chain slot 1 (after the generator).
Rendered renderWithAutomation(const std::vector<AutomationCurve>& curves, double renderBpm, double beats,
                              std::shared_ptr<IPluginInstance> slot1Fx = nullptr) {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));

    MixerChannel* src = trackManager->addChannel("src");
    require(src != nullptr, "addChannel failed");
    require(src->getEffectChain().insertPlugin(0, std::make_shared<SineGeneratorPlugin>()), "generator insert failed");
    if (slot1Fx) {
        require(src->getEffectChain().insertPlugin(1, std::move(slot1Fx)), "slot-1 effect insert failed");
    }

    auto& playlist = trackManager->getPlaylistModel();
    playlist.setProjectSampleRate(static_cast<double>(kSampleRate));
    playlist.setBPM(renderBpm);
    const auto laneId = playlist.createLane("lane0");
    auto* lane = playlist.getLane(laneId);
    require(lane != nullptr, "getLane failed");
    lane->automationCurves = curves;
    for (auto& curve : lane->automationCurves) {
        curve.mixerChannelId = src->getChannelId();
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
    engine.setTransportPlaying(true); // automation position tracks the transport

    const double samplesPerBeat = (static_cast<double>(kSampleRate) * 60.0) / renderBpm;
    const uint64_t totalFrames = static_cast<uint64_t>(beats * samplesPerBeat);

    Rendered out;
    out.left.reserve(totalFrames);
    out.right.reserve(totalFrames);
    std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    uint64_t rendered = 0;
    while (rendered < totalFrames) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, kBlockFrames, static_cast<double>(rendered) / kSampleRate);
        for (uint32_t k = 0; k < kBlockFrames && rendered + k < totalFrames; ++k) {
            const float l = block[static_cast<size_t>(k) * 2];
            const float r = block[static_cast<size_t>(k) * 2 + 1];
            if (!std::isfinite(l) || !std::isfinite(r)) {
                out.hasInvalid = true;
            }
            out.left.push_back(l);
            out.right.push_back(r);
        }
        rendered += kBlockFrames;
    }
    return out;
}

double rmsWindow(const std::vector<float>& x, double fromBeat, double toBeat, double bpm) {
    const double samplesPerBeat = (static_cast<double>(kSampleRate) * 60.0) / bpm;
    const size_t from = static_cast<size_t>(fromBeat * samplesPerBeat);
    const size_t to = std::min(static_cast<size_t>(toBeat * samplesPerBeat), x.size());
    if (to <= from) {
        return 0.0;
    }
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) {
        acc += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(acc / static_cast<double>(to - from));
}

AutomationCurve volumeCurve(std::initializer_list<std::pair<double, float>> pts, double addTimeSamplesPerBeat) {
    AutomationCurve curve("Volume", AutomationTarget::Volume);
    curve.setDefaultValue(1.0f);
    for (const auto& [beat, value] : pts) {
        curve.addPoint(beat, value, addTimeSamplesPerBeat, 0.5f);
    }
    return curve;
}

constexpr double kSpbAt120 = (static_cast<double>(kSampleRate) * 60.0) / 120.0; // 24000

} // namespace

int main() {
    // ---------------- 1. Primary regression: a drawn volume curve is audible
    // end-to-end. Full gain until beat 2, linear fade to silence at beat 4.
    {
        const auto r =
            renderWithAutomation({volumeCurve({{0.0, 1.0f}, {2.0, 1.0f}, {4.0, 0.0f}}, kSpbAt120)}, 120.0, 6.0);
        require(!r.hasInvalid, "volume: output contains NaN/Inf");
        const double loud = rmsWindow(r.left, 0.5, 1.5, 120.0);
        const double mid = rmsWindow(r.left, 2.9, 3.1, 120.0);
        const double silent = rmsWindow(r.left, 4.5, 5.5, 120.0);
        std::cout << "volume automation: loud=" << loud << " mid=" << mid << " tail=" << silent << "\n";
        require(loud > 1.0e-3, "volume automation region is silent (curve never reached the engine)");
        require(mid > 0.35 * loud && mid < 0.65 * loud, "volume automation midpoint is not ~half gain");
        require(silent < 0.02 * loud, "volume automation did not fade to silence (curve ignored)");
    }

    // ---------------- 2. Tempo-change correctness: the same curve added while
    // the project was at 120 BPM must stay beat-aligned when rendered at 240.
    // (Pre-fix, the stale sample cache stretched the fade to twice as long.)
    {
        const auto r =
            renderWithAutomation({volumeCurve({{0.0, 1.0f}, {2.0, 1.0f}, {4.0, 0.0f}}, kSpbAt120)}, 240.0, 8.0);
        require(!r.hasInvalid, "tempo: output contains NaN/Inf");
        const double loud = rmsWindow(r.left, 0.5, 1.5, 240.0);
        const double silent = rmsWindow(r.left, 5.0, 7.0, 240.0);
        require(loud > 1.0e-3, "tempo: automation region is silent");
        require(silent < 0.02 * loud,
                "tempo: automation shifted in musical time after BPM change (sample-domain evaluation)");
    }

    // ---------------- 3. UI drag contract: drags edit point beats in place and
    // re-sort; playback must follow the new beat, not a stale sample cache.
    {
        auto curve = volumeCurve({{0.0, 1.0f}, {4.0, 1.0f}}, kSpbAt120);
        auto& pts = curve.getPoints();
        pts[1].beat = 1.0;   // drag the beat-4 point to beat 1...
        pts[1].value = 0.0f; // ...and to zero gain (exactly what TrackUIComponent does)
        curve.sortPoints();
        const auto r = renderWithAutomation({curve}, 120.0, 4.0);
        require(!r.hasInvalid, "drag: output contains NaN/Inf");
        const double loud = rmsWindow(r.left, 0.0, 0.7, 120.0);
        const double silent = rmsWindow(r.left, 1.5, 3.5, 120.0);
        require(loud > 1.0e-3, "drag: pre-fade region is silent");
        require(silent < 0.02 * loud, "drag: playback ignored the dragged point (stale sample cache)");
    }

    // ---------------- 4. Pan automation pans; the volume path stays fader-driven
    // when no volume curve exists.
    {
        AutomationCurve pan("Pan", AutomationTarget::Pan);
        pan.setDefaultValue(0.0f);
        pan.addPoint(0.0, -1.0f, kSpbAt120, 0.5f);
        pan.addPoint(2.0, -1.0f, kSpbAt120, 0.5f);
        pan.addPoint(2.5, 1.0f, kSpbAt120, 0.5f);
        const auto r = renderWithAutomation({pan}, 120.0, 5.0);
        require(!r.hasInvalid, "pan: output contains NaN/Inf");
        const double leftEarly = rmsWindow(r.left, 0.5, 1.5, 120.0);
        const double rightEarly = rmsWindow(r.right, 0.5, 1.5, 120.0);
        const double leftLate = rmsWindow(r.left, 3.5, 4.5, 120.0);
        const double rightLate = rmsWindow(r.right, 3.5, 4.5, 120.0);
        std::cout << "pan automation: earlyL=" << leftEarly << " earlyR=" << rightEarly << " lateL=" << leftLate
                  << " lateR=" << rightLate << "\n";
        require(leftEarly > 1.0e-3, "pan: hard-left region has no left signal");
        require(rightEarly < 0.05 * leftEarly, "pan: hard-left region leaks right signal");
        require(rightLate > 1.0e-3, "pan: hard-right region has no right signal");
        require(leftLate < 0.05 * rightLate, "pan: hard-right region leaks left signal");
    }

    // ---------------- 5. Plugin-parameter automation (Custom target): a curve
    // addressed at {effect slot 1, param 0} drives an Internal-format gain
    // plugin — output fades even though volume/pan automation is absent.
    {
        AutomationCurve param("Gain", AutomationTarget::Custom);
        param.setDefaultValue(1.0f);
        param.effectSlot = 1;
        param.paramId = 0;
        param.addPoint(0.0, 1.0f, kSpbAt120, 0.5f);
        param.addPoint(2.0, 1.0f, kSpbAt120, 0.5f);
        param.addPoint(4.0, 0.0f, kSpbAt120, 0.5f);
        const auto r =
            renderWithAutomation({param}, 120.0, 6.0, std::make_shared<GainParamPlugin>(PluginFormat::Internal));
        require(!r.hasInvalid, "param: output contains NaN/Inf");
        const double loud = rmsWindow(r.left, 0.5, 1.5, 120.0);
        const double mid = rmsWindow(r.left, 2.9, 3.1, 120.0);
        const double silent = rmsWindow(r.left, 4.5, 5.5, 120.0);
        std::cout << "param automation: loud=" << loud << " mid=" << mid << " tail=" << silent << "\n";
        require(loud > 1.0e-3, "param automation region is silent");
        require(mid > 0.35 * loud && mid < 0.65 * loud, "param automation midpoint is not ~half gain");
        require(silent < 0.02 * loud, "param automation did not drive the plugin parameter");
    }

    // ---------------- 6. Third-party guard: the identical curve must NOT drive
    // a non-Internal plugin (no host param queue yet — silently skipping is the
    // contract until #467's third-party slice).
    {
        AutomationCurve param("Gain", AutomationTarget::Custom);
        param.setDefaultValue(1.0f);
        param.effectSlot = 1;
        param.paramId = 0;
        param.addPoint(0.0, 1.0f, kSpbAt120, 0.5f);
        param.addPoint(4.0, 0.0f, kSpbAt120, 0.5f);
        const auto r = renderWithAutomation({param}, 120.0, 6.0, std::make_shared<GainParamPlugin>(PluginFormat::VST3));
        require(!r.hasInvalid, "guard: output contains NaN/Inf");
        const double early = rmsWindow(r.left, 0.5, 1.5, 120.0);
        const double late = rmsWindow(r.left, 4.5, 5.5, 120.0);
        require(early > 1.0e-3, "guard: output is silent");
        require(late > 0.8 * early, "guard: Custom curve drove a non-Internal plugin (RT-unsafe path)");
    }

    // ---------------- 7. UI-created empty curve contract: the first point on
    // an empty lane creates a Volume curve with defaultValue 1.0 (neutral).
    // An empty curve must NOT silence the channel — the pre-fix UI creation
    // path left defaultValue at 0.0, which muted the channel until a point
    // was added.
    {
        AutomationCurve empty("Volume", AutomationTarget::Volume);
        empty.setDefaultValue(1.0f); // exactly what TrackUIComponent now sets
        const auto r = renderWithAutomation({empty}, 120.0, 3.0);
        const auto baseline = renderWithAutomation({}, 120.0, 3.0);
        require(!r.hasInvalid, "empty-curve: output contains NaN/Inf");
        const double level = rmsWindow(r.left, 0.5, 2.5, 120.0);
        const double baseLevel = rmsWindow(baseline.left, 0.5, 2.5, 120.0);
        std::cout << "empty volume curve (default 1.0): rms=" << level << " baseline=" << baseLevel << "\n";
        require(level > 1.0e-3, "empty-curve: default-1.0 volume curve silenced the channel");
        require(level > 0.95 * baseLevel, "empty-curve: default-1.0 volume curve attenuated the channel");
    }

    std::cout << "[PASS] AutomationPlaybackTest\n";
    return 0;
}
