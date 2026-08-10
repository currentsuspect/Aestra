// © 2026 Aestra Studios — All Rights Reserved.

#include "NUIPianoRollWidgets.h"

#include <algorithm>
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

NUIMouseEvent mouseDown(float x, float y, NUIMouseButton button, NUIModifiers modifiers = NUIModifiers::None) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Down;
    event.position = {x, y};
    event.button = button;
    event.modifiers = modifiers;
    event.pressed = true;
    return event;
}

NUIMouseEvent mouseDrag(float x, float y, NUIMouseButton button, NUIModifiers modifiers = NUIModifiers::None) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Drag;
    event.position = {x, y};
    event.button = button;
    event.modifiers = modifiers;
    return event;
}

NUIMouseEvent mouseUp(float x, float y, NUIMouseButton button, NUIModifiers modifiers = NUIModifiers::None) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Up;
    event.position = {x, y};
    event.button = button;
    event.modifiers = modifiers;
    event.released = true;
    return event;
}

NUIKeyEvent keyPress(NUIKeyCode keyCode, NUIModifiers modifiers = NUIModifiers::None) {
    NUIKeyEvent event;
    event.keyCode = keyCode;
    event.modifiers = modifiers;
    event.pressed = true;
    return event;
}

float pitchRowCenter(int pitch) {
    return static_cast<float>(127 - pitch) * 24.0f + 12.0f;
}

bool hasNote(const std::vector<MidiNote>& notes, int pitch, double beat) {
    return std::any_of(notes.begin(), notes.end(), [pitch, beat](const MidiNote& note) {
        return note.pitch == pitch && std::abs(note.startBeat - beat) < 0.001;
    });
}

void testChordBrushPaintsCompleteTriadsAsOneEdit() {
    PianoRollNoteLayer layer;
    layer.setBounds({0.0f, 0.0f, 800.0f, 3072.0f});
    layer.setTool(GlobalTool::Pencil);
    layer.setSnap(SnapGrid::Beat);
    layer.setRootKey(0);
    layer.setScaleType(ScaleType::Major);
    layer.setChordMode(true);

    int commits = 0;
    layer.setOnNotesChanged([&commits](const std::vector<MidiNote>&) { ++commits; });
    const float y = pitchRowCenter(60);
    check(layer.onMouseEvent(mouseDown(10.0f, y, NUIMouseButton::Left, NUIModifiers::Shift)),
          "chord brush press should be handled");
    check(layer.onMouseEvent(mouseDrag(90.0f, y, NUIMouseButton::Left, NUIModifiers::Shift)),
          "chord brush drag should be handled");
    check(layer.onMouseEvent(mouseUp(90.0f, y, NUIMouseButton::Left, NUIModifiers::Shift)),
          "chord brush release should be handled");

    const auto& notes = layer.getNotes();
    check(notes.size() == 6, "two crossed cells should contain two complete triads");
    for (double beat : {0.0, 1.0}) {
        check(hasNote(notes, 60, beat), "each brushed chord should contain its root");
        check(hasNote(notes, 64, beat), "each brushed chord should contain its third");
        check(hasNote(notes, 67, beat), "each brushed chord should contain its fifth");
    }
    check(commits == 1, "a chord brush gesture should commit once on release");

    layer.undo();
    check(layer.getNotes().empty(), "one undo should remove the complete chord brush gesture");
}

void testRightDragEraseIsOneUndoableStroke() {
    PianoRollNoteLayer layer;
    layer.setBounds({0.0f, 0.0f, 800.0f, 3072.0f});
    layer.setSnap(SnapGrid::Beat);
    std::vector<MidiNote> notes;
    for (double beat : {0.0, 1.0, 2.0}) {
        MidiNote note;
        note.pitch = 60;
        note.startBeat = beat;
        note.durationBeats = 1.0;
        notes.push_back(note);
    }
    layer.setNotes(notes);

    int commits = 0;
    layer.setOnNotesChanged([&commits](const std::vector<MidiNote>&) { ++commits; });
    const float y = pitchRowCenter(60);
    check(layer.onMouseEvent(mouseDown(10.0f, y, NUIMouseButton::Right)),
          "erase stroke press should be handled");
    check(layer.onMouseEvent(mouseDrag(90.0f, y, NUIMouseButton::Right)),
          "erase stroke should consume the second note");
    check(layer.onMouseEvent(mouseDrag(170.0f, y, NUIMouseButton::Right)),
          "erase stroke should consume the third note");
    check(layer.onMouseEvent(mouseUp(170.0f, y, NUIMouseButton::Right)),
          "erase stroke release should be handled");

    check(layer.getNotes().empty(), "right-drag should erase every crossed note");
    check(commits == 1, "an erase stroke should commit once on release");
    layer.undo();
    check(layer.getNotes().size() == 3, "one undo should restore the complete erase stroke");
}

void testSelectionHandleStretchesPhraseTimingAsOneEdit() {
    PianoRollNoteLayer layer;
    layer.setBounds({0.0f, 0.0f, 800.0f, 3072.0f});
    layer.setSnap(SnapGrid::Beat);

    MidiNote root;
    root.pitch = 60;
    root.startBeat = 0.0;
    root.durationBeats = 1.0;
    root.selected = true;
    MidiNote upper = root;
    upper.pitch = 64;
    upper.startBeat = 2.0;
    upper.durationBeats = 2.0;
    layer.setNotes({root, upper});

    int commits = 0;
    layer.setOnNotesChanged([&commits](const std::vector<MidiNote>&) { ++commits; });

    // The two selected notes span beats 0..4. Their shared handle sits at beat
    // 4, vertically centred on the selection frame; drag it to beat 8.
    constexpr float HANDLE_Y = 1572.0f;
    check(layer.onMouseEvent(mouseDown(320.0f, HANDLE_Y, NUIMouseButton::Left)),
          "unchanged selection stretch press should be handled");
    check(layer.onMouseEvent(mouseUp(320.0f, HANDLE_Y, NUIMouseButton::Left)),
          "unchanged selection stretch release should be handled");
    check(commits == 0, "an unchanged selection stretch should not commit");

    check(layer.onMouseEvent(mouseDown(320.0f, HANDLE_Y, NUIMouseButton::Left)),
          "selection stretch handle press should be handled");
    check(layer.onMouseEvent(mouseDrag(640.0f, HANDLE_Y, NUIMouseButton::Left)),
          "selection stretch drag should be handled");
    check(layer.onMouseEvent(mouseUp(640.0f, HANDLE_Y, NUIMouseButton::Left)),
          "selection stretch release should be handled");

    const auto& stretched = layer.getNotes();
    check(stretched.size() == 2, "selection stretch must preserve note count");
    check(std::abs(stretched[0].startBeat - 0.0) < 0.001 && std::abs(stretched[0].durationBeats - 2.0) < 0.001,
          "selection stretch should keep the anchor and scale its length");
    check(std::abs(stretched[1].startBeat - 4.0) < 0.001 && std::abs(stretched[1].durationBeats - 4.0) < 0.001,
          "selection stretch should scale later starts and lengths proportionally");
    check(commits == 1, "selection stretch should commit once on release");

    layer.undo();
    const auto& restored = layer.getNotes();
    check(std::abs(restored[0].durationBeats - 1.0) < 0.001 && std::abs(restored[1].startBeat - 2.0) < 0.001 &&
              std::abs(restored[1].durationBeats - 2.0) < 0.001,
          "one undo should restore the complete phrase timing");
}

void testSubdivideSelectionUsesSnapAndPreservesNoteData() {
    PianoRollNoteLayer layer;
    layer.setSnap(SnapGrid::Quarter);

    MidiNote selected;
    selected.pitch = 60;
    selected.startBeat = 0.125;
    selected.durationBeats = 0.75;
    selected.velocity = 0.61f;
    selected.pan = -0.3f;
    selected.unitId = 42;
    selected.selected = true;

    MidiNote untouched = selected;
    untouched.pitch = 64;
    untouched.startBeat = 2.0;
    untouched.durationBeats = 1.0;
    untouched.selected = false;
    layer.setNotes({selected, untouched});

    int commits = 0;
    layer.setOnNotesChanged([&commits](const std::vector<MidiNote>&) { ++commits; });
    check(layer.onKeyEvent(keyPress(NUIKeyCode::G, NUIModifiers::Ctrl | NUIModifiers::Shift)),
          "Ctrl+Shift+G should route to subdivision");

    const auto& notes = layer.getNotes();
    check(notes.size() == 4, "subdivision should replace one three-cell note without touching other notes");
    for (double beat : {0.125, 0.375, 0.625}) {
        const auto fragment = std::find_if(notes.begin(), notes.end(), [beat](const MidiNote& note) {
            return note.pitch == 60 && std::abs(note.startBeat - beat) < 0.001;
        });
        check(fragment != notes.end(), "subdivision should preserve each sequential segment start");
        if (fragment != notes.end()) {
            check(std::abs(fragment->durationBeats - 0.25) < 0.001,
                  "subdivision should use the current snap duration");
            check(fragment->selected && std::abs(fragment->velocity - 0.61f) < 0.001f &&
                      std::abs(fragment->pan + 0.3f) < 0.001f && fragment->unitId == 42,
                  "subdivision should preserve selected-note expression and routing");
        }
    }
    check(std::any_of(notes.begin(), notes.end(), [](const MidiNote& note) {
              return note.pitch == 64 && !note.selected && std::abs(note.startBeat - 2.0) < 0.001 &&
                     std::abs(note.durationBeats - 1.0) < 0.001;
          }),
          "subdivision should leave unselected notes unchanged");
    check(commits == 1, "subdivision should commit once");

    layer.undo();
    const auto& restored = layer.getNotes();
    check(restored.size() == 2, "one undo should restore the original selected note");
    const auto original = std::find_if(restored.begin(), restored.end(), [](const MidiNote& note) {
        return note.pitch == 60;
    });
    check(original != restored.end() && std::abs(original->startBeat - 0.125) < 0.001 &&
              std::abs(original->durationBeats - 0.75) < 0.001,
          "undo should restore the original note timing");
}

void testSubdividePreservesPartialTailAndHonorsNoSnap() {
    PianoRollNoteLayer layer;
    layer.setSnap(SnapGrid::Quarter);

    MidiNote note;
    note.pitch = 72;
    note.startBeat = 1.0;
    note.durationBeats = 0.6;
    note.selected = true;
    layer.setNotes({note});
    layer.subdivideSelectedNotes();

    const auto& subdivided = layer.getNotes();
    check(subdivided.size() == 3, "a partial final grid cell should become its own note");
    check(std::abs(subdivided[0].durationBeats - 0.25) < 0.001 &&
              std::abs(subdivided[1].durationBeats - 0.25) < 0.001 &&
              std::abs(subdivided[2].durationBeats - 0.1) < 0.001,
          "subdivision should retain the exact final remainder");
    check(std::abs(subdivided.back().startBeat + subdivided.back().durationBeats - 1.6) < 0.001,
          "subdivision should preserve the selected note's original end");

    layer.undo();
    layer.setSnap(SnapGrid::None);
    int commits = 0;
    layer.setOnNotesChanged([&commits](const std::vector<MidiNote>&) { ++commits; });
    layer.subdivideSelectedNotes();
    check(layer.getNotes().size() == 1 && std::abs(layer.getNotes()[0].durationBeats - 0.6) < 0.001,
          "subdivision should not invent a grid when snapping is disabled");
    check(commits == 0, "a no-snap subdivision should not create an edit");
}

} // namespace

int main() {
    testChordBrushPaintsCompleteTriadsAsOneEdit();
    testRightDragEraseIsOneUndoableStroke();
    testSelectionHandleStretchesPhraseTimingAsOneEdit();
    testSubdivideSelectionUsesSnapAndPreservesNoteData();
    testSubdividePreservesPartialTailAndHonorsNoSnap();

    if (g_failures == 0) {
        std::cout << "Piano Roll stroke interaction tests passed\n";
    }
    return g_failures == 0 ? 0 : 1;
}
