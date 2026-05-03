// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NoteDiff Unit Tests
// Tests: diffNotes() — the pure note diffing function that powers PianoRollPanel::savePattern()

#include "Commands/NoteDiff.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

static int testsPassed = 0;
static int testsFailed = 0;

#define PASS(msg) do { std::cout << "PASS: " << msg << "\n"; testsPassed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << "\n"; testsFailed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)
#define ASSERT_SIZE(v, n, msg) ASSERT(static_cast<int>((v).size()) == (n), msg)

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
static MidiNote make(int pitch, double start, double dur, float vel = 0.8f,
                     uint64_t unitId = 0) {
    return MidiNote{pitch, start, dur, vel, unitId, 0, 1.0f, false};
}


// ---------------------------------------------------------------------------
// Empty diffs
// ---------------------------------------------------------------------------
static void test_empty_to_one_note() {
    std::vector<MidiNote> before;
    std::vector<MidiNote> after{make(60, 0.0, 1.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.added, 1, "added 1 note");
    ASSERT_SIZE(r.removed, 0, "no removals");
    ASSERT_SIZE(r.moved, 0, "no moves");
    ASSERT_SIZE(r.resized, 0, "no resizes");
    PASS("empty to one note");
}

static void test_one_note_to_empty() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0)};
    std::vector<MidiNote> after;
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.added, 0, "no additions");
    ASSERT_SIZE(r.removed, 1, "removed 1 note");
    PASS("one note to empty");
}

static void test_unchanged_note() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(64, 2.0, 0.5)};
    std::vector<MidiNote> after{make(60, 0.0, 1.0), make(64, 2.0, 0.5)};
    auto r = diffNotes(before, after);
    ASSERT(r.empty(), "no changes at all");
    PASS("unchanged notes");
}

// ---------------------------------------------------------------------------
// Add / Remove
// ---------------------------------------------------------------------------
static void test_add_two_notes() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0)};
    std::vector<MidiNote> after{make(60, 0.0, 1.0), make(64, 2.0, 0.5), make(67, 4.0, 1.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.added, 2, "2 added");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    PASS("add two notes");
}

static void test_remove_two_notes() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(64, 2.0, 0.5), make(67, 4.0, 1.0)};
    std::vector<MidiNote> after{make(64, 2.0, 0.5)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT_SIZE(r.removed, 2, "2 removed");
    PASS("remove two notes");
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------
static void test_resize_one_note() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0)};
    std::vector<MidiNote> after{make(60, 0.0, 2.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.resized, 1, "1 resize");
    ASSERT_SIZE(r.moved, 0, "0 moves");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    ASSERT(r.resized[0].first.durationBeats == 1.0, "old duration 1.0");
    ASSERT(r.resized[0].second.durationBeats == 2.0, "new duration 2.0");
    PASS("resize one note");
}

static void test_resize_then_change_pitch() {
    // Note changes both pitch AND duration. Since position (pitch+start) changes,
    // this cannot be a pure move or resize. It's remove+add (ambiguous).
    std::vector<MidiNote> before{make(60, 0.0, 1.0)};
    std::vector<MidiNote> after{make(62, 0.0, 2.0)};
    auto r = diffNotes(before, after);
    // Position changed AND duration changed → remove+add (ambiguous)
    ASSERT_SIZE(r.removed, 1, "1 removed (position+duration changed)");
    ASSERT_SIZE(r.added, 1, "1 added (ambiguous)");
    ASSERT_SIZE(r.resized, 0, "0 resizes");
    ASSERT_SIZE(r.moved, 0, "0 moves");
    PASS("position+duration change treated as remove+add");
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------
static void test_move_one_note() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0)};
    std::vector<MidiNote> after{make(64, 4.0, 1.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.moved, 1, "1 move");
    ASSERT_SIZE(r.resized, 0, "0 resizes");
    ASSERT(r.moved[0].first.pitch == 60, "old pitch 60");
    ASSERT(r.moved[0].first.startBeat == 0.0, "old start 0");
    ASSERT(r.moved[0].second.pitch == 64, "new pitch 64");
    ASSERT(r.moved[0].second.startBeat == 4.0, "new start 4");
    PASS("move one note");
}

static void test_move_and_resize() {
    // Move to new position, keep duration — move
    std::vector<MidiNote> before{make(60, 0.0, 1.0)};
    std::vector<MidiNote> after{make(64, 4.0, 1.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.moved, 1, "1 move");
    PASS("move preserves duration");
}

static void test_move_with_different_duration_preserves_both() {
    // Move a note that has a different duration than any note at the new position.
    // Key scenario: note1 at (60, 0.0, dur=1.0), note2 at (60, 0.0, dur=2.0).
    // Moving note1 to (64, 2.0, dur=1.0) should be a move, not remove+add.
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(64, 2.0, 2.0)};
    std::vector<MidiNote> after{make(64, 2.0, 2.0), make(64, 4.0, 1.0)}; // note1 moved to (64,4)
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.moved, 1, "1 move");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    PASS("move with different duration preserved as move");
}

// ---------------------------------------------------------------------------
// THE CRITICAL AMBIGUITY CASES
// ---------------------------------------------------------------------------

// Case 1: Delete one of two same-position different-duration notes
static void test_delete_one_of_two_same_position_different_duration() {
    // note1: (60, 0.0, dur=1.0), note2: (60, 0.0, dur=2.0)
    // Delete note2 → only (60, 0.0, dur=1.0) should remain
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 0.0, 2.0)};
    std::vector<MidiNote> after{make(60, 0.0, 1.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.removed, 1, "1 removed");
    ASSERT_SIZE(r.added, 0, "0 added");
    // Check the removed note is note2 (duration 2.0), not note1 (duration 1.0)
    ASSERT(r.removed[0].durationBeats == 2.0, "removed note has dur=2.0");
    PASS("delete one of two same-position different-duration notes");
}

// Case 2: Resize one of two same-position different-duration notes
static void test_resize_one_of_two_same_position_different_duration() {
    // note1: (60, 0.0, dur=1.0), note2: (60, 0.0, dur=2.0)
    // Resize note1 to dur=4.0 → (60, 0.0, dur=4.0) + note2 unchanged
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 0.0, 2.0)};
    std::vector<MidiNote> after{make(60, 0.0, 4.0), make(60, 0.0, 2.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.resized, 1, "1 resize");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT(r.resized[0].first.durationBeats == 1.0, "resized note old dur=1.0");
    ASSERT(r.resized[0].second.durationBeats == 4.0, "resized note new dur=4.0");
    PASS("resize one of two same-position different-duration notes");
}

// Case 3: Add a third note at same position with yet another duration
static void test_add_third_note_same_position_different_duration() {
    // note1: (60, 0.0, dur=1.0), note2: (60, 0.0, dur=2.0)
    // Add note3: (60, 0.0, dur=3.0) → 3 notes total, all distinct
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 0.0, 2.0)};
    std::vector<MidiNote> after{make(60, 0.0, 1.0), make(60, 0.0, 2.0), make(60, 0.0, 3.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.added, 1, "1 added");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    ASSERT(r.added[0].durationBeats == 3.0, "added note has dur=3.0");
    PASS("add third note same position, different duration");
}

// Case 4: Delete + resize in same commit
static void test_delete_and_resize_together() {
    // note1: (60, 0.0, dur=1.0), note2: (60, 0.0, dur=2.0)
    // Delete note1, resize note2 to dur=4.0
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 0.0, 2.0)};
    std::vector<MidiNote> after{make(60, 0.0, 4.0)};
    auto r = diffNotes(before, after);
    ASSERT_SIZE(r.removed, 1, "1 removed");
    ASSERT_SIZE(r.resized, 1, "1 resized");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT(r.removed[0].durationBeats == 1.0, "removed note has dur=1.0");
    ASSERT(r.resized[0].first.durationBeats == 2.0, "resized note old dur=2.0");
    ASSERT(r.resized[0].second.durationBeats == 4.0, "resized note new dur=4.0");
    PASS("delete + resize together");
}

// Case 5: Ambiguous — two before notes at same position+duration, only one after
// When exactly 2 notes share same position+duration, delete one → remove
static void test_ambiguous_delete_two_at_same_position() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 0.0, 1.0)};
    std::vector<MidiNote> after{make(60, 0.0, 1.0)};
    auto r = diffNotes(before, after);
    // Two notes at same position+duration. One matches exactly (unchanged),
    // the other has no match in 'after' for the same position -> removed.
    ASSERT_SIZE(r.removed, 1, "1 removed");
    ASSERT_SIZE(r.added, 0, "0 added");
    PASS("ambiguous delete — one note at same pos+dur removed");
}

// Case 6: Two before notes at same position, different duration, different after
// note1: (60,0,1), note2: (60,0,2) → after: (60,0,2), (60,0,3)
// note2 matches exactly (dur=2), note1 dur=1 -> dur=3 -> resize
static void test_two_before_different_dur_after_three() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 0.0, 2.0)};
    std::vector<MidiNote> after{make(60, 0.0, 2.0), make(60, 0.0, 3.0)};
    auto r = diffNotes(before, after);
    // dur=2 matches exactly, dur=1 matches to dur=3 -> resize
    ASSERT_SIZE(r.removed, 0, "0 removed");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT_SIZE(r.resized, 1, "1 resized");
    PASS("two before (diff dur) → one exact match, one resize");
}

// Case 7: Move one note when another at same position has different duration
// note1: (60,0,1), note2: (60,0,2) → move note1 to (64,2,1), keep note2
// Result: 1 move, 1 unchanged
static void test_move_when_sibling_has_different_duration() {
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 0.0, 2.0)};
    std::vector<MidiNote> after{make(60, 0.0, 2.0), make(64, 2.0, 1.0)};
    auto r = diffNotes(before, after);
    // note2 (60,0,dur=2) matches exactly. note1 moves from (60,0,dur=1) to (64,2,dur=1).
    ASSERT_SIZE(r.moved, 1, "1 move");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    ASSERT(r.moved[0].first.durationBeats == 1.0, "moved note old dur=1.0");
    ASSERT(r.moved[0].first.pitch == 60, "moved note old pitch=60");
    ASSERT(r.moved[0].first.startBeat == 0.0, "moved note old start=0");
    PASS("move note when sibling has different duration");
}

// Case 8: Move to a position already occupied by a different-duration note
// note1: (60,0,1) → move to (60,2,1) but note2 already at (60,2,2)
// Result: move note1 to (60,2,1), add duplicate at (60,2,2) — ambiguous, remove+add
// This is the tricky case: destination has different duration, can't be a move target
static void test_move_to_occupied_position_different_duration() {
    // note1: (60,0,1), note2: (60,2,2)
    // After: (60,2,1) and (60,2,2) - note1 moved to (60,2), note2 unchanged position but...
    // Wait, note2 starts at (60,2) already. So after has two notes at (60,2): dur=1 and dur=2.
    // This means note1 (60,0,1) -> (60,2,1) is a move, note2 (60,2,2) stays.
    std::vector<MidiNote> before{make(60, 0.0, 1.0), make(60, 2.0, 2.0)};
    std::vector<MidiNote> after{make(60, 2.0, 1.0), make(60, 2.0, 2.0)};
    auto r = diffNotes(before, after);
    // note2 at (60,2,2) matches exactly. note1 (60,0,1) -> (60,2,1) is a move (same duration).
    ASSERT_SIZE(r.moved, 1, "1 move");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    ASSERT(r.moved[0].first.startBeat == 0.0, "moved note from start 0");
    ASSERT(r.moved[0].second.startBeat == 2.0, "moved note to start 2");
    PASS("move to occupied position with different duration sibling");
}

// ---------------------------------------------------------------------------
// Complex edits
// ---------------------------------------------------------------------------
static void test_complex_edit_add_remove_resize_move() {
    std::vector<MidiNote> before{
        make(60, 0.0, 1.0),
        make(64, 2.0, 0.5),
        make(67, 4.0, 1.0),
        make(60, 0.0, 2.0) // second note at (60,0) with different duration
    };
    std::vector<MidiNote> after{
        make(60, 0.0, 2.0), // note4 stays
        make(64, 3.0, 0.5), // note2 moved
        make(67, 4.0, 2.0), // note3 resized
        make(72, 6.0, 1.0)  // new note
    };
    auto r = diffNotes(before, after);
    // note4 (60,0,dur=2) matches exactly with (60,0,dur=2) in after.
    // note1 (60,0,dur=1) has no exact match at position (60,0).
    //   - The greedy match pairs note1 (dur=1) with (72,6,dur=1) -> move (same duration)
    //   - note3 (67,4,dur=1) -> (67,4,dur=2) -> resize (position match)
    //   - note2 (64,2,dur=0.5) -> (64,3,dur=0.5) -> move (duration match only)
    ASSERT_SIZE(r.moved, 2, "2 moves");
    ASSERT_SIZE(r.resized, 1, "1 resize");
    ASSERT_SIZE(r.added, 0, "0 added");
    ASSERT_SIZE(r.removed, 0, "0 removed");
    // note1 moved to (72,6), note2 moved to (64,3), note3 resized, note4 unchanged
    PASS("complex edit: 2 moves, 1 resize");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== NoteDiff Unit Tests ===\n\n";

    std::cout << "[Empty Diffs]\n";
    test_empty_to_one_note();
    test_one_note_to_empty();
    test_unchanged_note();

    std::cout << "\n[Add/Remove]\n";
    test_add_two_notes();
    test_remove_two_notes();

    std::cout << "\n[Resize]\n";
    test_resize_one_note();
    test_resize_then_change_pitch();

    std::cout << "\n[Move]\n";
    test_move_one_note();
    test_move_and_resize();
    test_move_with_different_duration_preserves_both();

    std::cout << "\n[Critical Ambiguity Cases]\n";
    test_delete_one_of_two_same_position_different_duration();
    test_resize_one_of_two_same_position_different_duration();
    test_add_third_note_same_position_different_duration();
    test_delete_and_resize_together();
    test_ambiguous_delete_two_at_same_position();
    test_two_before_different_dur_after_three();
    test_move_when_sibling_has_different_duration();
    test_move_to_occupied_position_different_duration();

    std::cout << "\n[Complex Edits]\n";
    test_complex_edit_add_remove_resize_move();

    std::cout << "\n=== Results: " << testsPassed << " passed, " << testsFailed << " failed ===\n";
    return testsFailed > 0 ? 1 : 0;
}
