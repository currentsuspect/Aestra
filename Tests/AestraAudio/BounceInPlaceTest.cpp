// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// BounceInPlaceTest — V8-W5.
//
// Bounce in Place renders a track's own output (inserts, insert-parameter
// automation, delay compensation; fader and pan stay live) over the selected
// clips' span, replaces the clips with the result, bypasses the inserts and
// mutes their automation, all as one undo step. What must hold:
//   1. the tap reads the strip BEFORE the fader, so the bounce does not bake the fader in;
//   2. after the bounce the project SOUNDS THE SAME (master render before == after);
//   3. undo restores the clips, the bypass flags and the automation; redo repeats the bounce;
//   4. it refuses, changing nothing, when the selection would play twice or is not bounceable.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "Commands/CommandHistory.h"
#include "IO/AudioExporter.h"
#include "IO/BounceInPlace.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kTau = 6.28318530717958647692;
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond)
        ++g_failures;
}

// Effect that scales its input by param 0 (default 0.5) and accepts render-thread automation.
class HalfGain final : public IPluginInstance {
public:
    HalfGain() {
        m_info.id = "aestra.test.bounce.halfgain";
        m_info.name = "HalfGain";
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
    void process(const float* const* inputs, float** outputs, uint32_t, uint32_t numOut, uint32_t frames,
                 const MidiBuffer* = nullptr, MidiBuffer* = nullptr) override {
        const float g = m_gain.load(std::memory_order_relaxed);
        for (uint32_t c = 0; c < numOut; ++c) {
            if (!outputs[c])
                continue;
            for (uint32_t i = 0; i < frames; ++i)
                outputs[c][i] = (inputs && inputs[c] ? inputs[c][i] : 0.0f) * g;
        }
    }
    std::vector<PluginParameter> getParameters() const override { return {}; }
    uint32_t getParameterCount() const override { return 1; }
    float getParameter(uint32_t id) const override { return id == 0 ? m_gain.load() : 0.0f; }
    void setParameter(uint32_t id, float v) override {
        if (id == 0)
            m_gain.store(v);
    }
    bool supportsRealtimeAutomation() const noexcept override { return true; }
    void applyAutomation(uint32_t id, float v) noexcept override { setParameter(id, v); }
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
    std::atomic<float> m_gain{0.5f};
};

std::vector<float> sine(double hz, float amp, uint32_t frames, uint32_t sr) {
    std::vector<float> s(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * hz * i / sr)) * amp;
        s[static_cast<size_t>(i) * 2] = v;
        s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

struct Session {
    SessionConfig cfg;
    std::shared_ptr<TrackManager> tm = std::make_shared<TrackManager>();
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    MixerChannel* channel = nullptr;
    ClipInstanceID clip;

    Session() {
        tm->setOutputSampleRate(cfg.sampleRate);
        tm->getPlaylistModel().setBPM(cfg.bpm);
        const uint32_t frames = cfg.sampleRate * 2; // 4 beats at 120 BPM
        addAudioTrack(*tm, "A", sine(440.0, 0.4f, frames, cfg.sampleRate), frames, cfg);
        channel = tm->getChannel(0);
        channel->getEffectChain().insertPlugin(0, std::make_shared<HalfGain>());
        channel->setVolume(0.5f); // the fader must NOT be baked into a bounce
        auto& pm = tm->getPlaylistModel();
        clip = pm.getLane(pm.getLaneId(0))->clips.front().id;
        prepareEngine(*engine, tm, cfg);
    }

    std::vector<float> masterRender() {
        AudioExporter exporter(*engine, *tm);
        AudioExporter::Config c;
        c.scope = AudioExporter::RenderScope::FullSong;
        c.endBeat = 4.0;
        c.sampleRate = cfg.sampleRate;
        c.bitDepth = AudioExporter::BitDepth::Float_32;
        c.tailSeconds = 0.0;
        AudioExporter::RenderedAudio out;
        exporter.renderToBuffer(c, out);
        return out.interleaved;
    }

    size_t clipCount() {
        size_t n = 0;
        auto& pm = tm->getPlaylistModel();
        for (size_t l = 0; l < pm.getLaneCount(); ++l)
            n += pm.getLane(pm.getLaneId(l))->clips.size();
        return n;
    }
};

double peak(const std::vector<float>& v, size_t from = 0) {
    double p = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        p = std::max(p, static_cast<double>(std::fabs(v[i])));
    return p;
}

// Every clip edge carries a 128-frame anti-click ramp (kClipEdgeFadeSamples). The bounce bakes the
// ramp into the audio and the bounced clip's own edge applies it once more, so the first and last
// 128 frames differ by design; everything between them must match.
double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size())
        return 1e9;
    constexpr size_t kEdge = 2 * 130;
    double d = 0.0;
    for (size_t i = kEdge; i + kEdge < a.size(); ++i)
        d = std::max(d, static_cast<double>(std::fabs(a[i] - b[i])));
    return d;
}

// A mono 16-bit tone, for a Sampler unit.
bool writeToneWav(const std::string& path, double hz, double amp, uint32_t frames, uint32_t sr) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    const uint32_t dataBytes = frames * 2;
    const uint32_t riff = 36 + dataBytes, fmt = 16, byteRate = sr * 2;
    const uint16_t pcmFmt = 1, ch = 1, align = 2, bits = 16;
    out.write("RIFF", 4); out.write(reinterpret_cast<const char*>(&riff), 4); out.write("WAVEfmt ", 8);
    out.write(reinterpret_cast<const char*>(&fmt), 4); out.write(reinterpret_cast<const char*>(&pcmFmt), 2);
    out.write(reinterpret_cast<const char*>(&ch), 2); out.write(reinterpret_cast<const char*>(&sr), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4); out.write(reinterpret_cast<const char*>(&align), 2);
    out.write(reinterpret_cast<const char*>(&bits), 2); out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataBytes), 4);
    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t v = static_cast<int16_t>(std::sin(kTau * hz * i / sr) * amp * 32767.0);
        out.write(reinterpret_cast<const char*>(&v), 2);
    }
    return out.good();
}

} // namespace

int main() {
    std::cout << "[1] the tap reads the strip before the fader\n";
    {
        Session s;
        AudioExporter exporter(*s.engine, *s.tm);
        AudioExporter::Config c;
        c.scope = AudioExporter::RenderScope::FullSong;
        c.endBeat = 4.0;
        c.sampleRate = s.cfg.sampleRate;
        c.tailSeconds = 0.0;
        c.captureMixerChannelId = s.channel->getChannelId();
        AudioExporter::RenderedAudio tap;
        check(exporter.renderToBuffer(c, tap).success, "the track-output render succeeds");
        const auto master = s.masterRender();
        // sine 0.4 through the insert (x0.5) = 0.2; the fader (0.5) is only in the master mix.
        check(std::fabs(peak(tap.interleaved, 4096) - 0.2) < 0.01, "track output carries the insert (peak ~0.2)");
        check(peak(master, 4096) < 0.12 && peak(master, 4096) > 0.08, "master mix also carries the fader (peak ~0.1)");

        c.captureMixerChannelId = 9999;
        AudioExporter::RenderedAudio none;
        check(exporter.renderToBuffer(c, none).success && peak(none.interleaved) == 0.0,
              "a channel that does not exist renders silence, not the master");
        c.captureMixerChannelId = s.channel->getChannelId();
        c.numChannels = 1;
        check(!exporter.render(c).success, "a mono track-output render is refused");
    }

    std::cout << "[2] after the bounce the project sounds the same\n";
    {
        Session s;
        const auto before = s.masterRender();
        const auto r = bounceClipsInPlace(*s.engine, *s.tm, {s.clip});
        check(r.ok, "bounce succeeds: " + r.error);
        check(s.clipCount() == 1 && s.tm->getPlaylistModel().getClip(r.clip) != nullptr, "one clip, the bounced one");
        check(s.tm->getPlaylistModel().getClip(s.clip) == nullptr, "the original clip is gone");
        check(s.channel->getEffectChain().isSlotBypassed(0), "the insert is bypassed");
        const auto after = s.masterRender();
        check(maxDiff(before, after) < 1e-4, "master render before == after (max diff " +
                                                 std::to_string(maxDiff(before, after)) + ")");

        std::cout << "[3] one undo step restores everything; redo repeats it\n";
        s.tm->getCommandHistory().undo();
        check(s.clipCount() == 1 && s.tm->getPlaylistModel().getClip(s.clip) != nullptr, "undo: the original clip is back");
        check(s.tm->getPlaylistModel().getClip(r.clip) == nullptr, "undo: the bounced clip is gone");
        check(!s.channel->getEffectChain().isSlotBypassed(0), "undo: the insert is live again");
        check(maxDiff(before, s.masterRender()) < 1e-4, "undo: sounds as it did");
        s.tm->getCommandHistory().redo();
        check(s.channel->getEffectChain().isSlotBypassed(0) && s.clipCount() == 1, "redo: bounced again");
        check(maxDiff(before, s.masterRender()) < 1e-4, "redo: sounds the same");
    }

    std::cout << "[4] insert automation is baked, then muted; undo unmutes it\n";
    {
        Session s;
        auto& pm = s.tm->getPlaylistModel();
        AutomationCurve curve("gain", AutomationTarget::Custom);
        curve.deviceInstanceId = s.channel->getEffectChain().getSlotInstanceId(0);
        curve.paramId = 0;
        curve.mixerChannelId = s.channel->getChannelId();
        curve.addPoint(0.0, 0.25f, 24000.0, 0.0f);
        curve.addPoint(4.0, 0.25f, 24000.0, 0.0f);
        // And a volume curve: it drives the fader, which stays live, so it must keep running.
        AutomationCurve vol("vol", AutomationTarget::Volume);
        vol.mixerChannelId = s.channel->getChannelId();
        vol.addPoint(0.0, 0.5f, 24000.0, 0.0f);
        vol.addPoint(4.0, 0.5f, 24000.0, 0.0f);
        auto* lane = pm.getLane(pm.getLaneId(0));
        lane->automationCurves = {curve, vol};
        const auto before = s.masterRender();
        // 0.4 sine x 0.25 insert gain x 0.5 volume curve x 0.5 fader: automation composes with the fader (FD-64, #466).
        check(peak(before, 4096) < 0.03 && peak(before, 4096) > 0.02, "automation drives the insert to 0.25 (peak ~0.025)");

        const auto r = bounceClipsInPlace(*s.engine, *s.tm, {s.clip});
        check(r.ok, "bounce with automation succeeds: " + r.error);
        lane = pm.getLane(pm.getLaneId(0));
        check(lane->automationCurves.size() == 2 && lane->automationCurves[0].muted && !lane->automationCurves[1].muted,
              "the plugin curve is muted, the volume curve is not");
        check(maxDiff(before, s.masterRender()) < 1e-4, "sounds the same with the curve muted");
        s.tm->getCommandHistory().undo();
        lane = pm.getLane(pm.getLaneId(0));
        check(!lane->automationCurves[0].muted && lane->automationCurves[0].getPoints().size() == 2,
              "undo: the curve is back with its points");
        check(maxDiff(before, s.masterRender()) < 1e-4, "undo: sounds as it did");
    }

    std::cout << "[5] it refuses, and changes nothing\n";
    {
        Session s;
        check(!bounceClipsInPlace(*s.engine, *s.tm, {}).ok, "empty selection");
        // A second clip on the same track, overlapping the span but not selected.
        auto& pm = s.tm->getPlaylistModel();
        const auto laneB = pm.createLane("B");
        const auto* clipA = pm.getClip(s.clip);
        const auto other = pm.addClipFromPattern(laneB, clipA->patternId, 1.0, 2.0);
        check(other.isValid(), "second clip placed");
        const auto refused = bounceClipsInPlace(*s.engine, *s.tm, {s.clip});
        check(!refused.ok && !refused.error.empty(), "an unselected clip on the same track in the span: " + refused.error);
        check(s.clipCount() == 2 && !s.channel->getEffectChain().isSlotBypassed(0), "nothing changed");
        check(!s.tm->getCommandHistory().canUndo(), "no undo step was recorded");
        // Selecting both clips (same track) is fine.
        check(bounceClipsInPlace(*s.engine, *s.tm, {s.clip, other}).ok, "both selected: bounces");
        check(s.clipCount() == 1, "two clips became one");
    }
    {
        Session s;
        auto& pm = s.tm->getPlaylistModel();
        const auto* clipA = pm.getClip(s.clip);
        s.tm->getPatternManager().setPatternMixerChannel(clipA->patternId, 0);
        // Routed straight to Master: it bounces onto Master (owner ruling 2026-10-15) and sounds the same.
        const auto before = s.masterRender();
        const auto r = bounceClipsInPlace(*s.engine, *s.tm, {s.clip});
        check(r.ok, "a master-routed clip bounces: " + r.error);
        const auto* bounced = pm.getClip(r.clip);
        const auto* bouncedPattern = bounced ? s.tm->getPatternManager().getPattern(bounced->patternId) : nullptr;
        check(bouncedPattern && bouncedPattern->isAudio() && bouncedPattern->getMixerChannelId() == 0, "onto Master");
        check(maxDiff(before, s.masterRender()) < 1e-4, "and sounds the same");
        check(!s.channel->getEffectChain().isSlotBypassed(0), "a track it never played through keeps its insert");
    }

    std::cout << "[6] a pattern clip bounces too\n";
    {
        // The Sampler unit comes from the built-in plugin registry.
        check(PluginManager::getInstance().initialize(), "plugin registry initialises");
        SessionConfig cfg;
        auto tm = std::make_shared<TrackManager>();
        tm->setOutputSampleRate(cfg.sampleRate);
        tm->getPlaylistModel().setBPM(cfg.bpm);
        auto* channel = tm->addChannel("Keys");
        channel->getEffectChain().insertPlugin(0, std::make_shared<HalfGain>());
        const std::string wav = (std::filesystem::temp_directory_path() / "bounce_pattern_tone.wav").string();
        writeToneWav(wav, 440.0, 0.8, cfg.sampleRate * 4, cfg.sampleRate);
        auto& units = tm->getUnitManager();
        const UnitID unit = units.createUnit("Keys", UnitType::Sampler);
        units.setUnitAudioClip(unit, wav);
        units.setUnitEnabled(unit, true);
        units.setUnitGain(unit, 1.0f);
        units.setUnitMixerChannel(unit, channel->getChannelId());

        auto& patterns = tm->getPatternManager();
        const PatternID pid = patterns.createPattern();
        auto* pat = patterns.getPattern(pid);
        pat->type = PatternSource::Type::Midi;
        pat->lengthBeats = 4.0;
        pat->payload = MidiPayload{};
        std::get<MidiPayload>(pat->payload).notes.push_back(MidiNote{60, 0.0, 3.5, 1.0f, 0.0f, unit});
        auto& pm = tm->getPlaylistModel();
        const auto lane = pm.createLane("Keys");
        ClipInstance ci;
        ci.patternId = pid;
        ci.sourceId = pid.value;
        ci.startBeat = 0.0;
        ci.durationBeats = 4.0;
        const ClipInstanceID midiClip = pm.addClip(lane, ci);
        auto engine = std::make_unique<AudioEngine>();
        prepareEngine(*engine, tm, cfg);
        engine->setUnitManager(&tm->getUnitManager());
        engine->setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());

        const auto master = [&]() {
            tm->getPatternPlaybackEngine().clearScheduledInstances();
            tm->scheduleTimelineForOfflineRender(0.0);

            AudioExporter exporter(*engine, *tm);
            AudioExporter::Config c;
            c.scope = AudioExporter::RenderScope::FullSong;
            c.endBeat = 4.0;
            c.sampleRate = cfg.sampleRate;
            c.bitDepth = AudioExporter::BitDepth::Float_32;
            c.tailSeconds = 0.0;
            AudioExporter::RenderedAudio out;
            exporter.renderToBuffer(c, out);
            return out.interleaved;
        };
        const auto cold = master();
        const auto before = master();
        check(maxDiff(cold, before) < 1e-6, "two renders in a row are identical (no voice rings into the next)");
        check(peak(before, 4096) > 0.05, "the pattern plays through the track before the bounce (peak " +
                                             std::to_string(peak(before, 4096)) + ")");
        const auto r = bounceClipsInPlace(*engine, *tm, {midiClip});
        check(r.ok, "bounce succeeds: " + r.error);
        const auto* bounced = pm.getClip(r.clip);
        const auto* bouncedPattern = bounced ? patterns.getPattern(bounced->patternId) : nullptr;
        check(pm.getClip(midiClip) == nullptr && bouncedPattern && bouncedPattern->isAudio(),
              "the pattern clip became an audio clip");
        check(channel->getEffectChain().isSlotBypassed(0), "the insert is bypassed");
        const auto after = master();
        check(maxDiff(before, after) < 1e-3, "master render before == after (max diff " +
                                                 std::to_string(maxDiff(before, after)) + ", peaks " +
                                                 std::to_string(peak(before, 4096)) + " / " +
                                                 std::to_string(peak(after, 4096)) + ")");
        tm->getCommandHistory().undo();
        check(pm.getClip(midiClip) != nullptr && pm.getClip(r.clip) == nullptr, "undo: the pattern clip is back");
        check(maxDiff(before, master()) < 1e-3, "undo: sounds as it did");

        // Another clip of the same instrument playing in the span would play twice.
        const auto laneB = pm.createLane("B");
        const auto twin = pm.addClipFromPattern(laneB, pid, 1.0, 2.0);
        const auto refused = bounceClipsInPlace(*engine, *tm, {midiClip});
        check(twin.isValid() && !refused.ok && !refused.error.empty(),
              "another clip of the same instrument in the span is refused: " + refused.error);

        // A pattern with no notes has nothing to bounce.
        const PatternID emptyId = patterns.createPattern();
        auto* emptyPat = patterns.getPattern(emptyId);
        emptyPat->type = PatternSource::Type::Midi;
        emptyPat->lengthBeats = 4.0;
        emptyPat->payload = MidiPayload{};
        const auto laneC = pm.createLane("C");
        const auto emptyClip = pm.addClipFromPattern(laneC, emptyId, 8.0, 4.0);
        check(!bounceClipsInPlace(*engine, *tm, {emptyClip}).ok, "an empty pattern is refused");
    }

    std::cout << "[7] a pattern on several tracks (and straight to Master) becomes one clip on Master\n";
    {
        SessionConfig cfg;
        auto tm = std::make_shared<TrackManager>();
        tm->setOutputSampleRate(cfg.sampleRate);
        tm->getPlaylistModel().setBPM(cfg.bpm);
        auto* kickTrack = tm->addChannel("Kick");
        kickTrack->getEffectChain().insertPlugin(0, std::make_shared<HalfGain>());
        kickTrack->setVolume(0.5f); // a fader the bounce must carry, since the clip no longer passes through it
        auto* clapTrack = tm->addChannel("Clap");
        auto& units = tm->getUnitManager();
        const auto makeUnit = [&](const char* name, double hz, uint32_t channelId) {
            const std::string wav = (std::filesystem::temp_directory_path() / (std::string("bounce_multi_") + name + ".wav")).string();
            writeToneWav(wav, hz, 0.6, cfg.sampleRate * 4, cfg.sampleRate);
            const UnitID u = units.createUnit(name, UnitType::Sampler);
            units.setUnitAudioClip(u, wav);
            units.setUnitEnabled(u, true);
            units.setUnitGain(u, 1.0f);
            units.setUnitMixerChannel(u, channelId);
            return u;
        };
        const UnitID kick = makeUnit("kick", 110.0, kickTrack->getChannelId());
        const UnitID clap = makeUnit("clap", 660.0, clapTrack->getChannelId());
        const UnitID hat = makeUnit("hat", 2200.0, 0); // straight to Master

        auto& patterns = tm->getPatternManager();
        const PatternID pid = patterns.createPattern();
        auto* pat = patterns.getPattern(pid);
        pat->type = PatternSource::Type::Midi;
        pat->lengthBeats = 4.0;
        pat->payload = MidiPayload{};
        auto& notes = std::get<MidiPayload>(pat->payload).notes;
        notes.push_back(MidiNote{60, 0.0, 1.5, 1.0f, 0.0f, kick});
        notes.push_back(MidiNote{60, 1.0, 1.5, 1.0f, 0.0f, clap});
        notes.push_back(MidiNote{60, 2.0, 1.5, 1.0f, 0.0f, hat});
        auto& pm = tm->getPlaylistModel();
        const auto lane = pm.createLane("Drums");
        ClipInstance ci;
        ci.patternId = pid;
        ci.sourceId = pid.value;
        ci.startBeat = 0.0;
        ci.durationBeats = 4.0;
        const ClipInstanceID drums = pm.addClip(lane, ci);
        auto engine = std::make_unique<AudioEngine>();
        prepareEngine(*engine, tm, cfg);
        engine->setUnitManager(&tm->getUnitManager());
        engine->setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
        const auto master = [&]() {
            tm->getPatternPlaybackEngine().clearScheduledInstances();
            tm->scheduleTimelineForOfflineRender(0.0);
            AudioExporter exporter(*engine, *tm);
            AudioExporter::Config c;
            c.scope = AudioExporter::RenderScope::FullSong;
            c.endBeat = 4.0;
            c.sampleRate = cfg.sampleRate;
            c.bitDepth = AudioExporter::BitDepth::Float_32;
            c.tailSeconds = 0.0;
            AudioExporter::RenderedAudio out;
            exporter.renderToBuffer(c, out);
            return out.interleaved;
        };
        master();
        const auto before = master();
        check(peak(before, 4096) > 0.05, "the drums play before the bounce");
        const auto r = bounceClipsInPlace(*engine, *tm, {drums});
        check(r.ok, "bounce succeeds: " + r.error);
        const auto* bounced = pm.getClip(r.clip);
        const auto* bouncedPattern = bounced ? patterns.getPattern(bounced->patternId) : nullptr;
        check(pm.getClip(drums) == nullptr && bouncedPattern && bouncedPattern->isAudio(), "the pattern clip became an audio clip");
        check(bouncedPattern && bouncedPattern->getMixerChannelId() == 0, "placed on Master");
        check(!kickTrack->getEffectChain().isSlotBypassed(0), "the tracks keep their inserts (the clip no longer passes through them)");
        const auto after = master();
        check(maxDiff(before, after) < 1e-3, "master render before == after (max diff " + std::to_string(maxDiff(before, after)) +
                                                 ", peaks " + std::to_string(peak(before, 4096)) + " / " + std::to_string(peak(after, 4096)) + ")");
        tm->getCommandHistory().undo();
        check(pm.getClip(drums) != nullptr && pm.getClip(r.clip) == nullptr, "undo: the pattern clip is back");
        check(maxDiff(before, master()) < 1e-3, "undo: sounds as it did");

        // Another pattern on one of those tracks in the span would be baked in and then play again.
        const PatternID other = patterns.createPattern();
        auto* op = patterns.getPattern(other);
        op->type = PatternSource::Type::Midi;
        op->lengthBeats = 4.0;
        op->payload = MidiPayload{};
        std::get<MidiPayload>(op->payload).notes.push_back(MidiNote{62, 0.0, 1.0, 1.0f, 0.0f, clap});
        const auto laneB = pm.createLane("Fill");
        pm.addClipFromPattern(laneB, other, 1.0, 2.0);
        const auto refused = bounceClipsInPlace(*engine, *tm, {drums});
        check(!refused.ok && !refused.error.empty(), "another clip through one of the same tracks is refused: " + refused.error);
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
