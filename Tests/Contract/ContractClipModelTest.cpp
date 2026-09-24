// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Ownership contract — clip model invariants (contract:ownership).
//
// Modes (argv[1]):
//   split-referent      guards F8   §3.2 clip referent = patternId; isPatternUsed sees every referenced pattern
//   split-undo-exact    guards F6,F7 §3.5 undo restores captured state exactly; creating commands detach on undo
//   midi-offset         guards F24  §3.4 a clip window [sourceOffset, +duration) plays at clipStart - sourceOffset + t
//   midi-split-offset   guards F10  §3.4/D1 splitting an offset MIDI clip leaves the audible notes unchanged
//   midi-left-trim      guards F11  §3.4 left-trimming a MIDI clip hides notes; it does not slide them
//   identical-notes     guards F4   characterization: identical notes stay deterministic and uncorrupted
//
// Expected outcomes are the contract (Aestra-Internals: Ownership & Reference
// Contract, rev 3), not the current behavior. Nothing here changes production code.

#include "../Contract/ContractSupport.h"
#include "Commands/MoveNoteCommand.h"
#include "Commands/SplitClipCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

using namespace Aestra::Audio;
using AestraContract::contractSetup;
using AestraContract::str;
using AestraContract::Verdict;

namespace {

constexpr int kSampleRate = 48000;
constexpr double kBeatFrames = 24000.0; // 120 BPM at 48 kHz
constexpr uint64_t kTolerance = 128;    // one scheduling block

std::vector<ClipInstance> clipsOnLane(PlaylistModel& playlist, const PlaylistLaneID& lane) {
    const auto* l = playlist.getLane(lane);
    contractSetup(l != nullptr, "lane missing");
    return l->clips;
}

// ---------------------------------------------------------------- F8
int splitReferent() {
    Verdict v("F8");
    TrackManager tm;
    auto& pm = tm.getPatternManager();
    auto& playlist = tm.getPlaylistModel();
    playlist.setPatternManager(&pm);

    MidiPayload payload;
    for (int i = 0; i < 4; ++i) {
        payload.notes.push_back(MidiNote{60 + i, static_cast<double>(i), 0.5, 0.8f, 0.0f, 1});
    }
    const PatternID pid = pm.createMidiPattern("Referent", 4.0, payload);
    const PlaylistLaneID lane = playlist.createLane("A");
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.patternId = pid;
    clip.sourceId = pid.value;
    clip.startBeat = 0.0;
    clip.durationBeats = 4.0;
    contractSetup(playlist.addClip(lane, clip).isValid(), "addClip failed");

    SplitClipCommand split(playlist, clip.id, 2.0);
    split.execute();
    const auto clips = clipsOnLane(playlist, lane);
    contractSetup(clips.size() == 2, "split did not produce two clips");

    for (const auto& c : clips) {
        v.check(playlist.isPatternUsed(c.patternId),
                "isPatternUsed(" + str(c.patternId.value) + ") for a clip that references it");
    }
    return v.finish();
}

// ---------------------------------------------------------------- F6, F7
int splitUndoExact() {
    Verdict v("F6,F7");
    TrackManager tm;
    auto& pm = tm.getPatternManager();
    auto& playlist = tm.getPlaylistModel();
    playlist.setPatternManager(&pm);
    playlist.setBPM(120.0);

    AudioSlicePayload payload;
    payload.audioSourceId = ClipSourceID(1);
    payload.durationSeconds = 4.0;
    const PatternID pid = pm.createAudioPattern("Varispeed", 8.0, payload);
    const PlaylistLaneID lane = playlist.createLane("A");
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.patternId = pid;
    clip.sourceId = pid.value;
    clip.startBeat = 0.0;
    clip.durationBeats = 8.0;
    clip.durationSeconds = playlist.beatToSeconds(8.0);
    contractSetup(playlist.addClip(lane, clip).isValid(), "addClip failed");

    // Speed x2 through the model API, which maintains the canonical
    // durationSeconds == beatToSeconds(durationBeats) / varispeed invariant (#746).
    ClipEdits edits = ClipEdits::forNewAudioClip();
    edits.playbackRate = 2.0f;
    contractSetup(playlist.setClipEdits(clip.id, edits), "setClipEdits failed");

    const ClipInstance before = *playlist.getClip(clip.id);
    const size_t patternsBefore = pm.getAllPatterns().size();
    const size_t clipsBefore = clipsOnLane(playlist, lane).size();

    SplitClipCommand split(playlist, clip.id, 4.0);
    split.execute();
    contractSetup(clipsOnLane(playlist, lane).size() == clipsBefore + 1, "split did not add a clip");
    split.undo();

    const ClipInstance* after = playlist.getClip(clip.id);
    contractSetup(after != nullptr, "original clip missing after undo");
    v.check(clipsOnLane(playlist, lane).size() == clipsBefore, "clip count restored");
    v.check(after->startBeat == before.startBeat, "startBeat restored");
    v.check(after->durationBeats == before.durationBeats, "durationBeats restored");
    v.check(after->durationSeconds == before.durationSeconds,
            "durationSeconds restored exactly (before=" + str(before.durationSeconds) +
                " after=" + str(after->durationSeconds) + ")");
    v.check(after->sourceOffset == before.sourceOffset, "sourceOffset restored");
    v.check(after->sourceOffsetSeconds == before.sourceOffsetSeconds, "sourceOffsetSeconds restored");
    v.check(after->patternId == before.patternId, "patternId restored");
    v.check(after->edits == before.edits, "edits restored");
    v.check(pm.getAllPatterns().size() == patternsBefore, "pattern count restored (before=" + str(patternsBefore) +
                                                              " after=" + str(pm.getAllPatterns().size()) + ")");
    return v.finish();
}

// ---------------------------------------------------------------- scheduler observation
struct NoteOn {
    double beat;
    int pitch;
};

// What the pattern scheduler actually emits for the current timeline, from beat 0.
std::vector<NoteOn> scheduledNoteOns(TrackManager& tm, UnitID unit, double beats) {
    auto& playback = tm.getPatternPlaybackEngine();
    playback.clearScheduledInstances();
    tm.play();
    const uint64_t totalFrames = static_cast<uint64_t>(beats * kBeatFrames);
    playback.refillWindow(0, kSampleRate, totalFrames);

    PatternPlaybackEngine::UnitMidiRoute route{unit, nullptr};
    std::vector<NoteOn> hits;
    constexpr uint32_t kBlock = 128;
    for (uint64_t frame = 0; frame < totalFrames; frame += kBlock) {
        MidiBuffer buffer;
        route.midiBuffer = &buffer;
        playback.processAudio(frame, kBlock, &route, 1);
        for (size_t i = 0; i < buffer.getEventCount(); ++i) {
            const auto& e = buffer.getEvent(i);
            if ((e.data[0] & 0xF0) == 0x90 && e.data[2] > 0) {
                hits.push_back({static_cast<double>(frame + e.sampleOffset) / kBeatFrames, e.data[1]});
            }
        }
    }
    tm.stop();
    return hits;
}

void compareNoteOns(Verdict& v, const std::vector<NoteOn>& expected, const std::vector<NoteOn>& actual,
                    const std::string& label) {
    auto describe = [](const std::vector<NoteOn>& notes) {
        std::string s;
        for (const auto& n : notes) {
            s += " " + str(n.pitch) + "@" + str(std::round(n.beat * 100.0) / 100.0);
        }
        return s.empty() ? std::string(" (none)") : s;
    };
    bool same = expected.size() == actual.size();
    for (size_t i = 0; same && i < expected.size(); ++i) {
        same = expected[i].pitch == actual[i].pitch &&
               std::abs(expected[i].beat - actual[i].beat) * kBeatFrames <= static_cast<double>(kTolerance);
    }
    v.check(same, label + ": expected" + describe(expected) + " | scheduled" + describe(actual));
}

struct MidiSession {
    TrackManager tm;
    UnitID unit{0};
    PatternID pattern;
    PlaylistLaneID lane;
};

// Eight one-beat-apart notes: pattern beat i plays pitch 60+i.
void buildMidiSession(MidiSession& s) {
    auto& pm = s.tm.getPatternManager();
    s.tm.getPlaylistModel().setPatternManager(&pm);
    s.unit = s.tm.getUnitManager().createUnit("Window Unit", UnitType::Sampler);
    contractSetup(s.unit != 0, "createUnit failed");
    MidiPayload payload;
    for (int i = 0; i < 8; ++i) {
        payload.notes.push_back(MidiNote{60 + i, static_cast<double>(i), 0.5, 1.0f, 0.0f, s.unit});
    }
    s.pattern = pm.createMidiPattern("Window", 8.0, payload);
    s.lane = s.tm.getPlaylistModel().createLane("A");
}

ClipInstanceID addMidiClip(MidiSession& s, double start, double duration, double sourceOffset) {
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.patternId = s.pattern;
    clip.sourceId = s.pattern.value;
    clip.startBeat = start;
    clip.durationBeats = duration;
    clip.sourceOffset = sourceOffset;
    const ClipInstanceID id = s.tm.getPlaylistModel().addClip(s.lane, clip);
    contractSetup(id.isValid(), "addClip failed");
    return id;
}

// ---------------------------------------------------------------- F24
// A clip at 10 showing pattern window [2, 8) plays pattern beat t at 10 - 2 + t:
// pitches 62..67 at beats 10..15. (sourceOffset is a persisted field; legacy
// projects and the split fallback already produce it.)
int midiOffset() {
    Verdict v("F24");
    MidiSession s;
    buildMidiSession(s);
    addMidiClip(s, 10.0, 6.0, 2.0);
    std::vector<NoteOn> expected;
    for (int i = 2; i < 8; ++i) {
        expected.push_back({10.0 - 2.0 + i, 60 + i});
    }
    compareNoteOns(v, expected, scheduledNoteOns(s.tm, s.unit, 20.0), "offset clip");
    return v.finish();
}

// ---------------------------------------------------------------- F10
int midiSplitOffset() {
    Verdict v("F10");
    MidiSession s;
    buildMidiSession(s);
    const ClipInstanceID id = addMidiClip(s, 10.0, 6.0, 2.0);
    const auto unsplit = scheduledNoteOns(s.tm, s.unit, 20.0);

    SplitClipCommand split(s.tm.getPlaylistModel(), id, 13.0);
    split.execute();
    contractSetup(clipsOnLane(s.tm.getPlaylistModel(), s.lane).size() == 2, "split did not produce two clips");
    compareNoteOns(v, unsplit, scheduledNoteOns(s.tm, s.unit, 20.0), "split halves vs unsplit clip");
    return v.finish();
}

// ---------------------------------------------------------------- F11
// A clip at 0 over pattern [0, 8), left-trimmed to start at beat 2: the notes
// that remain audible are the ones that were already at beats 2..7.
int midiLeftTrim() {
    Verdict v("F11");
    MidiSession s;
    buildMidiSession(s);
    const ClipInstanceID id = addMidiClip(s, 0.0, 8.0, 0.0);

    TrimClipCommand trim(s.tm.getPlaylistModel(), id, 2.0, -1.0);
    trim.execute();
    const ClipInstance* clip = s.tm.getPlaylistModel().getClip(id);
    contractSetup(clip && clip->startBeat == 2.0 && clip->durationBeats == 6.0, "trim did not apply");

    std::vector<NoteOn> expected;
    for (int i = 2; i < 8; ++i) {
        expected.push_back({static_cast<double>(i), 60 + i});
    }
    compareNoteOns(v, expected, scheduledNoteOns(s.tm, s.unit, 12.0), "left-trimmed clip");
    return v.finish();
}

// ---------------------------------------------------------------- F4 (characterization)
// Two identical notes (same pitch/start/unit). NoteID is deferred, so this only
// pins that value-matched commands stay deterministic and never corrupt the
// pattern: moving "one of them" moves exactly one, and undo restores both.
int identicalNotes() {
    Verdict v("F4");
    PatternManager pm;
    MidiPayload payload;
    const MidiNote twin{64, 1.0, 1.0, 0.8f, 0.0f, 7};
    payload.notes = {twin, twin, MidiNote{67, 2.0, 1.0, 0.8f, 0.0f, 7}};
    const PatternID pid = pm.createMidiPattern("Twins", 4.0, payload);
    auto notes = [&]() { return pm.getPattern(pid)->getMidiNotes(); };

    MoveNoteCommand move(pm, pid, twin, 3.0, 64);
    move.execute();
    const auto moved = notes();
    const auto countAt = [](const std::vector<MidiNote>& ns, int pitch, double start) {
        return std::count_if(ns.begin(), ns.end(),
                             [&](const MidiNote& n) { return n.pitch == pitch && n.startBeat == start; });
    };
    v.check(moved.size() == 3, "note count preserved after move (" + str(moved.size()) + ")");
    v.check(countAt(moved, 64, 1.0) == 1 && countAt(moved, 64, 3.0) == 1,
            "exactly one of the twins moved (at 1.0: " + str(countAt(moved, 64, 1.0)) +
                ", at 3.0: " + str(countAt(moved, 64, 3.0)) + ")");
    v.check(countAt(moved, 67, 2.0) == 1, "unrelated note untouched");

    move.undo();
    const auto undone = notes();
    v.check(undone.size() == 3 && countAt(undone, 64, 1.0) == 2 && countAt(undone, 64, 3.0) == 0,
            "undo restores both twins");
    return v.finish();
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = AestraContract::contractMode(argc, argv);
    if (mode == "split-referent")
        return splitReferent();
    if (mode == "split-undo-exact")
        return splitUndoExact();
    if (mode == "midi-offset")
        return midiOffset();
    if (mode == "midi-split-offset")
        return midiSplitOffset();
    if (mode == "midi-left-trim")
        return midiLeftTrim();
    if (mode == "identical-notes")
        return identicalNotes();
    contractSetup(false, "unknown mode " + mode);
    return 2;
}
