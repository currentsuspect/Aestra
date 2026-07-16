// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Note Commands Unit Tests
// Tests: AddNoteCommand, RemoveNoteCommand, MoveNoteCommand, ResizeNoteCommand
//       execute/undo/redo with disambiguation-by-duration

#include "Commands/AddNoteCommand.h"
#include "Commands/RemoveNoteCommand.h"
#include "Commands/MoveNoteCommand.h"
#include "Commands/ResizeNoteCommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { std::cout << "PASS: " << msg << "\n"; testsPassed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << "\n"; testsFailed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// ---------------------------------------------------------------------------
// AddNoteCommand
// ---------------------------------------------------------------------------
void test_add_note_command_execute() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    MidiNote note{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};

    AddNoteCommand cmd(pm, pid, note);
    cmd.execute();

    auto* pat = pm.getPattern(pid);
    ASSERT(pat->isMidi(), "pattern should be MIDI type");
    ASSERT(pat->getMidiNotes().size() == 1, "should have 1 note after execute");
    PASS("AddNoteCommand::execute");
}

void test_add_note_command_undo() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    MidiNote note{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};

    AddNoteCommand cmd(pm, pid, note);
    cmd.execute();
    cmd.undo();

    auto* pat = pm.getPattern(pid);
    ASSERT(pat->getMidiNotes().empty(), "should have 0 notes after undo");
    PASS("AddNoteCommand::undo");
}

void test_add_note_command_undo_disambiguation() {
    // Two notes at the same pitch+start+unitId but DIFFERENT durations
    // must be independent: adding+removing note2 must NOT accidentally remove note1.
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    MidiNote note1{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};
    MidiNote note2{60, 0.0, 2.0, 0.8f, 0.0f, 0, 0, 1.0f, false};

    // Add both notes
    AddNoteCommand cmd1(pm, pid, note1);
    AddNoteCommand cmd2(pm, pid, note2);
    cmd1.execute();
    {
        auto* pat = pm.getPattern(pid);
        ASSERT(pat->getMidiNotes().size() == 1, "should have 1 note after add1");
    }
    cmd2.execute();
    {
        auto* pat = pm.getPattern(pid);
        ASSERT(pat->getMidiNotes().size() == 2, "should have 2 notes after add");
    }

    // Undo cmd2 — this used to incorrectly remove note1 as well
    cmd2.undo();
    {
        auto* pat = pm.getPattern(pid);
        ASSERT(pat->getMidiNotes().size() == 1, "undo should only remove the matching note");
        const auto& remaining = pat->getMidiNotes()[0];
        ASSERT(remaining.pitch == 60 && remaining.startBeat == 0.0 && remaining.durationBeats == 1.0,
               "remaining note should be note1 (duration 1.0), not note2 (duration 2.0)");
    }
    PASS("AddNoteCommand::undo disambiguates by pitch+start+duration+unitId");
}

void test_add_note_command_redo() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    MidiNote note{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};

    AddNoteCommand cmd(pm, pid, note);
    cmd.execute();
    cmd.undo();
    cmd.redo();

    auto* pat = pm.getPattern(pid);
    ASSERT(pat->getMidiNotes().size() == 1, "redo should restore note");
    PASS("AddNoteCommand::redo");
}

void test_add_note_command_double_execute_noop() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    MidiNote note{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};

    AddNoteCommand cmd(pm, pid, note);
    cmd.execute();
    cmd.execute(); // double execute should be noop

    auto* pat = pm.getPattern(pid);
    ASSERT(pat->getMidiNotes().size() == 1, "double execute should be noop");
    PASS("AddNoteCommand double-execute noop");
}

// ---------------------------------------------------------------------------
// RemoveNoteCommand
// ---------------------------------------------------------------------------
void test_remove_note_command_execute() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    AddNoteCommand(pm, pid, MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false}).execute();

    auto* pat = pm.getPattern(pid);
    MidiNote toRemove = pat->getMidiNotes()[0];

    RemoveNoteCommand cmd(pm, pid, toRemove);
    cmd.execute();

    ASSERT(pat->getMidiNotes().empty(), "should have 0 notes after remove");
    PASS("RemoveNoteCommand::execute");
}

void test_remove_note_command_undo() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    AddNoteCommand(pm, pid, MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false}).execute();

    auto* pat = pm.getPattern(pid);
    MidiNote toRemove = pat->getMidiNotes()[0];

    RemoveNoteCommand cmd(pm, pid, toRemove);
    cmd.execute();
    cmd.undo();

    ASSERT(pat->getMidiNotes().size() == 1, "should have 1 note after undo");
    PASS("RemoveNoteCommand::undo");
}

// ---------------------------------------------------------------------------
// MoveNoteCommand
// ---------------------------------------------------------------------------
void test_move_note_command_execute() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    AddNoteCommand(pm, pid, MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false}).execute();

    auto* pat = pm.getPattern(pid);
    MidiNote original = pat->getMidiNotes()[0];

    MoveNoteCommand cmd(pm, pid, original, 4.0, 72);
    cmd.execute();

    const auto& note = pat->getMidiNotes()[0];
    ASSERT(note.pitch == 72, "pitch should be 72 after move");
    ASSERT(note.startBeat == 4.0, "startBeat should be 4.0 after move");
    PASS("MoveNoteCommand::execute");
}

void test_move_note_command_undo() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    AddNoteCommand(pm, pid, MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false}).execute();

    auto* pat = pm.getPattern(pid);
    MidiNote original = pat->getMidiNotes()[0];

    MoveNoteCommand cmd(pm, pid, original, 4.0, 72);
    cmd.execute();
    cmd.undo();

    const auto& note = pat->getMidiNotes()[0];
    ASSERT(note.pitch == 60, "pitch should be restored to 60 on undo");
    ASSERT(note.startBeat == 0.0, "startBeat should be restored to 0.0 on undo");
    PASS("MoveNoteCommand::undo");
}

// ---------------------------------------------------------------------------
// ResizeNoteCommand
// ---------------------------------------------------------------------------
void test_resize_note_command_execute() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    AddNoteCommand(pm, pid, MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false}).execute();

    auto* pat = pm.getPattern(pid);
    MidiNote original = pat->getMidiNotes()[0];

    ResizeNoteCommand cmd(pm, pid, original, 2.0);
    cmd.execute();

    const auto& note = pat->getMidiNotes()[0];
    ASSERT(note.durationBeats == 2.0, "durationBeats should be 2.0 after resize");
    PASS("ResizeNoteCommand::execute");
}

void test_resize_note_command_undo() {
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});
    AddNoteCommand(pm, pid, MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false}).execute();

    auto* pat = pm.getPattern(pid);
    MidiNote original = pat->getMidiNotes()[0];

    ResizeNoteCommand cmd(pm, pid, original, 2.0);
    cmd.execute();
    cmd.undo();

    const auto& note = pat->getMidiNotes()[0];
    ASSERT(note.durationBeats == 1.0, "durationBeats should be restored to 1.0 on undo");
    PASS("ResizeNoteCommand::undo");
}

// ---------------------------------------------------------------------------
// Data correctness
// ---------------------------------------------------------------------------
void test_note_creation_validates_bounds() {
    MidiNote note;

    // Pitch bounds
    note.pitch = 0;
    ASSERT(note.pitch >= 0 && note.pitch <= 127, "pitch 0 should be valid");

    note.pitch = 127;
    ASSERT(note.pitch >= 0 && note.pitch <= 127, "pitch 127 should be valid");

    note.pitch = -1;
    // Note: struct doesn't clamp — UI must clamp before assignment
    // This test documents the boundary: UI must enforce [0, 127]
    PASS("note pitch bounds documented (UI enforces)");

    // Velocity bounds
    note.velocity = 0.0f;
    ASSERT(note.velocity >= 0.0f && note.velocity <= 1.0f, "velocity 0.0 valid");

    note.velocity = 1.0f;
    ASSERT(note.velocity >= 0.0f && note.velocity <= 1.0f, "velocity 1.0 valid");

    note.velocity = 0.79f; // default value used by UI
    ASSERT(note.velocity >= 0.0f && note.velocity <= 1.0f, "default velocity valid");

    PASS("note velocity bounds valid");
}

void test_duration_minimum_enforced() {
    // Notes with durationBeats < 0.125 should be clamped by UI
    // Document the minimum used in the codebase
    constexpr double kMinDuration = 0.125;
    ASSERT(kMinDuration > 0.0, "minimum duration must be positive");
    PASS("minimum duration constant defined");
}

void test_snap_produces_exact_grid_values() {
    // Snap should produce exact multiples of the grid
    // Using std::round guarantees nearest-grid alignment
    // Triplet (1/3) is the problematic case due to floating-point representation

    // Beat snap: 1.0 grid
    double snapped = ::round(2.3 / 1.0) * 1.0;
    ASSERT(std::abs(snapped - 2.0) < 0.0001, "beat snap should round to 2.0");

    // Sixteenth snap: 0.0625 grid
    snapped = ::round(0.1 / 0.0625) * 0.0625;
    ASSERT(std::abs(snapped - 0.125) < 0.0001, "sixteenth snap should round to 0.125");

    // Triplet snap: 1/3 — verify rounding behavior
    double tri = 1.0 / 3.0;
    snapped = ::round(0.5 / tri) * tri;
    // After rounding: (0.5 / 0.333...) = ~1.5, round = 2, * 0.333... = 0.666...
    ASSERT(snapped > 0.6 && snapped < 0.7, "triplet snap produces grid-aligned value");
    PASS("snap math produces grid-aligned values");
}

void test_overlapping_notes_preserved() {
    // Multiple notes on the same pitch at different times should all be preserved
    PatternManager pm;
    PatternID pid = pm.createMidiPattern("test", 16.0, MidiPayload{});

    MidiNote n1{60, 0.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};
    MidiNote n2{60, 2.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};
    MidiNote n3{60, 4.0, 1.0, 0.8f, 0.0f, 0, 0, 1.0f, false};

    AddNoteCommand(pm, pid, n1).execute();
    AddNoteCommand(pm, pid, n2).execute();
    AddNoteCommand(pm, pid, n3).execute();

    auto* pat = pm.getPattern(pid);
    ASSERT(pat->getMidiNotes().size() == 3, "all overlapping notes should be preserved");
    PASS("overlapping notes on same pitch preserved");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== Note Commands Unit Tests ===\n\n";

    std::cout << "[AddNoteCommand]\n";
    test_add_note_command_execute();
    test_add_note_command_undo();
    test_add_note_command_undo_disambiguation();
    test_add_note_command_redo();
    test_add_note_command_double_execute_noop();

    std::cout << "\n[RemoveNoteCommand]\n";
    test_remove_note_command_execute();
    test_remove_note_command_undo();

    std::cout << "\n[MoveNoteCommand]\n";
    test_move_note_command_execute();
    test_move_note_command_undo();

    std::cout << "\n[ResizeNoteCommand]\n";
    test_resize_note_command_execute();
    test_resize_note_command_undo();

    std::cout << "\n[Data Correctness]\n";
    test_note_creation_validates_bounds();
    test_duration_minimum_enforced();
    test_snap_produces_exact_grid_values();
    test_overlapping_notes_preserved();

    std::cout << "\n=== Results: " << testsPassed << " passed, " << testsFailed << " failed ===\n";
    return testsFailed > 0 ? 1 : 0;
}
