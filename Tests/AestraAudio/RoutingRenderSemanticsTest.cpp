// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Routing Render Semantics (Contract V1-V3): render-level verification that
// the audio actually delivered matches the routing contract —
//   V1: pre-fader sends survive the source mute gate; post-fader sends die.
//   V2: sidechain key input follows the source's solo state and never enters
//       the destination's audible mix.
//   V3: combined — muted source + solo context + pre-fader sidechain: the
//       tap happens pre-mute, but the solo gate still silences the key.
// All assertions are on rendered buffers, not on code branches.

#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Aestra;
using namespace Aestra::Audio;

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 256;
constexpr uint32_t kChannels = 2;
constexpr double kRenderSeconds = 0.5;
constexpr uint32_t kRenderFrames = static_cast<uint32_t>(kRenderSeconds * kSampleRate);
constexpr double kTonePeak = 0.55;
constexpr double kKeyScale = 2.0; // SidechainGainPlugin output = input * (1 + kKeyScale * keyPeak)

int g_failures = 0;

#define require(cond, msg)                                        \
    do {                                                          \
        if (!(cond)) {                                            \
            std::cerr << "FAIL: " << msg << std::endl;            \
            ++g_failures;                                         \
            return;                                               \
        }                                                         \
    } while (0)

double toneAmplitude(const std::vector<float>& interleaved, double frequency, uint32_t sampleRate,
                     size_t skipFrames = 2048) {
    const size_t frameCount = interleaved.size() / 2;
    if (frameCount <= skipFrames) {
        return 0.0;
    }
    double real = 0.0;
    double imag = 0.0;
    const double omega = 2.0 * 3.14159265358979 * frequency / static_cast<double>(sampleRate);
    for (size_t frame = skipFrames; frame < frameCount; ++frame) {
        const double sample = static_cast<double>(interleaved[frame * 2]);
        const double phase = omega * static_cast<double>(frame);
        real += sample * std::cos(phase);
        imag -= sample * std::sin(phase);
    }
    return 2.0 * std::sqrt(real * real + imag * imag) / static_cast<double>(frameCount - skipFrames);
}

bool writeToneWav(const std::string& path, double frequency, double peak, uint32_t frames) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    const uint32_t dataBytes = frames * 2 * sizeof(int16_t);
    out.write("RIFF", 4);
    const uint32_t riffSize = 36 + dataBytes;
    out.write(reinterpret_cast<const char*>(&riffSize), 4);
    out.write("WAVEfmt ", 8);
    const uint32_t fmtChunk = 16;
    out.write(reinterpret_cast<const char*>(&fmtChunk), 4);
    const uint16_t format = 1;
    out.write(reinterpret_cast<const char*>(&format), 2);
    const uint16_t channels = 1;
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&kSampleRate), 4);
    const uint32_t byteRate = kSampleRate * 2;
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    const uint16_t blockAlign = 2;
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    const uint16_t bits = 16;
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataBytes), 4);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
        const float sample = static_cast<float>(std::sin(2.0 * 3.14159265358979 * frequency * t) * peak);
        const int16_t pcm = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        out.write(reinterpret_cast<const char*>(&pcm), 2);
    }
    return out.good();
}

// Test plugin: output = input * (1 + kKeyScale * keyPeak). The sidechain key
// input arrives as extra input channels (EffectChain appends sidechain after
// the audio channels for plugins declaring enough inputs). Observing the
// destination's output amplitude therefore reports whether the key input
// actually arrived — without the key, output equals input.
class SidechainGainPlugin : public IPluginInstance {
public:
    SidechainGainPlugin() {
        m_info.id = "aestra.test.sidechain_gain";
        m_info.name = "SidechainGain";
        m_info.vendor = "Aestra Test";
        m_info.version = "1.0";
        m_info.category = "Test";
        m_info.format = PluginFormat::Internal;
        m_info.type = PluginType::Effect;
        m_info.numAudioInputs = 4; // 2 audio + 2 sidechain
        m_info.numAudioOutputs = 2;
    }

    bool initialize(double, uint32_t) override { return true; }
    void shutdown() override {}
    void activate() override { m_active = true; }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                 uint32_t numOutputChannels, uint32_t numFrames,
                 const MidiBuffer* = nullptr, MidiBuffer* = nullptr) override {
        double keyPeak = 0.0;
        const bool hasSidechain = inputs && numInputChannels >= 4 && inputs[2] && inputs[3];
        if (hasSidechain) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                keyPeak = std::max(keyPeak, std::max(std::abs(static_cast<double>(inputs[2][i])),
                                                     std::abs(static_cast<double>(inputs[3][i]))));
            }
        }
        const float scale = static_cast<float>(1.0 + kKeyScale * keyPeak);
        const uint32_t channels = std::min(numOutputChannels, 2u);
        for (uint32_t c = 0; c < channels; ++c) {
            if (inputs && inputs[c] && outputs && outputs[c]) {
                for (uint32_t i = 0; i < numFrames; ++i) {
                    outputs[c][i] = inputs[c][i] * scale;
                }
            } else if (outputs && outputs[c]) {
                std::memset(outputs[c], 0, sizeof(float) * numFrames);
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
    PluginInfo m_info{};
    bool m_active = false;
};

struct Fixture {
    std::shared_ptr<TrackManager> tm;
    AudioEngine engine;
    std::string sampleRoot;

    MixerChannel* src{nullptr};  // 220 Hz tone source
    MixerChannel* dst{nullptr};  // destination (bus)
    UnitID srcUnit{0};
    UnitID dstUnit{0};
    PatternID patternId{};

    explicit Fixture(const std::string& tag) {
        sampleRoot = "/tmp/Aestra_tests/RoutingSemantics_" + tag + "_" +
                     std::to_string(reinterpret_cast<uintptr_t>(this));
        tm = std::make_shared<TrackManager>();
        tm->setOutputSampleRate(static_cast<double>(kSampleRate));
        tm->getPlaylistModel().setBPM(120.0);

        src = tm->addChannelWithId("Src", 101);
        dst = tm->addChannelWithId("Dst", 102);
        tm->addChannelWithId("Other", 103);
    }

    void initEngine() {
        engine.setTrackManager(tm);
        engine.initialize();
        engine.setSampleRate(kSampleRate);
        engine.setBufferConfig(kBlockSize, kChannels);
        engine.setBPM(120.0f);
    }

    UnitID addUnit(MixerChannel* channel, const std::string& wavName, double frequency) {
        const std::string path = sampleRoot + "_" + wavName + ".wav";
        if (!writeToneWav(path, frequency, kTonePeak, kSampleRate)) {
            std::cerr << "FAIL: could not write tone wav " << path << std::endl;
            std::exit(1);
        }
        auto& units = tm->getUnitManager();
        const UnitID id = units.createUnit(wavName, UnitType::Sampler);
        units.setUnitAudioClip(id, path);
        units.setUnitEnabled(id, true);
        units.setUnitGain(id, 1.0f);
        units.setUnitMixerChannel(id, channel->getChannelId());
        return id;
    }

    void schedulePattern() {
        auto& units = tm->getUnitManager();
        const std::vector<UnitID> allUnits = units.getAllUnitIDs();
        auto& patterns = tm->getPatternManager();
        patternId = patterns.createPattern();
        auto* pattern = patterns.getPattern(patternId);
        pattern->type = PatternSource::Type::Midi;
        pattern->lengthBeats = 8.0;
        pattern->payload = MidiPayload{};
        auto& notes = std::get<MidiPayload>(pattern->payload).notes;
        for (const UnitID id : allUnits) {
            notes.push_back(MidiNote{60, 0.0, 7.5, 120.0f, 0.0f, id});
        }
    }

    void startEngine() {
        engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
        if (auto slotMap = tm->getChannelSlotMapShared()) {
            engine.setChannelSlotMap(slotMap);
        }
        engine.setUnitManager(&tm->getUnitManager());
        engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
        engine.setPatternPlaybackMode(true, 8.0);
        engine.setGlobalSamplePos(0);
        engine.setMetronomeEnabled(false);
        engine.setAuditionModeEnabled(false);
        tm->getPatternPlaybackEngine().rewindScheduledInstances();
        tm->getPatternPlaybackEngine().schedulePatternInstance(patternId, 0.0, 1);
        engine.setTransportPlaying(true);
    }

    // Render to master; returns interleaved samples.
    std::vector<float> render() {
        std::vector<float> out(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(kRenderFrames) * kChannels);
        uint32_t rendered = 0;
        while (rendered < kRenderFrames) {
            tm->getPatternPlaybackEngine().refillWindow(rendered, static_cast<int>(kSampleRate),
                                                        static_cast<int>(kSampleRate));
            const uint32_t frames = std::min(kBlockSize, kRenderFrames - rendered);
            std::fill(out.begin(), out.end(), 0.0f);
            engine.processBlock(out.data(), nullptr, frames, 0.0);
            samples.insert(samples.end(), out.begin(), out.begin() + static_cast<std::ptrdiff_t>(frames * kChannels));
            rendered += frames;
        }
        engine.setTransportPlaying(false);
        return samples;
    }

    void stop() { engine.setTransportPlaying(false); }
};

// ============================================================================
// V1 — pre-fader send vs mute
// ============================================================================
void testV1PreFaderSurvivesMute() {
    Fixture fx("v1");
    fx.initEngine();
    fx.srcUnit = fx.addUnit(fx.src, "src", 220.0);
    fx.schedulePattern();

    // Src -> master (main); Src pre-fader send -> Dst; Dst -> master.
    AudioRoute send;
    send.targetChannelId = fx.dst->getChannelId();
    send.gain = 1.0f;
    send.postFader = false;
    fx.src->addSend(send);

    // Route exists in the compiled topology (audibleDownstream edge).
    {
        AudioGraph graph = AudioGraphBuilder::buildFromTrackManager(*fx.tm);
        const size_t srcIdx = graph.trackIndexById[fx.src->getChannelId()];
        bool found = false;
        for (const size_t dest : graph.audibleDownstream[srcIdx]) {
            if (graph.tracks[dest].trackId == fx.dst->getChannelId()) {
                found = true;
            }
        }
        require(found, "V1: send edge must exist in the compiled topology");
    }

    fx.startEngine();
    std::vector<float> unmuted = fx.render();
    const double unmutedAmp = toneAmplitude(unmuted, 220.0, kSampleRate);
    require(unmutedAmp > 1e-3, "V1: unmuted source must reach master");

    fx.src->setMute(true);
    fx.engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*fx.tm));
    fx.startEngine();
    std::vector<float> mutedPreFader = fx.render();
    const double mutedAmp = toneAmplitude(mutedPreFader, 220.0, kSampleRate);
    require(mutedAmp > 1e-3, "V1: muted source pre-fader send must still deliver audio");
    fx.stop();

    std::cout << "V1 pre-fader survives mute: unmuted=" << unmutedAmp << " muted=" << mutedAmp << "\n";
    // Unmuted master carries source main + send (2x tone); muted carries the
    // send path alone (~0.5x). Require the send path to be a substantial
    // fraction — the point is the signal is NOT silenced by mute.
    if (mutedAmp < unmutedAmp * 0.3) {
        std::cerr << "FAIL: V1: pre-fader send level collapsed under mute\n";
        ++g_failures;
    }
}

void testV1PostFaderDiesOnMute() {
    Fixture fx("v1post");
    fx.initEngine();
    fx.srcUnit = fx.addUnit(fx.src, "src", 220.0);
    fx.schedulePattern();

    AudioRoute send;
    send.targetChannelId = fx.dst->getChannelId();
    send.gain = 1.0f;
    send.postFader = true;
    fx.src->addSend(send);

    fx.startEngine();
    std::vector<float> unmuted = fx.render();
    const double unmutedAmp = toneAmplitude(unmuted, 220.0, kSampleRate);
    require(unmutedAmp > 1e-3, "V1-post: unmuted post-fader send must reach master");

    fx.src->setMute(true);
    fx.engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*fx.tm));
    fx.startEngine();
    std::vector<float> muted = fx.render();
    const double mutedAmp = toneAmplitude(muted, 220.0, kSampleRate);
    require(mutedAmp < 1e-4, "V1-post: muted source post-fader send must be silent");
    fx.stop();

    std::cout << "V1 post-fader dies on mute: unmuted=" << unmutedAmp << " muted=" << mutedAmp << "\n";
}

// ============================================================================
// V2 — solo + sidechain
// ============================================================================
void testV2SidechainNeverEntersAudibleMix() {
    Fixture fx("v2mix");
    fx.initEngine();
    fx.srcUnit = fx.addUnit(fx.src, "src", 220.0); // sidechain key source
    fx.dstUnit = fx.addUnit(fx.dst, "dst", 440.0); // destination's own signal
    fx.schedulePattern();

    // Dst -> master. Src main -> master too (own audible path, distinct freq).
    AudioRoute sidechain;
    sidechain.targetChannelId = fx.dst->getChannelId();
    sidechain.gain = 1.0f;
    sidechain.sidechainOnly = true;
    fx.src->addSend(sidechain);

    // Without a sidechain consumer the destination output must be IDENTICAL
    // with and without the sidechain edge — the key never enters the mix.
    fx.startEngine();
    std::vector<float> withEdge = fx.render();
    fx.stop();

    fx.src->removeSend(fx.src->getSends()[0].sendId);
    fx.engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*fx.tm));
    fx.startEngine();
    std::vector<float> withoutEdge = fx.render();
    fx.stop();

    const double ampWith = toneAmplitude(withEdge, 440.0, kSampleRate);
    const double ampWithout = toneAmplitude(withoutEdge, 440.0, kSampleRate);
    require(std::abs(ampWith - ampWithout) < 1e-6,
            "V2: sidechain edge must never contribute to the destination's audible mix");
    require(ampWith > 1e-3, "V2: destination's own signal must render");
    std::cout << "V2 key never in mix: with=" << ampWith << " without=" << ampWithout << "\n";
}

void testV2KeyFollowsSourceSoloState() {
    Fixture fx("v2solo");
    fx.initEngine();
    fx.srcUnit = fx.addUnit(fx.src, "src", 220.0); // sidechain key source
    fx.dstUnit = fx.addUnit(fx.dst, "dst", 440.0); // destination own signal
    fx.schedulePattern();

    AudioRoute sidechain;
    sidechain.targetChannelId = fx.dst->getChannelId();
    sidechain.gain = 1.0f;
    sidechain.sidechainOnly = true;
    fx.src->addSend(sidechain);
    require(fx.dst->getEffectChain().insertPlugin(0, std::make_shared<SidechainGainPlugin>()),
            "V2: sidechain observer plugin inserted");

    // Baseline: no solo — key flows; Dst output scaled by the key.
    fx.startEngine();
    std::vector<float> noSolo = fx.render();
    const double noSoloAmp = toneAmplitude(noSolo, 440.0, kSampleRate);
    require(noSoloAmp > 1e-3, "V2: destination signal must render");

    // Solo Dst only: Dst eligible, Src not -> key silenced (follows source).
    fx.dst->setSolo(true);
    fx.engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*fx.tm));
    fx.startEngine();
    std::vector<float> dstSoloed = fx.render();
    const double dstSoloAmp = toneAmplitude(dstSoloed, 440.0, kSampleRate);
    require(dstSoloAmp > 1e-3, "V2: soloed destination must still render its own signal");

    // Solo Src too: key flows again.
    fx.src->setSolo(true);
    fx.engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*fx.tm));
    fx.startEngine();
    std::vector<float> bothSoloed = fx.render();
    const double bothSoloAmp = toneAmplitude(bothSoloed, 440.0, kSampleRate);
    require(bothSoloAmp > 1e-3, "V2: both soloed must render");
    fx.stop();

    std::cout << "V2 key follows solo state: noSolo=" << noSoloAmp << " dstSolo=" << dstSoloAmp
              << " bothSolo=" << bothSoloAmp << "\n";
    // The key-scaled output must be measurably louder than the key-silenced
    // one. Sampler gain staging makes the key scale ~1.15x, so discriminate
    // at 0.92/0.95 rather than assuming a doubling.
    require(dstSoloAmp < noSoloAmp * 0.92, "V2: non-soloed source key must be silenced in solo context");
    require(bothSoloAmp > noSoloAmp * 0.95, "V2: soloed source key must flow again");
}

// ============================================================================
// V3 — muted + solo context + pre-fader sidechain
// ============================================================================
void testV3CombinedOrdering() {
    Fixture fx("v3");
    fx.initEngine();
    fx.srcUnit = fx.addUnit(fx.src, "src", 220.0);
    fx.dstUnit = fx.addUnit(fx.dst, "dst", 440.0);
    fx.schedulePattern();

    AudioRoute sidechain;
    sidechain.targetChannelId = fx.dst->getChannelId();
    sidechain.gain = 1.0f;
    sidechain.sidechainOnly = true;
    sidechain.postFader = false; // pre-fader tap
    fx.src->addSend(sidechain);
    require(fx.dst->getEffectChain().insertPlugin(0, std::make_shared<SidechainGainPlugin>()),
            "V3: sidechain observer plugin inserted");

    // V3a: muted source, NO solo context -> pre-fader tap bypasses the mute
    // gate, key arrives, destination output scaled.
    fx.src->setMute(true);
    fx.startEngine();
    std::vector<float> mutedNoSolo = fx.render();
    const double mutedNoSoloAmp = toneAmplitude(mutedNoSolo, 440.0, kSampleRate);
    require(mutedNoSoloAmp > 1e-3, "V3a: destination must render");
    fx.stop();

    // V3b: muted source + solo context (Dst soloed, Src not) -> solo gate
    // beats the pre-fader tap; key silenced.
    fx.dst->setSolo(true);
    fx.engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*fx.tm));
    fx.startEngine();
    std::vector<float> mutedSolo = fx.render();
    const double mutedSoloAmp = toneAmplitude(mutedSolo, 440.0, kSampleRate);
    require(mutedSoloAmp > 1e-3, "V3b: soloed destination must render its own signal");
    fx.stop();

    // V3c: muted source + POST-fader sidechain, no solo -> mute gate beats
    // the tap; key silenced.
    {
        const auto sends = fx.src->getSends();
        auto post = sends[0];
        post.postFader = true;
        fx.src->setSend(sends[0].sendId, post);
    }
    fx.dst->setSolo(false);
    fx.engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*fx.tm));
    fx.startEngine();
    std::vector<float> mutedPost = fx.render();
    const double mutedPostAmp = toneAmplitude(mutedPost, 440.0, kSampleRate);
    require(mutedPostAmp > 1e-3, "V3c: destination must render");
    fx.stop();

    std::cout << "V3 ordering: muted+noSolo(preFader)=" << mutedNoSoloAmp
              << " muted+solo(preFader)=" << mutedSoloAmp << " muted(postFader)=" << mutedPostAmp << "\n";
    require(mutedSoloAmp < mutedNoSoloAmp * 0.92,
            "V3: solo gate must silence the pre-fader key of a non-soloed muted source");
    require(mutedPostAmp < mutedNoSoloAmp * 0.92,
            "V3: mute gate must silence a post-fader key");
}

} // namespace

int main() {
    if (!PluginManager::getInstance().initialize()) {
        std::cerr << "FAIL: plugin manager initialize\n";
        return 1;
    }
    testV1PreFaderSurvivesMute();
    testV1PostFaderDiesOnMute();
    testV2SidechainNeverEntersAudibleMix();
    testV2KeyFollowsSourceSoloState();
    testV3CombinedOrdering();

    if (g_failures == 0) {
        std::cout << "[PASS] RoutingRenderSemanticsTest\n";
        return 0;
    }
    std::cerr << g_failures << " RoutingRenderSemantics test(s) failed\n";
    return 1;
}
