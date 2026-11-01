// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A note drawn in the piano roll on an 808 (pitched sampler) unit must sound for the length
// it was drawn. Owner report (2026-11-01): a theme sketch's piano melody, written as notes on
// an 808 unit, was cut to a short blip on every note. The engine timed every pitched-sampler
// note from the STEP gate (0.25 beat x gate) and ignored its duration, so a 4-beat note
// stopped after a quarter beat.
//
// Step-grid notes keep their gate: a one-step note (0.25 beat) still ends at 0.25 x gate,
// longer or shorter. Only a note drawn at another length takes its own length.
//
// Runs the REAL scheduler at MIDI level, block by block, and reads where the note-off lands.

#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr int kSampleRate = 48000;
constexpr double kBeatFrames = 24000.0; // 120 BPM
constexpr uint32_t kBlock = 128;
constexpr uint64_t kTol = 2 * kBlock;
constexpr uint8_t kPitch = 60;

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cout << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

uint64_t beatFrame(double beat) { return static_cast<uint64_t>(beat * kBeatFrames); }

// Not "near": <windef.h> defines near/far as empty macros, which broke the MSVC build.
bool landsNear(uint64_t frame, double beat) {
    const uint64_t want = beatFrame(beat);
    return (frame > want ? frame - want : want - frame) <= kTol;
}

struct Fixture {
    TrackManager tm;
    UnitID unit{0};
    std::vector<uint64_t> offs;
    uint64_t frame = 0;

    bool build(const std::vector<MidiNote>& notes) {
        unit = tm.getUnitManager().createUnit("808", UnitType::PitchedSampler);
        if (unit == 0) {
            return false;
        }
        auto& patterns = tm.getPatternManager();
        const PatternID id = patterns.createPattern();
        auto* p = patterns.getPattern(id);
        p->type = PatternSource::Type::Midi;
        p->name = "Lengths";
        p->lengthBeats = 16.0;
        p->payload = MidiPayload{};
        for (MidiNote n : notes) {
            n.unitId = unit;
            std::get<MidiPayload>(p->payload).notes.push_back(n);
        }
        ClipInstance clip;
        clip.patternId = id;
        clip.sourceId = id.value;
        clip.startBeat = 0.0;
        clip.durationBeats = 16.0;
        clip.sourceOffset = 0.0;
        const auto lane = tm.getPlaylistModel().createLane("Lengths");
        const auto placed = tm.getPlaylistModel().addClip(lane, clip);
        tm.play();
        return placed.isValid() && tm.isPlaying();
    }

    void renderTo(double beat) {
        auto& playback = tm.getPatternPlaybackEngine();
        PatternPlaybackEngine::UnitMidiRoute route{unit, nullptr};
        const uint64_t end = beatFrame(beat);
        while (frame < end) {
            playback.refillWindow(frame, kSampleRate, 4096);
            MidiBuffer buffer;
            route.midiBuffer = &buffer;
            playback.processAudio(frame, kBlock, &route, 1);
            for (size_t i = 0; i < buffer.getEventCount(); ++i) {
                const auto& e = buffer.getEvent(i);
                const uint8_t status = e.data[0] & 0xF0;
                if (status == 0x80 || (status == 0x90 && e.data[2] == 0)) {
                    offs.push_back(frame);
                }
            }
            frame += kBlock;
        }
    }
};

MidiNote note(double start, double length, float gate = 1.0f) {
    MidiNote n{kPitch, start, length, 1.0f, 0.0f, 0};
    n.gate = gate;
    return n;
}

// THE report: a 4-beat note drawn in the piano roll sounds for 4 beats.
void testDrawnLengthIsHeld() {
    Fixture f;
    if (!f.build({note(0.0, 4.0)})) {
        check(false, "fixture");
        return;
    }
    f.renderTo(6.0);
    check(f.offs.size() == 1, "one note-off for one note (got " + std::to_string(f.offs.size()) + ")");
    check(!f.offs.empty() && landsNear(f.offs.front(), 4.0),
          "a 4-beat 808 note ends at beat 4, not after one step (beat 0.25)");
}

// A drawn note shorter than a step is short too, not stretched to a step.
void testDrawnShortNoteIsShort() {
    Fixture f;
    if (!f.build({note(0.0, 0.1)})) {
        check(false, "fixture");
        return;
    }
    f.renderTo(2.0);
    check(!f.offs.empty() && landsNear(f.offs.front(), 0.1), "a 0.1-beat 808 note ends at beat 0.1");
}

// Step-grid notes keep the gate behaviour the Arsenal relies on.
void testStepNotesKeepTheirGate() {
    {
        Fixture f;
        if (!f.build({note(0.0, 0.25, 2.0f)})) {
            check(false, "fixture");
            return;
        }
        f.renderTo(2.0);
        check(!f.offs.empty() && landsNear(f.offs.front(), 0.5), "a step with gate 2 still rings to beat 0.5");
    }
    {
        Fixture f;
        if (!f.build({note(0.0, 0.25, 0.5f)})) {
            check(false, "fixture");
            return;
        }
        f.renderTo(2.0);
        check(!f.offs.empty() && landsNear(f.offs.front(), 0.125), "a step with gate 0.5 still ends at beat 0.125");
    }
}

// A ratcheted step splits its length AND keeps its gate per hit: gate 2, ratchet 2 = two hits of
// 0.125 beat, each held for 0.25. The first hit's note-off lands at beat 0.25, as before the fix.
void testRatchetedStepKeepsItsGatePerHit() {
    Fixture f;
    MidiNote n = note(0.0, 0.25, 2.0f);
    n.ratchet = 2;
    if (!f.build({n})) {
        check(false, "fixture");
        return;
    }
    f.renderTo(2.0);
    check(f.offs.size() == 2, "two ratchet hits, two note-offs (got " + std::to_string(f.offs.size()) + ")");
    check(!f.offs.empty() && landsNear(f.offs.front(), 0.25), "the first hit of a gate-2 ratchet ends at beat 0.25");
}

// A gate can now hold a note past its drawn length (gate 2 = twice it). In a clip that repeats its
// pattern, each repeat's note must still get its note-off, even one that lands after the repeat
// has ended (review, #1325): a 4-beat pattern, a 3-beat note at gate 2 = off 6 beats after each start.
void testRepeatingClipReleasesALongGatedNote() {
    TimelineClock clock{120.0};
    PatternManager patterns;
    UnitManager units;
    PatternPlaybackEngine engine(&clock, &patterns, &units);
    const UnitID unit = units.createUnit("808", UnitType::PitchedSampler);
    MidiPayload payload;
    MidiNote n = note(0.0, 3.0, 2.0f);
    n.unitId = unit;
    payload.notes.push_back(n);
    const PatternID id = patterns.createMidiPattern("Long gate", 4.0, payload);
    engine.schedulePatternInstance(id, 0.0, 2, 0.0, 12.0, true);

    std::vector<uint64_t> ons, offs;
    MidiBuffer buffer;
    PatternPlaybackEngine::UnitMidiRoute route{unit, &buffer};
    bool edited = false;
    for (uint64_t frame = 0; frame < beatFrame(20.0); frame += kBlock) {
        // A content edit while the first repeat's note still sounds (beat 5.5, its off is at 6):
        // the live-edit reconciliation must still find that note and keep its end.
        if (!edited && frame >= beatFrame(5.5)) {
            engine.patternContentEdited();
            edited = true;
        }
        engine.refillWindow(frame, kSampleRate, kSampleRate / 4, 0);
        buffer.clear();
        engine.processAudio(frame, kBlock, &route, 1);
        for (size_t i = 0; i < buffer.getEventCount(); ++i) {
            const auto& e = buffer.getEvent(i);
            const uint8_t status = e.data[0] & 0xF0;
            if (status == 0x90 && e.data[2] > 0) ons.push_back(frame + e.sampleOffset);
            if (status == 0x80 || (status == 0x90 && e.data[2] == 0)) offs.push_back(frame + e.sampleOffset);
        }
    }
    check(ons.size() == 3, "three repeats, three note-ons (got " + std::to_string(ons.size()) + ")");
    check(offs.size() == ons.size(), "every repeat's note gets its note-off (got " + std::to_string(offs.size()) + ")");
    check(!offs.empty() && landsNear(offs.front(), 6.0), "the first repeat's note ends at beat 6, after its repeat, even with an edit at beat 5.5");
}

// The 808 is monophonic: the next step still cuts a long note, as a new hit does.
void testNextStepStillCutsALongNote() {
    Fixture f;
    if (!f.build({note(0.0, 4.0), note(0.25, 0.25)})) {
        check(false, "fixture");
        return;
    }
    f.renderTo(6.0);
    check(!f.offs.empty() && landsNear(f.offs.front(), 0.25), "a long note is cut where the next step starts");
}

} // namespace

int main() {
    testDrawnLengthIsHeld();
    testDrawnShortNoteIsShort();
    testStepNotesKeepTheirGate();
    testRatchetedStepKeepsItsGatePerHit();
    testRepeatingClipReleasesALongGatedNote();
    testNextStepStillCutsALongNote();

    if (g_failures == 0) {
        std::cout << "Pitched sampler note length tests passed\n";
        return 0;
    }
    std::cout << g_failures << " pitched sampler note length test(s) failed\n";
    return 1;
}
