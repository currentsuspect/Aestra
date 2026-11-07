// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Delete Silence, command half (founder request 2026-11-07). The clip is built
// the way the import path builds one (source -> audio pattern -> clip) from a
// synthetic voice-over with known pauses, so the cut positions, the audio each
// surviving piece resolves to, the ripple scope and undo/redo are all checked
// against numbers worked out by hand.

#include "Commands/CommandHistory.h"
#include "Commands/DeleteSilenceCommand.h"
#include "Models/ClipRenderService.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
constexpr uint32_t kRate = 48000;

void require(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool close(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

struct Burst {
    double fromSec;
    double toSec;
};

/// Words at -20 dBFS over a -70 dBFS floor. Layout in seconds: lead 0..0.6, word 0.6..1.6, pause 1.6..2.4,
/// word 2.4..3.2, short pause 3.2..3.5, word 3.5..4.0, tail 4.0..4.8.
std::shared_ptr<AudioBufferData> makeVoiceOver() {
    const std::vector<Burst> words = {{0.6, 1.6}, {2.4, 3.2}, {3.5, 4.0}};
    auto b = std::make_shared<AudioBufferData>();
    b->sampleRate = kRate;
    b->numChannels = 1;
    b->numFrames = static_cast<uint64_t>(4.8 * kRate);
    b->interleavedData.assign(static_cast<size_t>(b->numFrames), 0.0f);
    uint32_t seed = 987;
    const double noiseAmp = std::sqrt(3.0) * std::pow(10.0, -70.0 / 20.0);
    for (uint64_t f = 0; f < b->numFrames; ++f) {
        seed = seed * 1664525u + 1013904223u;
        double s = ((static_cast<double>(seed >> 8) / 8388608.0) - 1.0) * noiseAmp;
        const double t = static_cast<double>(f) / kRate;
        for (const auto& w : words)
            if (t >= w.fromSec && t < w.toSec)
                s += std::sqrt(2.0) * 0.1 * std::sin(2.0 * M_PI * 1000.0 * t);
        b->interleavedData[f] = static_cast<float>(s);
    }
    return b;
}

struct Fixture {
    std::shared_ptr<TrackManager> tm;
    PlaylistLaneID lane1;
    PlaylistLaneID lane2;
    ClipInstanceID voice;      // the audio clip with the pauses, lane 1 at beat 4
    ClipInstanceID laterSame;  // lane 1, after the voice clip
    ClipInstanceID midOther;   // lane 2, starts after the lead cut but inside the voice clip
    ClipInstanceID lateOther;  // lane 2, after the voice clip
    ClipInstanceID earlyOther; // lane 2, before the voice clip
    double bpm = 120.0;
    double beatsPerSec() const { return bpm / 60.0; }
};

ClipInstanceID addClipAt(Fixture& fx, const PlaylistLaneID& lane, PatternID pattern, double startBeat, double durationBeats,
                         double durationSeconds, const std::string& name) {
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.name = name;
    clip.startBeat = startBeat;
    clip.durationBeats = durationBeats;
    clip.durationSeconds = durationSeconds;
    clip.patternId = pattern;
    clip.edits = ClipEdits::forNewAudioClip();
    fx.tm->getPlaylistModel().addClip(lane, clip);
    return clip.id;
}

Fixture makeFixture(const std::string& dir) {
    Fixture fx;
    fx.tm = std::make_shared<TrackManager>();
    fx.tm->setRecordingProjectPath(dir);
    fx.bpm = fx.tm->getPlaylistModel().getBPM();

    auto buffer = makeVoiceOver();
    const double seconds = buffer->durationSeconds();
    const ClipSourceID sourceId = fx.tm->getSourceManager().createRecordedSource(dir + "/vo.wav", "vo", buffer);

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = seconds;
    AudioSlice slice;
    slice.startSamples = 0.0;
    slice.lengthSamples = static_cast<double>(buffer->numFrames);
    payload.slices.push_back(slice);
    const double beats = seconds * fx.beatsPerSec();
    const PatternID pattern = fx.tm->getPatternManager().createAudioPattern("vo", beats, payload);

    fx.lane1 = fx.tm->getPlaylistModel().createLane("Voice");
    fx.lane2 = fx.tm->getPlaylistModel().createLane("Music");
    fx.voice = addClipAt(fx, fx.lane1, pattern, 4.0, beats, seconds, "vo");
    fx.laterSame = addClipAt(fx, fx.lane1, pattern, 4.0 + beats + 2.0, beats, seconds, "later");
    fx.earlyOther = addClipAt(fx, fx.lane2, pattern, 0.0, 2.0, 1.0, "early");
    fx.midOther = addClipAt(fx, fx.lane2, pattern, 4.0 + 0.5 * fx.beatsPerSec(), 2.0, 1.0, "mid");
    fx.lateOther = addClipAt(fx, fx.lane2, pattern, 4.0 + beats + 1.0, 2.0, 1.0, "late");
    return fx;
}

struct ClipState {
    PlaylistLaneID lane;
    ClipInstance clip;
};

/// Every clip on every lane by id: the whole observable playlist.
std::map<std::string, ClipState> snapshot(TrackManager& tm) {
    std::map<std::string, ClipState> out;
    tm.getPlaylistModel().withLanes([&](const std::vector<PlaylistLane>& lanes) {
        for (const auto& lane : lanes)
            for (const auto& clip : lane.clips)
                out[clip.id.toString()] = {lane.id, clip};
        return 0;
    });
    return out;
}

bool sameState(const std::map<std::string, ClipState>& a, const std::map<std::string, ClipState>& b, std::string& why) {
    if (a.size() != b.size()) {
        why = "clip count " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
        return false;
    }
    for (const auto& [id, x] : a) {
        auto it = b.find(id);
        if (it == b.end()) {
            why = "clip " + id + " missing";
            return false;
        }
        const ClipInstance& p = x.clip;
        const ClipInstance& q = it->second.clip;
        // Exact comparison on purpose: undo must put back the very same numbers, not close ones.
        if (!(x.lane == it->second.lane) || p.startBeat != q.startBeat || p.durationBeats != q.durationBeats ||
            p.durationSeconds != q.durationSeconds || p.sourceOffset != q.sourceOffset ||
            p.sourceOffsetSeconds != q.sourceOffsetSeconds || !(p.edits == q.edits) || !(p.patternId == q.patternId)) {
            why = "clip " + p.name + " differs (start " + std::to_string(p.startBeat) + " vs " + std::to_string(q.startBeat) +
                  ", dur " + std::to_string(p.durationBeats) + " vs " + std::to_string(q.durationBeats) + ")";
            return false;
        }
    }
    return true;
}

std::vector<ClipInstance> laneClips(TrackManager& tm, const PlaylistLaneID& lane) {
    std::vector<ClipInstance> out;
    tm.getPlaylistModel().withLanes([&](const std::vector<PlaylistLane>& lanes) {
        for (const auto& l : lanes)
            if (l.id == lane)
                out = l.clips;
        return 0;
    });
    std::sort(out.begin(), out.end(), [](const ClipInstance& a, const ClipInstance& b) { return a.startBeat < b.startBeat; });
    return out;
}

double startOf(TrackManager& tm, const ClipInstanceID& id) {
    const ClipInstance* c = tm.getPlaylistModel().getClip(id);
    return c ? c->startBeat : -1.0;
}

// Removed (source seconds): lead [0,0.475), pause [1.725,2.275), tail [4.125,4.8) = 1.7 s.
constexpr double kRemovedSec = 0.475 + 0.55 + 0.675;

void testThisLaneRippleAndPieces(const std::string& dir) {
    std::printf("this lane: pieces, positions, later clip on the same lane moves, other lane does not...\n");
    Fixture fx = makeFixture(dir);
    const double bps = fx.beatsPerSec();
    const double lateSameBefore = startOf(*fx.tm, fx.laterSame);
    const double lateOtherBefore = startOf(*fx.tm, fx.lateOther);

    auto cmd = std::make_shared<DeleteSilenceCommand>(*fx.tm, std::vector<ClipInstanceID>{fx.voice}, SilenceRipple::ThisLane);
    fx.tm->getCommandHistory().pushAndExecute(cmd);
    require(cmd->isUndoable(), "command made changes");
    require(cmd->cutCount() == 3, "three cuts; got " + std::to_string(cmd->cutCount()));
    require(std::fabs(cmd->removedSeconds() - kRemovedSec) < 0.011, "removed seconds ~1.7; got " + std::to_string(cmd->removedSeconds()));

    // Lane 1: two pieces of the voice clip, then the later clip.
    const auto lane1 = laneClips(*fx.tm, fx.lane1);
    require(lane1.size() == 3, "lane 1 holds two voice pieces and the later clip; got " + std::to_string(lane1.size()));
    if (lane1.size() != 3)
        return;
    // Piece 1 = source [0.475, 1.725): starts where the clip started (the lead cut closed up), 1.25 s long.
    const double p1Sec = 1.725 - 0.475;
    const double p2Sec = 4.125 - 2.275;
    require(close(lane1[0].startBeat, 4.0, 0.02), "piece 1 keeps the clip's start (beat 4); got " + std::to_string(lane1[0].startBeat));
    require(close(lane1[0].durationBeats, p1Sec * bps, 0.03), "piece 1 duration; got " + std::to_string(lane1[0].durationBeats));
    require(close(lane1[1].startBeat, lane1[0].startBeat + lane1[0].durationBeats, 1e-9), "piece 2 butts against piece 1 (the pause cut closed)");
    require(close(lane1[1].durationBeats, p2Sec * bps, 0.03), "piece 2 duration; got " + std::to_string(lane1[1].durationBeats));

    // Total length reduced by exactly what was removed.
    const double span = lane1[1].startBeat + lane1[1].durationBeats - lane1[0].startBeat;
    const double originalSpan = 4.8 * bps;
    require(close(originalSpan - span, cmd->removedSeconds() * bps, 1e-6), "span shrank by exactly the removed time");
    require(close(originalSpan - span, kRemovedSec * bps, 0.04), "span shrank by ~1.7 s of beats");

    // The later clip on the same lane moves left by the same amount.
    const double shift = lateSameBefore - startOf(*fx.tm, fx.laterSame);
    require(close(shift, originalSpan - span, 1e-6), "later clip on the same lane moved left by the removed time");
    // Other lane untouched.
    require(close(startOf(*fx.tm, fx.midOther), 4.0 + 0.5 * bps, 1e-9), "mid clip on the other lane did not move");
    require(close(startOf(*fx.tm, fx.lateOther), lateOtherBefore, 1e-12), "late clip on the other lane did not move");
    require(close(startOf(*fx.tm, fx.earlyOther), 0.0, 1e-9), "early clip on the other lane did not move");

    // What each piece actually plays: resolve it through the production seam.
    ClipRenderService service(fx.tm->getSourceManager(), fx.tm->getPatternManager());
    const double sr = fx.tm->getPlaylistModel().getProjectSampleRate();
    const auto r1 = service.resolveClipRegion(lane1[0], sr);
    const auto r2 = service.resolveClipRegion(lane1[1], sr);
    require(r1.isValid() && r2.isValid(), "both pieces resolve to audio");
    if (r1.isValid() && r2.isValid()) {
        require(std::llabs(static_cast<long long>(r1.startFrame) - static_cast<long long>(0.475 * kRate)) <= 480, "piece 1 plays from source 0.475 s");
        require(std::llabs(static_cast<long long>(r1.frameCount) - static_cast<long long>(p1Sec * kRate)) <= 480, "piece 1 plays 1.25 s");
        require(std::llabs(static_cast<long long>(r2.startFrame) - static_cast<long long>(2.275 * kRate)) <= 480, "piece 2 plays from source 2.275 s");
        require(std::llabs(static_cast<long long>(r2.frameCount) - static_cast<long long>(p2Sec * kRate)) <= 480, "piece 2 plays 1.85 s");
    }

    // Short fades on the new edges: piece 1 fades out into the cut, piece 2 fades in out of it.
    require(lane1[0].edits.fadeOutBeats > 0.0f && lane1[0].edits.fadeOutBeats < 0.1f, "piece 1 has a short fade-out at the cut");
    require(lane1[1].edits.fadeInBeats > 0.0f && lane1[1].edits.fadeInBeats < 0.1f, "piece 2 has a short fade-in at the cut");
}

void testAllLanesRipple(const std::string& dir) {
    std::printf("all lanes: clips on other lanes after each cut move left by the cuts before them...\n");
    Fixture fx = makeFixture(dir);
    const double bps = fx.beatsPerSec();
    const double midBefore = startOf(*fx.tm, fx.midOther);
    const double lateBefore = startOf(*fx.tm, fx.lateOther);
    const double earlyBefore = startOf(*fx.tm, fx.earlyOther);
    const double lateSameBefore = startOf(*fx.tm, fx.laterSame);

    auto cmd = std::make_shared<DeleteSilenceCommand>(*fx.tm, std::vector<ClipInstanceID>{fx.voice}, SilenceRipple::AllLanes);
    fx.tm->getCommandHistory().pushAndExecute(cmd);
    require(cmd->cutCount() == 3, "three cuts");

    const double total = cmd->removedSeconds() * bps;
    // mid clip starts at 0.5 s into the voice clip: after the lead cut (ends 0.475 s), before the pause cut.
    require(close(midBefore - startOf(*fx.tm, fx.midOther), 0.475 * bps, 0.03), "mid clip moved by the lead cut only; moved " + std::to_string(midBefore - startOf(*fx.tm, fx.midOther)));
    require(close(lateBefore - startOf(*fx.tm, fx.lateOther), total, 1e-6), "late clip on the other lane moved by every cut");
    require(close(lateSameBefore - startOf(*fx.tm, fx.laterSame), total, 1e-6), "later clip on the same lane moved by every cut");
    require(close(earlyBefore, startOf(*fx.tm, fx.earlyOther), 1e-12), "a clip before the cut never moves");
}

void testUndoRedoExact(const std::string& dir) {
    std::printf("undo/redo: bit-exact positions, durations, offsets, edits...\n");
    for (SilenceRipple ripple : {SilenceRipple::ThisLane, SilenceRipple::AllLanes}) {
        Fixture fx = makeFixture(dir);
        const auto before = snapshot(*fx.tm);
        fx.tm->getCommandHistory().pushAndExecute(
            std::make_shared<DeleteSilenceCommand>(*fx.tm, std::vector<ClipInstanceID>{fx.voice}, ripple));
        const auto after = snapshot(*fx.tm);
        std::string why;
        require(!sameState(before, after, why), "the cut changed the playlist");

        require(fx.tm->getCommandHistory().undo(), "undo is available (one step)");
        require(sameState(before, snapshot(*fx.tm), why), "undo restores every clip exactly: " + why);
        require(!fx.tm->getCommandHistory().canUndo(), "the whole cut was a single undo step");

        require(fx.tm->getCommandHistory().redo(), "redo is available");
        require(sameState(after, snapshot(*fx.tm), why), "redo reapplies exactly: " + why);
        require(fx.tm->getCommandHistory().undo(), "second undo");
        require(sameState(before, snapshot(*fx.tm), why), "second undo restores exactly: " + why);
    }
}

void testIgnoresNonAudioAndNoSilence(const std::string& dir) {
    std::printf("ignored: a missing clip and a clip with no silence change nothing...\n");
    Fixture fx = makeFixture(dir);
    const auto before = snapshot(*fx.tm);

    auto bogus = std::make_shared<DeleteSilenceCommand>(*fx.tm, std::vector<ClipInstanceID>{ClipInstanceID::generate()});
    fx.tm->getCommandHistory().pushAndExecute(bogus);
    require(!bogus->isUndoable(), "an unknown clip id is a no-op");

    // A clip too hot to have any quiet part: threshold -100 dB sees nothing quiet.
    SilenceParams p;
    p.autoThreshold = false;
    p.thresholdDb = -100.0;
    auto none = std::make_shared<DeleteSilenceCommand>(*fx.tm, std::vector<ClipInstanceID>{fx.voice}, SilenceRipple::ThisLane, p);
    fx.tm->getCommandHistory().pushAndExecute(none);
    require(!none->isUndoable(), "nothing under -100 dB means nothing to cut");
    std::string why;
    require(sameState(before, snapshot(*fx.tm), why), "playlist untouched: " + why);
    require(!fx.tm->getCommandHistory().canUndo(), "no undo entry was pushed for a no-op");
}

void testSeveralClipsOneUndo(const std::string& dir) {
    std::printf("selection: two clips on one lane are cut together and undone together...\n");
    Fixture fx = makeFixture(dir);
    const auto before = snapshot(*fx.tm);
    auto cmd = std::make_shared<DeleteSilenceCommand>(*fx.tm, std::vector<ClipInstanceID>{fx.voice, fx.laterSame}, SilenceRipple::ThisLane);
    fx.tm->getCommandHistory().pushAndExecute(cmd);
    require(cmd->cutCount() == 6, "three cuts in each clip; got " + std::to_string(cmd->cutCount()));
    const auto lane1 = laneClips(*fx.tm, fx.lane1);
    require(lane1.size() == 4, "two pieces from each clip; got " + std::to_string(lane1.size()));
    if (lane1.size() == 4) {
        // The second clip's first piece starts exactly where the gap (2 beats) after the first clip's last piece was.
        const double gap = lane1[2].startBeat - (lane1[1].startBeat + lane1[1].durationBeats);
        require(close(gap, 2.0 + 0.0, 1e-6), "the 2 beat gap between the clips survives; got " + std::to_string(gap));
    }
    fx.tm->getCommandHistory().undo();
    std::string why;
    require(sameState(before, snapshot(*fx.tm), why), "one undo restores both clips: " + why);
}

} // namespace

int main() {
    std::error_code ec;
    const std::string dir = (std::filesystem::temp_directory_path() / "aestra_delete_silence_test").string();
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    testThisLaneRippleAndPieces(dir);
    testAllLanesRipple(dir);
    testUndoRedoExact(dir);
    testIgnoresNonAudioAndNoSilence(dir);
    testSeveralClipsOneUndo(dir);
    std::filesystem::remove_all(dir, ec);
    if (g_failures == 0) {
        std::printf("DeleteSilenceCommandTest: all passed\n");
        return 0;
    }
    std::printf("DeleteSilenceCommandTest: %d failure(s)\n", g_failures);
    return 1;
}
