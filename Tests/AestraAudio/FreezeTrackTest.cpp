// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FreezeTrackTest — V83-E11.
//
// Freeze renders a track's own output (tail included) into one clip, keeps the source clips MUTED in the project,
// bypasses the inserts and mutes their automation. What must hold:
//   1. a frozen project sounds the same as the live one;
//   2. thaw brings back the sources, the inserts and the automation, as one undo step each way;
//   3. NEGATIVE CONTROL: a frozen project saved and reopened still thaws, with no undo history to lean on;
//   4. it refuses, changing nothing: already frozen, nothing to freeze;
//   5. a clip the user had already muted stays muted through freeze and thaw.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Commands/CommandHistory.h"
#include "IO/AudioExporter.h"
#include "IO/BounceInPlace.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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


PlaylistLaneID firstLane(Session& s) {
    auto& pm = s.tm->getPlaylistModel();
    return pm.getLaneId(0);
}

bool clipMuted(Session& s, const ClipInstanceID& id) {
    const ClipInstance* c = s.tm->getPlaylistModel().getClip(id);
    return c && c->edits.muted;
}

} // namespace

int main() {
    std::cout << "[1] a frozen track sounds the same, and the sources stay in the project, muted\n";
    {
        Session s;
        const auto before = s.masterRender();
        const auto r = freezeTrack(*s.engine, *s.tm, firstLane(s));
        check(r.ok, "freeze succeeds: " + r.error);
        check(s.tm->getPlaylistModel().getClip(r.clip) != nullptr, "the frozen clip exists");
        check(s.tm->getPlaylistModel().getClip(s.clip) != nullptr && clipMuted(s, s.clip),
              "the source clip is still there, muted");
        check(s.channel->getEffectChain().isSlotBypassed(0), "the insert is bypassed");
        check(s.channel->frozenTrack().active() && s.channel->frozenTrack().frozenClip == r.clip,
              "the channel records the freeze");
        const auto after = s.masterRender();
        check(maxDiff(before, after) < 1e-4, "master render before == after (max diff " +
                                                 std::to_string(maxDiff(before, after)) + ")");
    }

    std::cout << "[2] thaw restores everything; one undo step each way\n";
    {
        Session s;
        const auto before = s.masterRender();
        const auto r = freezeTrack(*s.engine, *s.tm, firstLane(s));
        check(r.ok, "freeze succeeds: " + r.error);
        check(thawTrack(*s.tm, s.channel->getChannelId()), "thaw succeeds");
        check(s.tm->getPlaylistModel().getClip(r.clip) == nullptr, "the frozen clip is gone");
        check(!clipMuted(s, s.clip), "the source clip is audible again");
        check(!s.channel->getEffectChain().isSlotBypassed(0), "the insert is live again");
        check(!s.channel->frozenTrack().active(), "the channel is not frozen");
        check(maxDiff(before, s.masterRender()) < 1e-4, "sounds as it did");
        s.tm->getCommandHistory().undo();
        check(s.channel->frozenTrack().active() && clipMuted(s, s.clip), "undo thaw: frozen again");
        check(maxDiff(before, s.masterRender()) < 1e-4, "undo thaw: still sounds the same");
        s.tm->getCommandHistory().undo();
        check(!s.channel->frozenTrack().active() && !clipMuted(s, s.clip) &&
                  s.tm->getPlaylistModel().getClip(r.clip) == nullptr,
              "undo freeze: the track is as it was");
        s.tm->getCommandHistory().redo();
        check(s.channel->frozenTrack().active() && clipMuted(s, s.clip), "redo freeze: frozen again");
    }

    std::cout << "[3] automation: the insert's curve is muted by freeze, a volume curve keeps running\n";
    {
        Session s;
        auto& pm = s.tm->getPlaylistModel();
        AutomationCurve curve("gain", AutomationTarget::Custom);
        curve.deviceInstanceId = s.channel->getEffectChain().getSlotInstanceId(0);
        curve.paramId = 0;
        curve.mixerChannelId = s.channel->getChannelId();
        curve.addPoint(0.0, 0.25f, 24000.0, 0.0f);
        curve.addPoint(4.0, 0.25f, 24000.0, 0.0f);
        AutomationCurve vol("vol", AutomationTarget::Volume);
        vol.mixerChannelId = s.channel->getChannelId();
        vol.addPoint(0.0, 0.5f, 24000.0, 0.0f);
        vol.addPoint(4.0, 0.5f, 24000.0, 0.0f);
        pm.getLane(pm.getLaneId(0))->automationCurves = {curve, vol};
        const auto before = s.masterRender();
        check(freezeTrack(*s.engine, *s.tm, firstLane(s)).ok, "freeze with automation");
        auto* lane = pm.getLane(pm.getLaneId(0));
        check(lane->automationCurves[0].muted && !lane->automationCurves[1].muted, "plugin curve muted, volume curve not");
        check(maxDiff(before, s.masterRender()) < 1e-4, "sounds the same");
        check(thawTrack(*s.tm, s.channel->getChannelId()), "thaw");
        lane = pm.getLane(pm.getLaneId(0));
        check(!lane->automationCurves[0].muted && lane->automationCurves[0].getPoints().size() == 2,
              "thaw: the curve is back with its points");
    }

    std::cout << "[4] negative control: save, reopen, then thaw, with no undo history\n";
    {
        Aestra::Tests::ScopedTempDirectory dir{"FreezeTrack"};
        const std::string path = (dir.path() / "frozen.aes").string();
        Session s;
        const auto r = freezeTrack(*s.engine, *s.tm, firstLane(s));
        check(r.ok, "freeze succeeds: " + r.error);
        check(ProjectSerializer::save(path, s.tm, s.cfg.bpm, 0.0), "project saved");

        auto loaded = std::make_shared<TrackManager>();
        loaded->setOutputSampleRate(static_cast<double>(s.cfg.sampleRate));
        const auto result = ProjectSerializer::load(path, loaded);
        check(result.ok, "project reopened");
        MixerChannel* ch = loaded->getChannel(0);
        check(ch != nullptr && ch->frozenTrack().active(), "the reopened channel is still frozen");
        check(ch && ch->frozenTrack().frozenClip == r.clip && ch->frozenTrack().mutedClips.size() == 1,
              "it remembers which clip is the freeze and which it muted");
        check(!loaded->getCommandHistory().canUndo(), "there is no undo history");
        if (ch) {
            const uint32_t id = ch->getChannelId();
            check(thawTrack(*loaded, id), "thaw works on the reopened project");
            auto& pm = loaded->getPlaylistModel();
            const ClipInstance* source = pm.getClip(s.clip);
            check(pm.getClip(r.clip) == nullptr && source && !source->edits.muted,
                  "the frozen clip is gone and the source plays again");
            check(!ch->frozenTrack().active(), "not frozen any more");
        }
    }

    std::cout << "[5] state text: round trip, and damaged text loads as not frozen\n";
    {
        FrozenTrackState st;
        st.frozenClip = ClipInstanceID::generate();
        st.mutedClips = {ClipInstanceID::generate(), ClipInstanceID::generate()};
        st.bypassedSlots = {0, 3};
        st.mutedCurves = {{PlaylistLaneID::generate(), 77u, 4u}};
        const auto back = FrozenTrackState::fromText(st.toText());
        check(back.frozenClip == st.frozenClip && back.mutedClips == st.mutedClips &&
                  back.bypassedSlots == st.bypassedSlots && back.mutedCurves == st.mutedCurves,
              "round trip keeps every field");
        check(!FrozenTrackState::fromText("").active() && !FrozenTrackState::fromText("v9|x").active() &&
                  !FrozenTrackState::fromText("v1|nonsense|||").active(),
              "empty, unknown and damaged text are not frozen");
        FrozenTrackState none;
        check(none.toText().empty(), "a track that is not frozen writes nothing");
    }

    std::cout << "[6] it refuses, and changes nothing\n";
    {
        Session s;
        check(freezeTrack(*s.engine, *s.tm, firstLane(s)).ok, "freeze");
        const auto again = freezeTrack(*s.engine, *s.tm, firstLane(s));
        check(!again.ok && !again.error.empty(), "already frozen: " + again.error);
        check(s.clipCount() == 2, "no second frozen clip");
        check(!thawTrack(*s.tm, 9999), "thawing a channel that does not exist changes nothing");
        check(thawTrack(*s.tm, s.channel->getChannelId()) && !thawTrack(*s.tm, s.channel->getChannelId()),
              "thawing twice: the second does nothing");
    }
    {
        Session s;
        auto& pm = s.tm->getPlaylistModel();
        ClipEdits edits = pm.getClip(s.clip)->edits;
        edits.muted = true;
        pm.setClipEdits(s.clip, edits);
        const auto r = freezeTrack(*s.engine, *s.tm, firstLane(s));
        check(!r.ok && !r.error.empty(), "a track with only muted clips has nothing to freeze: " + r.error);
        check(!s.tm->getCommandHistory().canUndo(), "no undo step was recorded");
    }

    std::cout << "[7] a clip the user had already muted stays muted through freeze and thaw\n";
    {
        Session s;
        auto& pm = s.tm->getPlaylistModel();
        const auto* a = pm.getClip(s.clip);
        const ClipInstanceID extra = pm.addClipFromPattern(firstLane(s), a->patternId, 4.0, 1.0);
        ClipEdits edits = pm.getClip(extra)->edits;
        edits.muted = true;
        pm.setClipEdits(extra, edits);
        check(freezeTrack(*s.engine, *s.tm, firstLane(s)).ok, "freeze");
        check(s.channel->frozenTrack().mutedClips.size() == 1, "only the audible clip is recorded as ours");
        check(thawTrack(*s.tm, s.channel->getChannelId()), "thaw");
        check(clipMuted(s, extra) && !clipMuted(s, s.clip), "the user's muted clip is still muted");
    }

    std::cout << (g_failures ? "FAILED\n" : "All freeze tests passed.\n");
    return g_failures ? 1 : 0;
}
