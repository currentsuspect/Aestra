// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CrossfadeTest — V83-E10 (FD-94): overlap two audio clips and they crossfade.
//
// A crossfade is a link stored on the LATER clip plus a shape; the fade length is always the current overlap.
// What must hold:
//   1. the resolver turns a link into the overlap-length fades, follows the overlap as clips move, and ignores a
//      link that no longer means anything (partner gone, touching only, wrong order);
//   2. Straight (equal gain) dips ~3 dB in the middle of two different recordings; Smooth (equal power) does not;
//      an unlinked overlap just sums (+3 dB), exactly as old projects always played;
//   3. link, unlink and reshape are each ONE undo step, and refuse (changing nothing) when they would do nothing;
//   4. a crossfade survives save and reopen; a file with none loads with none.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Commands/CommandHistory.h"
#include "Commands/CrossfadeCommands.h"
#include "IO/AudioExporter.h"
#include "Models/CrossfadeResolve.h"

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
bool closeTo(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

std::vector<float> sine(double hz, float amp, uint32_t frames, uint32_t sr) {
    std::vector<float> s(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * hz * i / sr)) * amp;
        s[static_cast<size_t>(i) * 2] = v;
        s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

ClipInstance clip(double start, double dur) {
    ClipInstance c;
    c.id = ClipInstanceID::generate();
    c.startBeat = start;
    c.durationBeats = dur;
    return c;
}

// Clip A: beats 0-4 (440 Hz). Clip B: beats 3-7 (660 Hz), moved onto A's lane. The overlap is beat 3-4.
struct Session {
    SessionConfig cfg;
    std::shared_ptr<TrackManager> tm = std::make_shared<TrackManager>();
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    ClipInstanceID a, b;
    PlaylistLaneID lane;

    Session() {
        tm->setOutputSampleRate(cfg.sampleRate);
        tm->getPlaylistModel().setBPM(cfg.bpm);
        const uint32_t frames = cfg.sampleRate * 2; // 4 beats at 120 BPM
        addAudioTrackAtBeat(*tm, "A", sine(440.0, 0.4f, frames, cfg.sampleRate), frames, cfg, 0.0);
        addAudioTrackAtBeat(*tm, "B", sine(660.0, 0.4f, frames, cfg.sampleRate), frames, cfg, 3.0);
        auto& pm = tm->getPlaylistModel();
        lane = pm.getLaneId(0);
        a = pm.getLane(lane)->clips.front().id;
        const PlaylistLaneID laneB = pm.getLaneId(1);
        const ClipInstance moved = pm.getLane(laneB)->clips.front();
        b = moved.id;
        pm.removeClip(b);
        pm.addClip(lane, moved);
        prepareEngine(*engine, tm, cfg);
    }

    std::vector<float> render() {
        AudioExporter exporter(*engine, *tm);
        AudioExporter::Config c;
        c.scope = AudioExporter::RenderScope::FullSong;
        c.endBeat = 7.0;
        c.sampleRate = cfg.sampleRate;
        c.bitDepth = AudioExporter::BitDepth::Float_32;
        c.tailSeconds = 0.0;
        AudioExporter::RenderedAudio out;
        exporter.renderToBuffer(c, out);
        return out.interleaved;
    }

    // RMS of the left channel across beats [from, to).
    double rms(const std::vector<float>& audio, double fromBeat, double toBeat) const {
        const double perBeat = cfg.sampleRate * 60.0 / cfg.bpm;
        const size_t f0 = static_cast<size_t>(fromBeat * perBeat), f1 = static_cast<size_t>(toBeat * perBeat);
        double sum = 0.0;
        size_t n = 0;
        for (size_t f = f0; f < f1 && f * 2 < audio.size(); ++f, ++n)
            sum += static_cast<double>(audio[f * 2]) * audio[f * 2];
        return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
    }

    bool link(FadeShape shape) {
        std::string why;
        auto cmd = makeCrossfadeCommand(tm->getPlaylistModel(), tm->getPatternManager(), a, b, &why, &shape);
        if (!cmd)
            return false;
        tm->getCommandHistory().pushAndExecute(cmd);
        return true;
    }
};

} // namespace

int main() {
    std::cout << "[1] the resolver turns a link into overlap-length fades\n";
    {
        ClipInstance a = clip(0.0, 4.0), b = clip(3.0, 4.0);
        std::vector<ClipInstance> lane = {a, b};
        auto plain = resolveClipFades(lane, lane[1]);
        check(plain.fadeInBeats == 0.0 && plain.fadeOutBeats == 0.0, "no link: no fades, as before");

        lane[1].edits.crossfadeFrom = lane[0].id;
        lane[1].edits.crossfadeShape = FadeShape::EqualPower;
        auto later = resolveClipFades(lane, lane[1]);
        auto earlier = resolveClipFades(lane, lane[0]);
        check(closeTo(later.fadeInBeats, 1.0, 1e-9) && later.fadeInShape == FadeShape::EqualPower,
              "the later clip fades in across the overlap, in the chosen shape");
        check(closeTo(earlier.fadeOutBeats, 1.0, 1e-9) && earlier.fadeOutShape == FadeShape::EqualPower,
              "the earlier clip fades out across the same overlap");
        check(closeTo(earlier.fadeInBeats, 0.0, 1e-9) && closeTo(later.fadeOutBeats, 0.0, 1e-9),
              "the far ends keep their own fades");

        lane[1].startBeat = 2.5; // overlap grows to 1.5
        check(closeTo(resolveClipFades(lane, lane[1]).fadeInBeats, 1.5, 1e-9) &&
                  closeTo(resolveClipFades(lane, lane[0]).fadeOutBeats, 1.5, 1e-9),
              "move a clip and the crossfade follows the new overlap");

        lane[1].startBeat = 4.0; // only touching
        check(resolveClipFades(lane, lane[1]).fadeInBeats == 0.0 && !isCrossfaded(lane, lane[1]),
              "clips that only touch have no crossfade");
        lane[1].startBeat = 3.0;
        lane[1].edits.crossfadeFrom = ClipInstanceID::generate();
        check(resolveClipFades(lane, lane[1]).fadeInBeats == 0.0, "a link to a clip that is gone is ignored");
        lane[1].edits.crossfadeFrom = lane[0].id;
        std::swap(lane[0].startBeat, lane[1].startBeat); // the 'earlier' now starts later
        check(resolveClipFades(lane, lane[1]).fadeInBeats == 0.0, "a link pointing forwards in time is ignored");

        lane[0].startBeat = 0.0; lane[1].startBeat = 3.0;
        lane[1].edits.fadeInBeats = 0.25f; // an own fade is replaced by the crossfade, never added to it
        check(closeTo(resolveClipFades(lane, lane[1]).fadeInBeats, 1.0, 1e-9), "the crossfade replaces an own fade-in");
    }

    std::cout << "[2] what you hear: Straight dips, Smooth holds, an unlinked overlap sums\n";
    {
        Session s;
        const auto plain = s.render();
        const double single = s.rms(plain, 0.5, 1.5); // A alone
        check(closeTo(single, 0.4 / std::sqrt(2.0), 0.01), "reference: one clip is its own level");
        const double summed = s.rms(plain, 3.25, 3.75) / single;
        check(closeTo(summed, std::sqrt(2.0), 0.08), "unlinked overlap sums, +3 dB (ratio " + std::to_string(summed) + ")");

        check(s.link(FadeShape::Linear), "link Straight");
        const double straight = s.rms(s.render(), 3.25, 3.75) / single;
        check(closeTo(straight, 0.736, 0.06), "Straight dips about 3 dB in the middle (ratio " + std::to_string(straight) + ")");

        std::string why;
        auto reshape = makeCrossfadeShapeCommand(s.tm->getPlaylistModel(), s.b, FadeShape::EqualPower, &why);
        check(reshape != nullptr, "reshape to Smooth");
        if (reshape)
            s.tm->getCommandHistory().pushAndExecute(reshape);
        const double smooth = s.rms(s.render(), 3.25, 3.75) / single;
        check(closeTo(smooth, 1.0, 0.06), "Smooth holds the level (ratio " + std::to_string(smooth) + ")");

        const auto whole = s.render();
        check(s.rms(whole, 0.5, 2.5) / single > 0.95 && s.rms(whole, 4.5, 6.5) / single > 0.95,
              "away from the overlap the clips play at full level");
    }

    std::cout << "[3] each edit is one undo step, and refuses when it would do nothing\n";
    {
        Session s;
        auto& pm = s.tm->getPlaylistModel();
        check(s.link(FadeShape::EqualPower) && pm.getClip(s.b)->edits.crossfadeFrom == s.a, "Crossfade links");
        s.tm->getCommandHistory().undo();
        check(!pm.getClip(s.b)->edits.crossfadeFrom.isValid(), "one undo takes it away");
        s.tm->getCommandHistory().redo();
        check(pm.getClip(s.b)->edits.crossfadeFrom == s.a, "redo brings it back");

        std::string why;
        auto cut = makeCutCrossfadeCommand(pm, s.b, &why);
        check(cut != nullptr, "Cut it here");
        if (cut)
            s.tm->getCommandHistory().pushAndExecute(cut);
        check(!pm.getClip(s.b)->edits.crossfadeFrom.isValid(), "cut: back to a plain overlap");
        why.clear();
        const bool cutAgain = makeCutCrossfadeCommand(pm, s.b, &why) == nullptr;
        check(cutAgain && why == "There is no crossfade here.", "cut again: refused (\"" + why + "\")");
        why.clear();
        const bool reshapeNone = makeCrossfadeShapeCommand(pm, s.b, FadeShape::Linear, &why) == nullptr;
        check(reshapeNone && why == "There is no crossfade here.", "reshape with no crossfade: refused (\"" + why + "\")");

        // Refusals: no overlap, wrong order.
        auto* lane = pm.getLane(s.lane);
        for (auto& c : lane->clips)
            if (c.id == s.b)
                c.startBeat = 4.0;
        why.clear();
        const bool touchRefused = makeCrossfadeCommand(pm, s.tm->getPatternManager(), s.a, s.b, &why) == nullptr;
        check(touchRefused && why == "The clips do not overlap.", "clips that only touch: refused (\"" + why + "\")");
        for (auto& c : lane->clips)
            if (c.id == s.b)
                c.startBeat = 3.0;
        why.clear();
        const bool backwards = makeCrossfadeCommand(pm, s.tm->getPatternManager(), s.b, s.a, &why) == nullptr;
        check(backwards && why == "The clips do not overlap.", "the later clip as the earlier one: refused (\"" + why + "\")");
        check(s.link(FadeShape::Linear) && !s.link(FadeShape::Linear), "linking the same pair twice: the second does nothing");
    }

    std::cout << "[4] the same recording starts Straight; different recordings start Smooth\n";
    {
        Session s;
        auto& pm = s.tm->getPlaylistModel();
        check(defaultCrossfadeShape(s.tm->getPatternManager(), *pm.getClip(s.a), *pm.getClip(s.b)) == FadeShape::EqualPower,
              "two different recordings: Smooth");
        const ClipInstanceID twin = pm.addClipFromPattern(s.lane, pm.getClip(s.a)->patternId, 3.5, 2.0);
        check(twin.isValid() &&
                  defaultCrossfadeShape(s.tm->getPatternManager(), *pm.getClip(s.a), *pm.getClip(twin)) == FadeShape::Linear,
              "two cuts of one recording: Straight");
    }

    std::cout << "[5] a crossfade survives save and reopen; a file with none loads with none\n";
    {
        Aestra::Tests::ScopedTempDirectory dir{"Crossfade"};
        const std::string with = (dir.path() / "with.aes").string();
        const std::string without = (dir.path() / "without.aes").string();
        Session s;
        check(ProjectSerializer::save(without, s.tm, s.cfg.bpm, 0.0), "saved a project with no crossfade");
        check(s.link(FadeShape::Linear), "crossfade");
        check(ProjectSerializer::save(with, s.tm, s.cfg.bpm, 0.0), "saved a project with a crossfade");

        auto loaded = std::make_shared<TrackManager>();
        loaded->setOutputSampleRate(static_cast<double>(s.cfg.sampleRate));
        check(ProjectSerializer::load(with, loaded).ok, "reopened");
        const ClipInstance* later = loaded->getPlaylistModel().getClip(s.b);
        check(later && later->edits.crossfadeFrom == s.a && later->edits.crossfadeShape == FadeShape::Linear,
              "the link and its shape came back");

        auto plain = std::make_shared<TrackManager>();
        plain->setOutputSampleRate(static_cast<double>(s.cfg.sampleRate));
        check(ProjectSerializer::load(without, plain).ok, "reopened the file with none");
        const ClipInstance* laterPlain = plain->getPlaylistModel().getClip(s.b);
        check(laterPlain && !laterPlain->edits.crossfadeFrom.isValid(), "it has none: old projects play as they did");
    }

    std::cout << (g_failures ? "FAILED\n" : "All crossfade tests passed.\n");
    return g_failures ? 1 : 0;
}
