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

} // namespace

int main() {
    testChordBrushPaintsCompleteTriadsAsOneEdit();
    testRightDragEraseIsOneUndoableStroke();

    if (g_failures == 0) {
        std::cout << "Piano Roll stroke interaction tests passed\n";
    }
    return g_failures == 0 ? 0 : 1;
}
