// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Ownership contract — Piano Roll panel invariants (contract:ownership, UI lane).
//
// Drives the real PianoRollPanel against a real TrackManager (same harness as
// PianoRollPanelPersistenceTest). Modes (argv[1]):
//   gesture        guards F3  §3.5 one user gesture = one CommandHistory entry, and undoing
//                             that entry restores the pattern exactly (redo re-applies it)
//   cross-pattern  guards F2  §3.5 undo after switching pattern never writes into the other pattern
//   global-undo    guards F1  §3.5 Ctrl+Z at the Piano Roll reaches the single global history
//   paste-target   guards F5  §3.1 pasted notes target the unit being edited
//
// The app shell (AestraContent) forwards keys to the Piano Roll whenever it is
// visible; this harness checks the panel half of that path. Expected outcomes
// are the contract (Aestra-Internals: Ownership & Reference Contract, rev 3).

#include "../Contract/ContractSupport.h"
#include "Commands/AddClipCommand.h"
#include "Commands/CommandHistory.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "PianoRollPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>
#include <vector>

using namespace Aestra::Audio;
using AestraContract::contractSetup;
using AestraContract::str;
using AestraContract::Verdict;

namespace {

bool key(PianoRollPanel& panel, AestraUI::NUIKeyCode code, bool ctrl = false, bool shift = false) {
    AestraUI::NUIKeyEvent e;
    e.keyCode = code;
    e.modifiers = AestraUI::NUIModifiers::None;
    if (ctrl)
        e.modifiers = e.modifiers | AestraUI::NUIModifiers::Ctrl;
    if (shift)
        e.modifiers = e.modifiers | AestraUI::NUIModifiers::Shift;
    e.pressed = true;
    return panel.handleKeyEvent(e);
}

std::vector<MidiNote> storedNotes(TrackManager& tm, PatternID pid) {
    const auto* p = tm.getPatternManager().getPattern(pid);
    contractSetup(p && p->isMidi(), "pattern missing");
    return p->getMidiNotes();
}

bool sameNotes(std::vector<MidiNote> a, std::vector<MidiNote> b) {
    auto key = [](const MidiNote& n) { return std::make_tuple(n.startBeat, n.pitch, n.durationBeats, n.unitId); };
    auto less = [&](const MidiNote& x, const MidiNote& y) { return key(x) < key(y); };
    std::sort(a.begin(), a.end(), less);
    std::sort(b.begin(), b.end(), less);
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (key(a[i]) != key(b[i]) || a[i].velocity != b[i].velocity)
            return false;
    }
    return true;
}

std::string describe(const std::vector<MidiNote>& notes) {
    std::string s;
    for (const auto& n : notes)
        s += " " + str(n.pitch) + "@" + str(n.startBeat) + "/u" + str(n.unitId);
    return s.empty() ? " (none)" : s;
}

PatternID makePattern(TrackManager& tm, const char* name, const std::vector<MidiNote>& notes) {
    MidiPayload payload;
    payload.notes = notes;
    const PatternID pid = tm.getPatternManager().createMidiPattern(name, 8.0, payload);
    contractSetup(pid.isValid(), "createMidiPattern failed");
    return pid;
}

// The panel's view mirrors the stored pattern (after an undo/redo reload).
bool viewMatches(PianoRollPanel& panel, const std::vector<MidiNote>& stored) {
    std::vector<MidiNote> shown;
    for (const auto& n : panel.getNotes()) {
        if (!n.isDeleted)
            shown.push_back(MidiNote{n.pitch, n.startBeat, n.durationBeats, n.velocity, n.pan, 0});
    }
    std::vector<MidiNote> expected = stored;
    for (auto& n : expected)
        n.unitId = 0;
    return sameNotes(shown, expected);
}

// ---------------------------------------------------------------- F3
int gesture() {
    Verdict v("F3");
    auto tm = std::make_shared<TrackManager>();
    // Two-beat notes, so the default one-beat grid can subdivide them below.
    const PatternID pid = makePattern(*tm, "Gesture",
                                      {MidiNote{60, 0.0, 2.0, 0.8f, 0.0f, 0}, MidiNote{64, 2.0, 2.0, 0.8f, 0.0f, 0},
                                       MidiNote{67, 4.0, 2.0, 0.8f, 0.0f, 0}});
    PianoRollPanel panel(tm);
    panel.loadPattern(pid);
    auto& history = tm->getCommandHistory();
    const auto original = storedNotes(*tm, pid);
    const double originalLength = tm->getPatternManager().getPattern(pid)->lengthBeats;
    const size_t before = history.getUndoStack().size();

    key(panel, AestraUI::NUIKeyCode::A, true); // select all
    key(panel, AestraUI::NUIKeyCode::Right);   // one gesture: nudge three notes
    const auto nudged = storedNotes(*tm, pid);
    contractSetup(std::any_of(nudged.begin(), nudged.end(),
                              [](const MidiNote& n) { return n.pitch == 60 && n.startBeat == 1.0; }),
                  "the nudge did not reach the pattern" + describe(nudged));

    const size_t entries = history.getUndoStack().size() - before;
    v.check(entries == 1, "one nudge of three selected notes = " + str(entries) + " history entries");

    // That one entry is the whole gesture: undo restores the pattern exactly
    // (notes and length), and redo re-applies it exactly.
    history.undo();
    const auto undone = storedNotes(*tm, pid);
    v.check(sameNotes(undone, original),
            "one undo restores the nudge: expected" + describe(original) + " | got" + describe(undone));
    v.check(tm->getPatternManager().getPattern(pid)->lengthBeats == originalLength,
            "one undo restores the pattern length (" + str(originalLength) + ")");
    v.check(viewMatches(panel, undone), "the Piano Roll view reflects the undone pattern");
    history.redo();
    const auto redone = storedNotes(*tm, pid);
    v.check(sameNotes(redone, nudged),
            "redo re-applies the nudge: expected" + describe(nudged) + " | got" + describe(redone));
    history.undo();

    // A second gesture kind: subdivide the selection (Ctrl+Shift+G) with the
    // default one-beat grid. One entry; one undo restores the original notes.
    key(panel, AestraUI::NUIKeyCode::A, true);
    const size_t beforeSubdivide = history.getUndoStack().size();
    key(panel, AestraUI::NUIKeyCode::G, true, true);
    const auto subdivided = storedNotes(*tm, pid);
    contractSetup(subdivided.size() > original.size(),
                  "the subdivision did not reach the pattern" + describe(subdivided));
    const size_t subdivideEntries = history.getUndoStack().size() - beforeSubdivide;
    v.check(subdivideEntries == 1, "one subdivision = " + str(subdivideEntries) + " history entries");
    history.undo();
    const auto afterSubdivideUndo = storedNotes(*tm, pid);
    v.check(sameNotes(afterSubdivideUndo, original), "one undo restores the subdivision: expected" +
                                                         describe(original) + " | got" + describe(afterSubdivideUndo));
    v.check(viewMatches(panel, afterSubdivideUndo), "the Piano Roll view reflects the undone subdivision");
    return v.finish();
}

// ---------------------------------------------------------------- F2
int crossPattern() {
    Verdict v("F2");
    auto tm = std::make_shared<TrackManager>();
    const PatternID a =
        makePattern(*tm, "A", {MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0}, MidiNote{64, 2.0, 1.0, 0.8f, 0.0f, 0}});
    const PatternID b = makePattern(*tm, "B", {MidiNote{72, 0.0, 2.0, 0.8f, 0.0f, 0}});

    PianoRollPanel panel(tm);
    panel.loadPattern(a);
    key(panel, AestraUI::NUIKeyCode::A, true);
    key(panel, AestraUI::NUIKeyCode::Right); // edit pattern A
    panel.loadPattern(b);                    // switch pattern
    const auto bBefore = storedNotes(*tm, b);

    const auto aEdited = storedNotes(*tm, a);

    // Ctrl+Z while B is loaded. When the panel does not consume it, the app
    // shell hands it to the global history — do that here too.
    if (!key(panel, AestraUI::NUIKeyCode::Z, true))
        tm->getCommandHistory().undo();
    const auto bAfter = storedNotes(*tm, b);
    v.check(sameNotes(bBefore, bAfter),
            "pattern B unchanged by undo: before" + describe(bBefore) + " | after" + describe(bAfter));
    const auto aAfter = storedNotes(*tm, a);
    v.check(!sameNotes(aAfter, aEdited), "the undo reverted the edit in pattern A: A still" + describe(aAfter));
    return v.finish();
}

// ---------------------------------------------------------------- F1
int globalUndo() {
    Verdict v("F1");
    auto tm = std::make_shared<TrackManager>();
    const PatternID pid = makePattern(*tm, "Visible", {MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, 0}});
    PianoRollPanel panel(tm);
    panel.setVisible(true);
    panel.loadPattern(pid);

    // The most recent user action happened outside the Piano Roll: a clip added
    // to the timeline.
    auto& playlist = tm->getPlaylistModel();
    const PlaylistLaneID lane = playlist.createLane("A");
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.patternId = pid;
    clip.sourceId = pid.value;
    clip.startBeat = 0.0;
    clip.durationBeats = 4.0;
    tm->getCommandHistory().pushAndExecute(std::make_shared<AddClipCommand>(playlist, lane, clip));
    contractSetup(playlist.getClip(clip.id) != nullptr, "AddClipCommand did not add the clip");

    const bool consumed = key(panel, AestraUI::NUIKeyCode::Z, true);
    const bool undone = playlist.getClip(clip.id) == nullptr;
    v.check(!consumed || undone, "Ctrl+Z at the Piano Roll reaches the global history (panel consumed=" +
                                     str(consumed) + ", timeline clip undone=" + str(undone) + ")");
    return v.finish();
}

// ---------------------------------------------------------------- F5
int pasteTarget() {
    Verdict v("F5");
    auto tm = std::make_shared<TrackManager>();
    auto& units = tm->getUnitManager();
    const UnitID unitA = units.createUnit("A", UnitType::Sampler);
    const UnitID unitB = units.createUnit("B", UnitType::Sampler);
    contractSetup(unitA != 0 && unitB != 0, "createUnit failed");
    const PatternID pid = makePattern(
        *tm, "Shared", {MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, unitA}, MidiNote{64, 1.0, 1.0, 0.8f, 0.0f, unitA}});

    PianoRollPanel panel(tm);
    panel.setEditingUnit(unitA);
    panel.loadPattern(pid);
    key(panel, AestraUI::NUIKeyCode::A, true);
    key(panel, AestraUI::NUIKeyCode::C, true); // copy unit A's notes
    panel.setEditingUnit(unitB);               // now editing unit B
    key(panel, AestraUI::NUIKeyCode::V, true); // paste

    const auto notes = storedNotes(*tm, pid);
    const auto countFor = [&](UnitID u) {
        return std::count_if(notes.begin(), notes.end(), [u](const MidiNote& n) { return n.unitId == u; });
    };
    contractSetup(notes.size() == 4, "paste did not add two notes (pattern:" + describe(notes) + ")");
    v.check(countFor(unitB) == 2 && countFor(unitA) == 2,
            "pasted notes target the editing unit B (unit A notes=" + str(countFor(unitA)) +
                ", unit B notes=" + str(countFor(unitB)) + ")");
    return v.finish();
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = AestraContract::contractMode(argc, argv);
    if (mode == "gesture")
        return gesture();
    if (mode == "cross-pattern")
        return crossPattern();
    if (mode == "global-undo")
        return globalUndo();
    if (mode == "paste-target")
        return pasteTarget();
    contractSetup(false, "unknown mode " + mode);
    return 2;
}
