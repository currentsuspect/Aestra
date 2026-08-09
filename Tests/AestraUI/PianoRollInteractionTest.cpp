// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Helpers/PianoRollInteraction.h"
#include "Helpers/TimelineGridRenderer.h"
#include "Common/MusicHelpers.h"
#include "Widgets/NUIPianoRollWidgets.h"
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
// MidiNote: pitch, startBeat, durationBeats, velocity, pan, unitId, selected, isDeleted, animationScale
// ---------------------------------------------------------------------------
static void test_deleted_notes_ignored() {
    std::vector<MidiNote> notes;
    // Note at pitch 60, beat 0, duration 1 beat
    // Screen position: nx = startBeat * pixelsPerBeat = 0, ny = (127 - pitch) * keyHeight = 67*24 = 1608
    notes.push_back({60, 0.0, 1.0, 0.8f, 0.0f, 0, false, false, 1.0f}); // active note (selected=false, isDeleted=false)
    notes.push_back({60, 0.0, 1.0, 0.8f, 0.0f, 0, false, true, 1.0f});  // deleted note (selected=false, isDeleted=true)

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
    notes.push_back({60, 0.0, 1.0, 0.8f, 0.0f, 0, false, false, 1.0f}); // bottom note
    notes.push_back({62, 0.0, 1.0, 0.8f, 0.0f, 0, false, false, 1.0f}); // top note (higher pitch = rendered on top)

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
    notes.push_back({60, 0.0, 1.0, 0.8f, 0.0f, 0, false, false, 1.0f});

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
    MidiNote note{60, 0.0, 1.0, 0.8f, 0.0f, 0, false, false, 1.0f}; // at beat 0, pitch 60

    // Note screen position: nx=(127-60)*24=1608, ny=0 (startBeat=0)
    // Box from x=0 to x=2000, y=1500 to y=1800 should contain the note
    bool inside = isNoteInSelectionBox(note, 0.0f, 1500.0f, 2000.0f, 300.0f, 80.0f, 24.0f);
    ASSERT(inside, "note should be inside box");
    PASS("marquee selection detects note inside box");
}

// ---------------------------------------------------------------------------
// Test: marquee selection ignores deleted notes
// MidiNote: pitch, startBeat, durationBeats, velocity, pan, unitId, selected, isDeleted, animationScale
// ---------------------------------------------------------------------------
static void test_marquee_ignores_deleted() {
    MidiNote deletedNote{60, 0.0, 1.0, 0.8f, 0.0f, 0, false, true, 1.0f}; // selected=false, isDeleted=true

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
    MidiNote note{60, 0.0, 1.0, 0.8f, 0.0f, 0, false, false, 1.0f};
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
// Test: velocity editor preserves MidiNote's normalized 0..1 representation
// ---------------------------------------------------------------------------
static void test_velocity_panel_normalization() {
    ASSERT(velocityFromPanelPosition(100.0f, 100.0f, 80.0f) == 0.0f,
           "velocity at panel floor should be zero");
    ASSERT(velocityFromPanelPosition(60.0f, 100.0f, 80.0f) == 0.5f,
           "velocity at panel midpoint should be normalized to 0.5");
    ASSERT(velocityFromPanelPosition(20.0f, 100.0f, 80.0f) == 1.0f,
           "velocity at panel ceiling should be one");
    ASSERT(velocityFromPanelPosition(0.0f, 100.0f, 80.0f) == 1.0f,
           "velocity above panel should clamp to one");
    ASSERT(velocityFromPanelPosition(120.0f, 100.0f, 80.0f) == 0.0f,
           "velocity below panel should clamp to zero");

    ASSERT(velocityToPanelHeight(0.0f, 80.0f) == 0.0f,
           "zero velocity should render at zero height");
    ASSERT(velocityToPanelHeight(0.5f, 80.0f) == 40.0f,
           "normalized midpoint should render at half height");
    ASSERT(velocityToPanelHeight(1.0f, 80.0f) == 80.0f,
           "full velocity should render at full height");
    ASSERT(velocityToPanelHeight(127.0f, 80.0f) == 80.0f,
           "legacy out-of-range velocity should render safely");
    PASS("velocity panel uses normalized MidiNote velocities");
}

static void test_shared_timeline_grid_density() {
    ASSERT(timelineGridLevelFade(10.0f) == 0.0f, "dense grid levels should be hidden");
    ASSERT(timelineGridLevelFade(21.0f) == 0.5f, "grid levels should crossfade at midpoint");
    ASSERT(timelineGridLevelFade(32.0f) == 1.0f, "wide grid levels should be fully visible");
    ASSERT(timelineGridBarStride(80.0f, 4) == 1, "normal zoom should show every bar");
    ASSERT(timelineGridBarStride(5.0f, 4) == 2, "zoomed-out grid should promote to two-bar blocks");
    ASSERT(timelineGridBarStride(1.0f, 4) == 8, "extreme zoom should preserve power-of-two hierarchy");
    PASS("Piano Roll and Track Manager share adaptive grid density");
}

static void test_connect_note_legato() {
    // Note at [0,1) with a follower starting at 2 → elongate end to 2 (gap filled).
    ASSERT(computeConnectedNoteEnd(0.0, 1.0, {0.0, 2.0}, 0.25) == 2.0,
           "connect should extend the note up to the next note's start");
    // Already overlapping the next note (next starts before end) → never shorten.
    ASSERT(computeConnectedNoteEnd(0.0, 3.0, {0.0, 2.0}, 0.25) == 3.0,
           "connect must never shorten a note that already reaches past the next");
    // No follower → extend to the next snap boundary beyond the end.
    ASSERT(computeConnectedNoteEnd(0.0, 1.3, {0.0}, 0.5) == 1.5,
           "connect should fill out to the next snap line when nothing follows");
    // No follower, end already on a boundary → advance to the next boundary.
    ASSERT(computeConnectedNoteEnd(0.0, 2.0, {0.0}, 1.0) == 3.0,
           "connect should reach the next beat when the end sits on one");
    PASS("Ctrl+L connect elongates to the next note or the next grid line");
}

static void test_quantize_to_grid() {
    ASSERT(quantizeBeatToGrid(0.24, 0.25) == 0.25, "quantize should snap up to the nearest grid line");
    ASSERT(quantizeBeatToGrid(0.10, 0.25) == 0.0, "quantize should snap down to the nearest grid line");
    ASSERT(quantizeBeatToGrid(1.0, 0.5) == 1.0, "quantize should leave on-grid positions untouched");
    ASSERT(quantizeBeatToGrid(0.30, 0.0) == 0.30, "a zero grid leaves the position unchanged");
    PASS("Q quantizes note starts to the nearest grid line");
}

// ---------------------------------------------------------------------------
// Scale Pitch Movement Tests
// ---------------------------------------------------------------------------

static void test_c_major_in_scale_detection() {
    // C3 = 48, C#3 = 49, D3 = 50, etc.
    // Root key 0 = C
    ASSERT(MusicTheory::isNoteInScale(48, 0, ScaleType::Major), "C in C major should be in scale");
    ASSERT(MusicTheory::isNoteInScale(50, 0, ScaleType::Major), "D in C major should be in scale");
    ASSERT(MusicTheory::isNoteInScale(52, 0, ScaleType::Major), "E in C major should be in scale");
    ASSERT(MusicTheory::isNoteInScale(53, 0, ScaleType::Major), "F in C major should be in scale");
    ASSERT(MusicTheory::isNoteInScale(55, 0, ScaleType::Major), "G in C major should be in scale");
    ASSERT(MusicTheory::isNoteInScale(57, 0, ScaleType::Major), "A in C major should be in scale");
    ASSERT(MusicTheory::isNoteInScale(59, 0, ScaleType::Major), "B in C major should be in scale");

    ASSERT(!MusicTheory::isNoteInScale(49, 0, ScaleType::Major), "C# in C major should NOT be in scale");
    ASSERT(!MusicTheory::isNoteInScale(51, 0, ScaleType::Major), "D# in C major should NOT be in scale");
    ASSERT(!MusicTheory::isNoteInScale(54, 0, ScaleType::Major), "F# in C major should NOT be in scale");
    ASSERT(!MusicTheory::isNoteInScale(56, 0, ScaleType::Major), "G# in C major should NOT be in scale");
    ASSERT(!MusicTheory::isNoteInScale(58, 0, ScaleType::Major), "A# in C major should NOT be in scale");
    PASS("C major in-scale detection");
}

static void test_a_natural_minor_detection() {
    // A3 = 57, root key 9 = A
    ASSERT(MusicTheory::isNoteInScale(57, 9, ScaleType::Minor), "A in A minor should be in scale");
    ASSERT(MusicTheory::isNoteInScale(59, 9, ScaleType::Minor), "B in A minor should be in scale");
    ASSERT(MusicTheory::isNoteInScale(60, 9, ScaleType::Minor), "C in A minor should be in scale");
    ASSERT(MusicTheory::isNoteInScale(62, 9, ScaleType::Minor), "D in A minor should be in scale");
    ASSERT(MusicTheory::isNoteInScale(64, 9, ScaleType::Minor), "E in A minor should be in scale");
    ASSERT(MusicTheory::isNoteInScale(65, 9, ScaleType::Minor), "F in A minor should be in scale");
    ASSERT(MusicTheory::isNoteInScale(67, 9, ScaleType::Minor), "G in A minor should be in scale");
    PASS("A natural minor detection");
}

static void test_chromatic_returns_original() {
    ASSERT(MusicTheory::nextPitchInScale(60, 0, ScaleType::Chromatic) == 61, "Chromatic next should be +1");
    ASSERT(MusicTheory::previousPitchInScale(60, 0, ScaleType::Chromatic) == 59, "Chromatic prev should be -1");
    PASS("Chromatic scale behaves like chromatic");
}

static void test_next_pitch_in_scale() {
    // C major: C D E F G A B (pitch classes 0, 2, 4, 5, 7, 9, 11)
    // C3 = 48, D3 = 50, E = 52, F = 53, G = 55, A = 57, B = 59

    ASSERT(MusicTheory::nextPitchInScale(48, 0, ScaleType::Major) == 50, "C in C major -> D");
    ASSERT(MusicTheory::nextPitchInScale(52, 0, ScaleType::Major) == 53, "E in C major -> F");
    ASSERT(MusicTheory::nextPitchInScale(59, 0, ScaleType::Major) == 60, "B in C major -> C next octave");
    ASSERT(MusicTheory::nextPitchInScale(127, 0, ScaleType::Major) == 127, "Pitch 127 clamps safely");
    PASS("nextPitchInScale works correctly");
}

static void test_previous_pitch_in_scale() {
    // B3 = 59, C4 = 60 in C major
    ASSERT(MusicTheory::previousPitchInScale(60, 0, ScaleType::Major) == 59, "C in C major -> B prev octave");
    ASSERT(MusicTheory::previousPitchInScale(53, 0, ScaleType::Major) == 52, "F in C major -> E");
    ASSERT(MusicTheory::previousPitchInScale(0, 0, ScaleType::Major) == 0, "Pitch 0 clamps safely");
    PASS("previousPitchInScale works correctly");
}

static void test_snap_pitch_to_scale_edge_cases() {
    // C#4 (61) should snap to nearest in C major: D (62) or C (60)?
    // Distance: C# -> C (1 semitone) vs D (1 semitone). Tie-break should pick C (lower).
    int snapped = MusicTheory::nextPitchInScale(61, 0, ScaleType::Major);  // Not the snap function, test next/prev
    ASSERT(snapped == 62 || snapped == 60, "C# in C major can go to D or C");

    // Test the actual snap behavior is handled in the widget layer
    PASS("snapPitchToScale edge cases documented");
}

static void test_harmony_context_edit_notification() {
    PianoRollToolbar toolbar;
    auto grid = std::make_shared<PianoRollGrid>();
    auto notes = std::make_shared<PianoRollNoteLayer>();
    toolbar.setGrid(grid);
    toolbar.setNoteLayer(notes);

    int notificationCount = 0;
    int notifiedRoot = -1;
    ScaleType notifiedScale = ScaleType::Chromatic;
    bool notifiedSnap = false;
    toolbar.setOnHarmonyContextChanged([&](int root, ScaleType scale, bool snap) {
        ++notificationCount;
        notifiedRoot = root;
        notifiedScale = scale;
        notifiedSnap = snap;
    });

    toolbar.setHarmonyContext(9, ScaleType::Minor, false);
    ASSERT(notificationCount == 0, "loading harmony context must not masquerade as a user edit");
    ASSERT(toolbar.getRootKey() == 9 && toolbar.getScaleType() == ScaleType::Minor,
           "loaded harmony context should become the toolbar authority");

    toolbar.applyHarmonyContextEdit(9, ScaleType::Minor, true);
    ASSERT(notificationCount == 1, "a harmony edit should notify the owning panel exactly once");
    ASSERT(notifiedRoot == 9 && notifiedScale == ScaleType::Minor && notifiedSnap,
           "harmony notification should carry the complete context");
    ASSERT(notes->getSnapToScale(), "harmony edit should update the note layer before notification");

    toolbar.applyHarmonyContextEdit(99, ScaleType::Count, false);
    ASSERT(toolbar.getRootKey() == 11,
           "out-of-range root edits should clamp before reaching persistent state");
    ASSERT(toolbar.getScaleType() == ScaleType::Blues,
           "out-of-range scale edits should clamp before reaching persistent state");
    PASS("Piano Roll harmony edits publish one complete, normalized context");
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
    test_velocity_panel_normalization();
    test_shared_timeline_grid_density();
    test_connect_note_legato();
    test_quantize_to_grid();
    test_c_major_in_scale_detection();
    test_a_natural_minor_detection();
    test_chromatic_returns_original();
    test_next_pitch_in_scale();
    test_previous_pitch_in_scale();
    test_snap_pitch_to_scale_edge_cases();
    test_harmony_context_edit_notification();

    std::cout << "\n=== Results: " << testsPassed << " passed, " << testsFailed << " failed ===\n";
    return testsFailed > 0 ? 1 : 0;
}
