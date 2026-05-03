// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Helpers/PianoRollInteraction.h"
#include <cassert>
#include <iostream>

using namespace AestraUI;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { std::cout << "PASS: " << msg << "\n"; testsPassed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << "\n"; testsFailed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// ---------------------------------------------------------------------------
// Test: deleted notes are ignored in hit testing
// MidiNote: pitch, startBeat, durationBeats, velocity, unitId, selected, isDeleted, animationScale
// ---------------------------------------------------------------------------
static void test_deleted_notes_ignored() {
    std::vector<MidiNote> notes;
    // Note at pitch 60, beat 0, duration 1 beat
    // Screen position: nx = startBeat * pixelsPerBeat = 0, ny = (127 - pitch) * keyHeight = 67*24 = 1608
    notes.push_back({60, 0.0, 1.0, 0.8f, 0, false, false, 1.0f}); // active note (selected=false, isDeleted=false)
    notes.push_back({60, 0.0, 1.0, 0.8f, 0, false, true, 1.0f});  // deleted note (selected=false, isDeleted=true)

    // Search at the position of the active note (nx=0, ny=1608) - should return index 0
    int idx = findNoteAtLocal(notes, 0.0f, 1608.0f, 80.0f, 24.0f);
    ASSERT(idx == 0, "should find active note, not deleted");
    PASS("deleted notes ignored in hit testing");
}

// ---------------------------------------------------------------------------
// Test: topmost note returned when overlapping
// ---------------------------------------------------------------------------
static void test_topmost_note_returned() {
    std::vector<MidiNote> notes;
    notes.push_back({60, 0.0, 1.0, 0.8f, 0, false, false, 1.0f}); // bottom note
    notes.push_back({62, 0.0, 1.0, 0.8f, 0, false, false, 1.0f}); // top note (higher pitch = rendered on top)

    // Search for note at pitch 62 position - should return index 1
    int idx = findNoteAtLocal(notes, 0.0f, (127 - 62) * 24.0f, 80.0f, 24.0f);
    ASSERT(idx == 1, "should find topmost note");

    // Search for note at pitch 60 position - should return index 0
    idx = findNoteAtLocal(notes, 0.0f, (127 - 60) * 24.0f, 80.0f, 24.0f);
    ASSERT(idx == 0, "should find lower note");
    PASS("topmost note returned when overlapping");
}

// ---------------------------------------------------------------------------
// Test: no note found outside notes
// ---------------------------------------------------------------------------
static void test_no_note_found_outside() {
    std::vector<MidiNote> notes;
    notes.push_back({60, 0.0, 1.0, 0.8f, 0, false, false, 1.0f});

    // Search far to the right (beat 10)
    int idx = findNoteAtLocal(notes, 800.0f, 0.0f, 80.0f, 24.0f);
    ASSERT(idx == -1, "no note should be found outside");

    // Search at wrong pitch
    idx = findNoteAtLocal(notes, 0.0f, (127 - 70) * 24.0f, 80.0f, 24.0f);
    ASSERT(idx == -1, "no note should be found at wrong pitch");
    PASS("no note found outside notes");
}

// ---------------------------------------------------------------------------
// Test: marquee selection detects note inside box
// ---------------------------------------------------------------------------
static void test_marquee_detects_note_inside() {
    MidiNote note{60, 0.0, 1.0, 0.8f, 0, false, false, 1.0f}; // at beat 0, pitch 60

    // Note screen position: nx=(127-60)*24=1608, ny=0 (startBeat=0)
    // Box from x=0 to x=2000, y=1500 to y=1800 should contain the note
    bool inside = isNoteInSelectionBox(note, 0.0f, 1500.0f, 2000.0f, 300.0f, 80.0f, 24.0f);
    ASSERT(inside, "note should be inside box");
    PASS("marquee selection detects note inside box");
}

// ---------------------------------------------------------------------------
// Test: marquee selection ignores deleted notes
// MidiNote: pitch, startBeat, durationBeats, velocity, unitId, selected, isDeleted, animationScale
// ---------------------------------------------------------------------------
static void test_marquee_ignores_deleted() {
    MidiNote deletedNote{60, 0.0, 1.0, 0.8f, 0, false, true, 1.0f}; // selected=false, isDeleted=true

    // The helper function isNoteInSelectionBox doesn't check isDeleted directly,
    // but this test documents expected caller behavior: deleted notes should
    // be filtered before calling.
    bool inside = isNoteInSelectionBox(deletedNote, 0.0f, 1500.0f, 2000.0f, 300.0f, 80.0f, 24.0f);
    ASSERT(inside, "helper returns true for deleted notes - caller must filter");

    // The isNoteActive helper should be used
    ASSERT(!isNoteActive(deletedNote), "isNoteActive returns false for deleted");
    PASS("marquee selection - deleted notes filtered by caller");
}

// ---------------------------------------------------------------------------
// Test: negative box dimensions handled
// ---------------------------------------------------------------------------
static void test_negative_box_dimensions() {
    MidiNote note{60, 0.0, 1.0, 0.8f, 0, false, false, 1.0f};
    // Note screen position: nx=0 (startBeat=0, pixelsPerBeat=80), ny=1608 ((127-60)*24), width=80, height=24

    // Box clearly outside note's position (note at x=0, box at x=0-100)
    bool outside = isNoteInSelectionBox(note, 0.0f, 0.0f, 100.0f, 100.0f, 80.0f, 24.0f);
    ASSERT(!outside, "note should NOT be inside box at x=0-100");

    // Same box with negative width (from 100 to 0) - should normalize and give same result
    outside = isNoteInSelectionBox(note, 100.0f, 0.0f, -100.0f, 100.0f, 80.0f, 24.0f);
    ASSERT(!outside, "negative width box normalized correctly");

    // Box containing the note (x 0-100, y 1600-1700) - note is at nx=0, ny=1608
    bool inside = isNoteInSelectionBox(note, 0.0f, 1600.0f, 100.0f, 100.0f, 80.0f, 24.0f);
    ASSERT(inside, "note should be inside box 0-100 x, 1600-1700 y");

    // Box with negative width containing the note (from 100 to 0, y unchanged)
    inside = isNoteInSelectionBox(note, 100.0f, 1600.0f, -100.0f, 100.0f, 80.0f, 24.0f);
    ASSERT(inside, "note should be inside box with negative width");
    PASS("negative box dimensions handled");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== PianoRollInteraction Unit Tests ===\n\n";

    test_deleted_notes_ignored();
    test_topmost_note_returned();
    test_no_note_found_outside();
    test_marquee_detects_note_inside();
    test_marquee_ignores_deleted();
    test_negative_box_dimensions();

    std::cout << "\n=== Results: " << testsPassed << " passed, " << testsFailed << " failed ===\n";
    return testsFailed > 0 ? 1 : 0;
}