// © 2026 Aestra Studios — All Rights Reserved.

// Regression for #829: external lane edits (velocity/pan control panel) must
// commit through onNotesChanged_ like every other gesture. Before the fix,
// pushExternalEdit only pushed the layer-local undo step, so PatternManager
// kept the old value until an unrelated committing event flushed it — the
// "pan applies but nothing refreshes until I place another note" symptom.
// The layer no longer keeps an undo history at all (ownership contract F1/F2):
// the commit is the undo step, recorded by the owner's CommandHistory.

#include "NUIPianoRollWidgets.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

MidiNote makeNote(int pitch, double startBeat) {
    MidiNote note;
    note.pitch = pitch;
    note.startBeat = startBeat;
    note.durationBeats = 1.0;
    note.velocity = 0.8f;
    note.pan = 0.0f;
    return note;
}

void testPushExternalEditFiresCommit() {
    PianoRollNoteLayer layer;
    layer.setBounds({0.0f, 0.0f, 800.0f, 3072.0f});
    layer.setNotes({makeNote(60, 0.0), makeNote(64, 1.0)});

    int commits = 0;
    std::vector<MidiNote> committed;
    layer.setOnNotesChanged([&commits, &committed](const std::vector<MidiNote>& notes) {
        ++commits;
        committed = notes;
    });

    const auto before = layer.getNotes();
    auto edited = before;
    edited[0].pan = -0.5f; // what a pan-lane drag writes per frame
    layer.setNotes(edited);

    layer.pushExternalEdit(before, "Pan");

    check(commits == 1, "pushExternalEdit must fire onNotesChanged_ exactly once");
    check(committed.size() == 2, "commit must carry the full note set");
    check(std::fabs(committed[0].pan - (-0.5f)) < 0.0001f, "committed note must carry the edited pan");
}

void testPushExternalEditVelocityCommitsOnce() {
    PianoRollNoteLayer layer;
    layer.setBounds({0.0f, 0.0f, 800.0f, 3072.0f});
    layer.setNotes({makeNote(62, 0.5)});

    int commits = 0;
    std::vector<MidiNote> committed;
    layer.setOnNotesChanged([&commits, &committed](const std::vector<MidiNote>& notes) {
        ++commits;
        committed = notes;
    });

    const auto before = layer.getNotes();
    auto edited = before;
    edited[0].velocity = 0.25f;
    layer.setNotes(edited);

    layer.pushExternalEdit(before, "Velocity");

    check(commits == 1, "velocity lane edit must commit exactly once");
    check(layer.getNotes()[0].velocity == 0.25f, "committed velocity must be visible via getNotes()");
    check(committed.size() == 1 && committed[0].velocity == 0.25f, "the commit must carry the edited velocity");
    check(before[0].velocity == 0.8f, "the pre-edit snapshot the owner diffs against must be untouched");
}

} // namespace

int main() {
    testPushExternalEditFiresCommit();
    testPushExternalEditVelocityCommitsOnce();

    if (g_failures == 0) {
        std::cout << "PianoRollExternalEditCommitTest: all checks passed\n";
        return 0;
    }
    std::cerr << "PianoRollExternalEditCommitTest: " << g_failures << " failure(s)\n";
    return 1;
}
