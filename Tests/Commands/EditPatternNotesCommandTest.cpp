// © 2026 Aestra Studios — All Rights Reserved.
// EditPatternNotesCommand Unit Tests
// Tests: whole-vector notes swap — already-executed push (undo/redo roundtrip),
// field/order fidelity, CommandHistory integration, non-MIDI pattern safety.
// Regression for #822: Arsenal grid gestures must be undoable.

#include "Commands/EditPatternNotesCommand.h"
#include "Commands/CommandHistory.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <cassert>
#include <iostream>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { std::cout << "PASS: " << msg << "\n"; testsPassed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << "\n"; testsFailed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

namespace {

MidiNote makeNote(int pitch, double startBeat, uint64_t unitId) {
    MidiNote note;
    note.pitch = pitch;
    note.startBeat = startBeat;
    note.durationBeats = 0.25;
    note.velocity = 0.8f;
    note.pan = -0.5f;
    note.unitId = unitId;
    note.gate = 0.5f;
    note.slide = true;
    return note;
}

} // namespace

void test_already_executed_undo_redo_roundtrip() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 4.0, MidiPayload{});
    const MidiNote first = makeNote(60, 0.0, 7);
    {
        pm.applyPatch(pid, [&first](PatternSource& p) {
            std::get<MidiPayload>(p.payload).notes.push_back(first);
        });
    }
    const auto before = std::get<MidiPayload>(pm.getPattern(pid)->payload).notes;

    // The gesture path: mutate directly, then push the command as executed.
    const MidiNote second = makeNote(64, 1.0, 7);
    pm.applyPatch(pid, [&second](PatternSource& p) {
        std::get<MidiPayload>(p.payload).notes.push_back(second);
    });

    EditPatternNotesCommand cmd(pm, pid, before,
                                std::get<MidiPayload>(pm.getPattern(pid)->payload).notes, "Paint Steps");
    ASSERT(std::get<MidiPayload>(pm.getPattern(pid)->payload).notes.size() == 2,
           "pattern holds both notes after direct mutation");

    cmd.undo();
    {
        const auto& notes = std::get<MidiPayload>(pm.getPattern(pid)->payload).notes;
        ASSERT(notes.size() == 1, "undo restores the before vector");
        ASSERT(notes[0].pitch == 60 && notes[0].startBeat == 0.0 && notes[0].unitId == 7,
               "undo restores the exact remaining note");
    }

    cmd.redo();
    {
        const auto& notes = std::get<MidiPayload>(pm.getPattern(pid)->payload).notes;
        ASSERT(notes.size() == 2, "redo re-applies the after vector");
        ASSERT(notes[1].pan == -0.5f && notes[1].gate == 0.5f && notes[1].slide,
               "redo preserves every note field, not just pitch/start");
    }
    PASS("already-executed undo/redo roundtrip");
}

void test_order_fidelity() {
    // Undo must restore exact vector order: scheduling and step occupancy
    // checks scan linearly with topmost-wins semantics.
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 4.0, MidiPayload{});
    const std::vector<MidiNote> ordered{makeNote(60, 0.0, 1), makeNote(62, 0.0, 2), makeNote(64, 0.0, 3)};
    pm.applyPatch(pid, [&ordered](PatternSource& p) {
        std::get<MidiPayload>(p.payload).notes = ordered;
    });

    std::vector<MidiNote> reversed{ordered.rbegin(), ordered.rend()};
    pm.applyPatch(pid, [&reversed](PatternSource& p) { std::get<MidiPayload>(p.payload).notes = reversed; });
    EditPatternNotesCommand cmd(pm, pid, ordered, reversed, "Reorder");
    ASSERT(std::get<MidiPayload>(pm.getPattern(pid)->payload).notes.front().pitch == 64,
           "after-vector held verbatim");
    cmd.undo();
    ASSERT(std::get<MidiPayload>(pm.getPattern(pid)->payload).notes.front().pitch == 60,
           "undo restores original order, not a canonical sort");
    cmd.redo();
    ASSERT(std::get<MidiPayload>(pm.getPattern(pid)->payload).notes.front().pitch == 64,
           "redo re-applies the exact after order");
    PASS("vector order fidelity");
}

void test_command_history_integration() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 4.0, MidiPayload{});
    CommandHistory history;

    const auto before = std::vector<MidiNote>{makeNote(60, 0.0, 7)};
    pm.applyPatch(pid, [&before](PatternSource& p) { std::get<MidiPayload>(p.payload).notes = before; });

    // Gesture: add a note, push as executed.
    std::vector<MidiNote> after = before;
    after.push_back(makeNote(67, 2.0, 7));
    pm.applyPatch(pid, [&after](PatternSource& p) { std::get<MidiPayload>(p.payload).notes = after; });
    history.pushExecuted(std::make_shared<EditPatternNotesCommand>(pm, pid, before, after, "Place Step"));
    ASSERT(history.canUndo(), "pushed gesture is undoable");

    history.undo();
    ASSERT(std::get<MidiPayload>(pm.getPattern(pid)->payload).notes.size() == 1, "history.undo restores before");

    history.redo();
    ASSERT(std::get<MidiPayload>(pm.getPattern(pid)->payload).notes.size() == 2, "history.redo restores after");
    PASS("CommandHistory integration");
}

void test_non_midi_pattern_is_safe_noop() {
    PatternManager pm;
    PatternID pid; // invalid / no pattern backing
    std::vector<MidiNote> before{makeNote(60, 0.0, 7)};

    EditPatternNotesCommand cmd(pm, pid, before, {}, "Edit Notes");
    cmd.undo(); // must not crash or corrupt state
    cmd.redo();
    cmd.undo();
    PASS("invalid/non-midi target is a safe no-op");
}

int main() {
    test_already_executed_undo_redo_roundtrip();
    test_order_fidelity();
    test_command_history_integration();
    test_non_midi_pattern_is_safe_noop();

    std::cout << "\nEditPatternNotesCommandTest: " << testsPassed << " passed, " << testsFailed << " failed\n";
    return testsFailed == 0 ? 0 : 1;
}
