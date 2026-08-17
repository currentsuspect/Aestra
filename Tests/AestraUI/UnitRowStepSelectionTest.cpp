// © 2026 Aestra Studios — All Rights Reserved.
// Regression coverage for selection-based step editing: click-to-select
// grammar (no click-to-delete), Ctrl+B span duplication, Delete/Backspace
// removal, and the duplicate-becomes-selection contract.

#include "UnitRow.h"
#include "PatternSource.h"
#include "TrackManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

NUIKeyEvent keyPress(NUIKeyCode key, NUIModifiers modifiers = NUIModifiers::None) {
    NUIKeyEvent event;
    event.keyCode = key;
    event.modifiers = modifiers;
    event.pressed = true;
    return event;
}

NUIMouseEvent leftPress(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Down;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    event.pressed = true;
    return event;
}

NUIMouseEvent leftRelease(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Up;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    event.released = true;
    return event;
}

NUIMouseEvent dragTo(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Drag;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    return event;
}

NUIMouseEvent rightPress(float x, float y) {
    NUIMouseEvent event;
    event.type = NUIMouseEventType::Down;
    event.position = {x, y};
    event.button = NUIMouseButton::Right;
    event.pressed = true;
    return event;
}

// Geometry mirror for a 700x56 row (m_controlWidth = 700*0.38 = 266,
// separator at 266, context inset 8, lane padding 6, group gap 2, 16 steps):
// cell centers must match resolveGridStep's drawn-pad containment exactly.
float cellCenterX(int step) {
    const float controlWidth = 700.0f * 0.38f;
    const float gridX = controlWidth + 8.0f + 6.0f;
    const float avail = 700.0f - (controlWidth + 2.0f) - 8.0f - 12.0f;
    const float stepWidth = (avail - ((16 + 3) / 4) * 2.0f) / 16.0f;
    return gridX + step * stepWidth + (step / 4) * 2.0f + stepWidth * 0.5f;
}

float cellGapX(int step) {
    return cellCenterX(step) + (cellCenterX(1) - cellCenterX(0)) * 0.5f;
}

struct Fixture {
    std::shared_ptr<Aestra::Audio::TrackManager> tm{std::make_shared<Aestra::Audio::TrackManager>()};
    Aestra::Audio::UnitID unitId{0};
    Aestra::Audio::PatternID patternId{};
    std::shared_ptr<UnitRow> row;

    Fixture() {
        auto& unitMgr = tm->getUnitManager();
        unitId = unitMgr.createUnit("Test", Aestra::Audio::UnitType::Sampler);
        patternId = tm->getPatternManager().createPattern();
        tm->getPatternManager().applyPatch(patternId, [](Aestra::Audio::PatternSource& p) {
            p.type = Aestra::Audio::PatternSource::Type::Midi;
            p.lengthBeats = 4.0;
            p.payload = Aestra::Audio::MidiPayload{};
        });
        row = std::make_shared<UnitRow>(tm, unitMgr, unitId, patternId);
        row->setStepCount(16);
        row->updateState();
    }

    void addNote(int step, float velocity = 100.0f / 127.0f) {
        tm->getPatternManager().applyPatch(patternId, [this, step, velocity](Aestra::Audio::PatternSource& p) {
            Aestra::Audio::MidiNote note;
            note.pitch = 60; // root — keeps the step grid (not note-roll) mode
            note.startBeat = step * 0.25;
            note.durationBeats = 0.25;
            note.velocity = velocity;
            note.unitId = unitId;
            p.getMidiNotes().push_back(note);
        });
    }

    std::vector<int> stepsForUnit() const {
        std::vector<int> steps;
        const auto* pattern = tm->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isMidi()) {
            return steps;
        }
        for (const auto& n : pattern->getMidiNotes()) {
            if (n.unitId == unitId) {
                steps.push_back(static_cast<int>(std::lround(n.startBeat / 0.25)));
            }
        }
        std::sort(steps.begin(), steps.end());
        return steps;
    }

    float velocityAt(int step) const {
        const auto* pattern = tm->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isMidi()) {
            return -1.0f;
        }
        for (const auto& n : pattern->getMidiNotes()) {
            if (n.unitId == unitId && n.startBeat == step * 0.25) {
                return n.velocity;
            }
        }
        return -1.0f;
    }
};

void testDuplicateUsesOccupiedSpan() {
    Fixture f;
    f.addNote(1);
    f.addNote(3, 0.80f);
    f.addNote(4);

    std::vector<int> capturedSelection;
    int patternEditedCount = 0;
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });
    f.row->setOnPatternEdited([&](Aestra::Audio::PatternID) { ++patternEditedCount; });

    f.row->setStepSelection({1, 3, 4});
    check(f.row->onKeyEvent(keyPress(NUIKeyCode::B, NUIModifiers::Ctrl)), "Ctrl+B handled");

    check(f.stepsForUnit() == std::vector<int>({1, 3, 4, 5, 7, 8}),
          "duplicate lands right after the occupied span (1,3,4 -> 5,7,8)");
    check(capturedSelection == std::vector<int>({5, 7, 8}), "duplicated notes become the selection");
    check(patternEditedCount == 1, "one pattern-edit notification per duplicate");
    check(std::abs(f.velocityAt(7) - 0.80f) < 0.001f, "duplicate preserves velocity");
}

void testRepeatedDuplicateBuildsPhrases() {
    Fixture f;
    f.addNote(1);
    f.addNote(2);
    f.row->setStepSelection({1, 2});
    f.row->onKeyEvent(keyPress(NUIKeyCode::B, NUIModifiers::Ctrl)); // -> 3,4
    f.row->onKeyEvent(keyPress(NUIKeyCode::B, NUIModifiers::Ctrl)); // -> 5,6
    check(f.stepsForUnit() == std::vector<int>({1, 2, 3, 4, 5, 6}),
          "repeated Ctrl+B builds the phrase step by step");
}

void testDuplicateSkipsOccupiedDestination() {
    Fixture f;
    f.addNote(1);
    f.addNote(3);
    f.addNote(4);
    f.addNote(5); // occupies the would-be duplicate slot
    f.row->setStepSelection({1, 3, 4});
    f.row->onKeyEvent(keyPress(NUIKeyCode::B, NUIModifiers::Ctrl));
    check(f.stepsForUnit() == std::vector<int>({1, 3, 4, 5, 7, 8}),
          "duplicate skips occupied destinations without clobbering");
}

void testDeleteRemovesOnlySelectedNotes() {
    Fixture f;
    f.addNote(1);
    f.addNote(3);
    f.addNote(4);
    f.addNote(9);
    f.row->setStepSelection({1, 3, 4});

    std::vector<int> capturedSelection{1, 3, 4};
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    check(f.row->onKeyEvent(keyPress(NUIKeyCode::Delete)), "Delete handled");
    check(f.stepsForUnit() == std::vector<int>({9}), "Delete removes only the selected notes");
    check(capturedSelection.empty(), "selection cleared after delete");
}

void testBackspaceConsumedWithEmptySelection() {
    Fixture f;
    f.row->setStepSelection({});
    check(f.row->onKeyEvent(keyPress(NUIKeyCode::Backspace)), "Backspace consumed on a step grid");
    check(f.stepsForUnit().empty(), "no notes with empty selection");
}

// The reported regression: a unit whose pattern has notes but no loaded
// sample still draws pads, but the old interaction gate required content, so
// every click fell into the legacy toggle — left-click ERASED active notes
// and drags never painted. All four tests below run on a notes-only row.
void setupMouseRow(const std::shared_ptr<UnitRow>& row) {
    row->setBounds(0.0f, 0.0f, 700.0f, 56.0f);
    row->updateState();
}

void testLeftClickSelectsInsteadOfErasing() {
    Fixture f;
    f.addNote(2);
    setupMouseRow(f.row);

    std::vector<int> capturedSelection;
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    const float x = cellCenterX(2);
    check(f.row->onMouseEvent(leftPress(x, 28.0f)), "press on active note handled");
    check(f.row->onMouseEvent(leftRelease(x, 28.0f)), "release handled");
    check(f.stepsForUnit() == std::vector<int>({2}), "left-click on a note never erases it");
    check(capturedSelection == std::vector<int>({2}), "left-click selects the note");
}

void testGapClickDoesNotToggleNeighbor() {
    Fixture f;
    f.addNote(2);
    setupMouseRow(f.row);

    const float x = cellGapX(2);
    f.row->onMouseEvent(leftPress(x, 28.0f));
    f.row->onMouseEvent(leftRelease(x, 28.0f));
    check(f.stepsForUnit() == std::vector<int>({2}), "gap click does not toggle the adjacent pad");
}

void testPaintDragWorksWithoutSample() {
    Fixture f;
    setupMouseRow(f.row);

    std::vector<int> capturedSelection{-1};
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    const float y = 28.0f;
    check(f.row->onMouseEvent(leftPress(cellCenterX(4), y)), "press on empty pad handled");
    f.row->onMouseEvent(dragTo(cellCenterX(5), y));
    f.row->onMouseEvent(dragTo(cellCenterX(6), y));
    f.row->onMouseEvent(dragTo(cellCenterX(7), y));
    f.row->onMouseEvent(leftRelease(cellCenterX(7), y));

    check(f.stepsForUnit() == std::vector<int>({4, 5, 6, 7}), "left-drag paints the crossed range");
    check(capturedSelection == std::vector<int>{-1},
          "painted pads are placed, not auto-selected (placement never hijacks selection)");
}

void testClickOnEmptyPlacesWithoutSelecting() {
    Fixture f;
    f.addNote(1);
    setupMouseRow(f.row);

    std::vector<int> capturedSelection{1}; // setStepSelection never notifies — mirror the pushed state
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    f.row->setStepSelection({1});

    // Plain click on an empty pad: places, selection untouched.
    const float x = cellCenterX(4);
    f.row->onMouseEvent(leftPress(x, 28.0f));
    f.row->onMouseEvent(leftRelease(x, 28.0f));
    check(f.stepsForUnit() == std::vector<int>({1, 4}), "click on empty pad places a note");
    check(capturedSelection == std::vector<int>{1}, "placing a note does not select it");

    // Shift+click on an empty pad: same — placement is not selection.
    // Step outside the 400ms double-click-load window first (sample-less rows
    // treat two rapid taps as "open the file picker", by design).
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    const float x2 = cellCenterX(6);
    NUIMouseEvent shiftPress = leftPress(x2, 28.0f);
    shiftPress.modifiers = NUIModifiers::Shift;
    NUIMouseEvent shiftRelease = leftRelease(x2, 28.0f);
    shiftRelease.modifiers = NUIModifiers::Shift;
    f.row->onMouseEvent(shiftPress);
    f.row->onMouseEvent(shiftRelease);
    check(f.stepsForUnit() == std::vector<int>({1, 4, 6}), "shift+click on empty pad places a note");
    check(capturedSelection == std::vector<int>{1}, "shift-placement does not select either");
}

void testEscClearsSelection() {
    Fixture f;
    f.addNote(2);
    f.addNote(5);

    std::vector<int> capturedSelection{2, 5};
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    f.row->setStepSelection({2, 5});
    check(f.row->onKeyEvent(keyPress(NUIKeyCode::Escape)), "Esc handled");
    check(capturedSelection.empty(), "Esc dismisses the selection");
    check(f.stepsForUnit() == std::vector<int>({2, 5}), "Esc never touches notes");
}

void testRightClickErasesCell() {
    Fixture f;
    f.addNote(2);
    setupMouseRow(f.row);

    check(f.row->onMouseEvent(rightPress(cellCenterX(2), 28.0f)), "right-click on pad handled");
    check(f.stepsForUnit().empty(), "right-click on a pad erases it (no context menu on the grid)");
}

void testDoubleClickLoadsSampleOnNotesOnlyRow() {
    Fixture f;
    setupMouseRow(f.row);

    int loadCount = 0;
    f.row->setOnLoadUnitSample([&](Aestra::Audio::UnitID) { ++loadCount; });

    const float x = cellCenterX(2);
    f.row->onMouseEvent(leftPress(x, 28.0f));
    f.row->onMouseEvent(leftRelease(x, 28.0f));
    f.row->onMouseEvent(leftPress(x, 28.0f));
    check(loadCount == 1, "double-click on a sample-less unit opens the file picker");
}

void testArrowKeysMoveSelectedNotes() {
    Fixture f;
    f.addNote(1);
    f.addNote(3);
    f.addNote(4);

    std::vector<int> capturedSelection;
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    f.row->setStepSelection({1, 3, 4});
    check(f.row->onKeyEvent(keyPress(NUIKeyCode::Right)), "Right arrow handled");
    check(f.stepsForUnit() == std::vector<int>({2, 4, 5}), "Right moves the selected notes one step");
    check(capturedSelection == std::vector<int>({2, 4, 5}), "selection follows the moved notes");

    check(f.row->onKeyEvent(keyPress(NUIKeyCode::Left)), "Left arrow handled");
    check(f.stepsForUnit() == std::vector<int>({1, 3, 4}), "Left moves them back");
    check(capturedSelection == std::vector<int>({1, 3, 4}), "selection follows back");
}

void testMoveCascadesThroughAdjacentSelection() {
    Fixture f;
    f.addNote(1);
    f.addNote(2);
    f.row->setStepSelection({1, 2});
    f.row->onKeyEvent(keyPress(NUIKeyCode::Right));
    check(f.stepsForUnit() == std::vector<int>({2, 3}),
          "adjacent selected notes cascade into each other's vacated slots");
}

void testMoveBlockedByUnselectedWall() {
    Fixture f;
    f.addNote(2);
    f.addNote(3); // wall — not selected

    std::vector<int> capturedSelection{2};
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    f.row->setStepSelection({2});
    f.row->onKeyEvent(keyPress(NUIKeyCode::Right));
    check(f.stepsForUnit() == std::vector<int>({2, 3}), "move blocked by an unselected note");
    check(capturedSelection == std::vector<int>({2}), "blocked note stays selected in place");
}

void testMoveCascadeBlockedByWall() {
    Fixture f;
    f.addNote(1);
    f.addNote(2);
    f.addNote(3);
    f.row->setStepSelection({1, 2});
    f.row->onKeyEvent(keyPress(NUIKeyCode::Right));
    check(f.stepsForUnit() == std::vector<int>({1, 2, 3}), "wall blocks the whole cascade");
}

void testMoveClampedAtGridEdges() {
    Fixture f;
    f.addNote(0);
    f.addNote(15);
    f.row->setStepSelection({0, 15});
    f.row->onKeyEvent(keyPress(NUIKeyCode::Left));
    check(f.stepsForUnit() == std::vector<int>({0, 14}),
          "Left clamps step 0 but still moves step 15 (per-step independence)");
    f.row->onKeyEvent(keyPress(NUIKeyCode::Right));
    check(f.stepsForUnit() == std::vector<int>({1, 15}),
          "Right clamps step 15 but still moves step 0");
}

void testArrowVelocityNudge() {
    Fixture f;
    f.addNote(2, 0.50f);
    f.row->setStepSelection({2});

    f.row->onKeyEvent(keyPress(NUIKeyCode::Up));
    check(std::abs(f.velocityAt(2) - 0.55f) < 0.001f, "Up nudges velocity louder");

    f.row->onKeyEvent(keyPress(NUIKeyCode::Down));
    f.row->onKeyEvent(keyPress(NUIKeyCode::Down));
    check(std::abs(f.velocityAt(2) - 0.45f) < 0.001f, "Down nudges velocity quieter");

    for (int i = 0; i < 30; ++i) {
        f.row->onKeyEvent(keyPress(NUIKeyCode::Down));
    }
    check(std::abs(f.velocityAt(2) - 0.05f) < 0.001f, "velocity clamps at the minimum");
}

void testArrowsConsumedWithEmptySelection() {
    Fixture f;
    check(f.row->onKeyEvent(keyPress(NUIKeyCode::Right)), "Right consumed on a step grid");
    check(f.row->onKeyEvent(keyPress(NUIKeyCode::Up)), "Up consumed on a step grid");
    check(f.stepsForUnit().empty(), "no notes created by arrows with empty selection");
}

void testCtrlASelectsActiveNotes() {
    Fixture f;
    f.addNote(1);
    f.addNote(3);
    f.addNote(4);

    std::vector<int> capturedSelection;
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    check(f.row->onKeyEvent(keyPress(NUIKeyCode::A, NUIModifiers::Ctrl)), "Ctrl+A handled");
    check(capturedSelection == std::vector<int>({1, 3, 4}), "Ctrl+A selects the active notes only");
    check(f.stepsForUnit() == std::vector<int>({1, 3, 4}), "Ctrl+A never edits notes");
}

void testCtrlAWithNoNotesClearsSelection() {
    Fixture f;
    f.row->setStepSelection({2, 5});

    std::vector<int> capturedSelection{2, 5};
    f.row->setOnStepSelectionChanged(
        [&](Aestra::Audio::UnitID, const std::vector<int>& steps) { capturedSelection = steps; });

    f.row->onKeyEvent(keyPress(NUIKeyCode::A, NUIModifiers::Ctrl));
    check(capturedSelection.empty(), "Ctrl+A with no notes leaves nothing selected");
}

} // namespace

int main() {
    testDuplicateUsesOccupiedSpan();
    testRepeatedDuplicateBuildsPhrases();
    testDuplicateSkipsOccupiedDestination();
    testDeleteRemovesOnlySelectedNotes();
    testBackspaceConsumedWithEmptySelection();
    testLeftClickSelectsInsteadOfErasing();
    testGapClickDoesNotToggleNeighbor();
    testPaintDragWorksWithoutSample();
    testClickOnEmptyPlacesWithoutSelecting();
    testRightClickErasesCell();
    testDoubleClickLoadsSampleOnNotesOnlyRow();
    testArrowKeysMoveSelectedNotes();
    testMoveCascadesThroughAdjacentSelection();
    testMoveBlockedByUnselectedWall();
    testMoveCascadeBlockedByWall();
    testMoveClampedAtGridEdges();
    testArrowVelocityNudge();
    testArrowsConsumedWithEmptySelection();
    testCtrlASelectsActiveNotes();
    testCtrlAWithNoNotesClearsSelection();
    testEscClearsSelection();

    if (g_failures == 0) {
        std::cout << "UnitRowStepSelectionTest: all checks passed\n";
        return 0;
    }
    std::cout << "UnitRowStepSelectionTest: " << g_failures << " failure(s)\n";
    return 1;
}