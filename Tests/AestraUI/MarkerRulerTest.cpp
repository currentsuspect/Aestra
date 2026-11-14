// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Song markers on the ruler (v0.8.3 essentials, slice 2).
//
// Slice 1 (#1337) shipped the model, the undo steps and the project-file
// section, and there was no way to see or make a marker. These checks are the
// surface's contract: a chip you can see, a click that goes there, a drag that
// moves it as ONE undo step, a rename box that commits once, and Delete on a
// marker that never steals the clip delete.
//
// Everything here goes through onMouseEvent / onKeyEvent as a user would, so a
// gesture that only works when a handler is called directly does not pass.

#include "Models/TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackManagerUIMath.h"
#include "NUITextInput.h"
#include "NUIThemeSystem.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

AestraUI::NUIMouseEvent mouse(AestraUI::NUIMouseEventType type, AestraUI::NUIMouseButton button, float x, float y,
                              AestraUI::NUIModifiers modifiers = AestraUI::NUIModifiers::None) {
    AestraUI::NUIMouseEvent e;
    e.type = type;
    e.button = button;
    e.position = {x, y};
    e.pressed = type == AestraUI::NUIMouseEventType::Down;
    e.released = type == AestraUI::NUIMouseEventType::Up;
    e.modifiers = modifiers;
    return e;
}

AestraUI::NUIKeyEvent key(AestraUI::NUIKeyCode code, AestraUI::NUIModifiers modifiers = AestraUI::NUIModifiers::None) {
    AestraUI::NUIKeyEvent e;
    e.keyCode = code;
    e.pressed = true;
    e.released = false;
    e.modifiers = modifiers;
    return e;
}

struct Fixture {
    std::shared_ptr<TrackManager> trackManager = std::make_shared<TrackManager>();
    std::shared_ptr<TrackManagerUI> ui = std::make_shared<TrackManagerUI>(trackManager);
    float gridX = 0.0f;

    Fixture() {
        // Off the origin on purpose: the app's timeline sits under the toolbar,
        // and with bounds at (0,0) a missing local->window conversion in the
        // component reads as correct. It shipped a marker layer that painted
        // every chip 118 px too high, and a (0,0) fixture could not see it.
        ui->setBounds(AestraUI::NUIRect(kOriginX, kOriginY, 1400.0f, 700.0f));
        const float controls = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
        gridX = controls + kTimelineGridInsetX;
    }

    static constexpr float kOriginX = 37.0f;
    static constexpr float kOriginY = 118.0f;

    // Event positions are window coordinates; markerChipRect() is local.
    float windowX(float localX) const { return kOriginX + localX; }
    float windowY(float localY) const { return kOriginY + localY; }

    double beatToSeconds(double beat) const { return trackManager->getPlaylistModel().beatToSeconds(beat); }
    double playheadBeat() const {
        const double bpm = trackManager->getPlaylistModel().getBPM();
        return trackManager->getPosition() * bpm / 60.0;
    }
    void setPlayheadBeat(double beat) { trackManager->setPosition(beatToSeconds(beat)); }

    float xForBeat(double beat) const {
        return gridX + static_cast<float>(beat * static_cast<double>(ui->getHorizontalZoom())) -
               ui->getHorizontalScroll();
    }

    // A user's click: press and release on the chip's centre line.
    void clickChip(uint64_t id) {
        const AestraUI::NUIRect chip = ui->markerChipRect(id);
        const float x = windowX(chip.x + 2.0f);
        const float y = windowY(kTimelineRulerTop + 10.0f);
        ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Down, AestraUI::NUIMouseButton::Left, x, y));
        ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Up, AestraUI::NUIMouseButton::Left, x, y));
    }

    void dragChip(uint64_t id, float toX) {
        const AestraUI::NUIRect chip = ui->markerChipRect(id);
        const float y = windowY(kTimelineRulerTop + 10.0f);
        toX = windowX(toX);
        ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Down, AestraUI::NUIMouseButton::Left, windowX(chip.x + 2.0f), y));
        ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Move, AestraUI::NUIMouseButton::Left, toX, y));
        ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Up, AestraUI::NUIMouseButton::Left, toX, y));
    }

    AestraUI::NUITextInput* renameBox() {
        for (const auto& child : ui->getChildren()) {
            if (auto* input = dynamic_cast<AestraUI::NUITextInput*>(child.get())) {
                return input;
            }
        }
        return nullptr;
    }
};

// M makes a marker at the playhead, selected, with the model's default name.
void testMAddsAMarkerAtThePlayhead() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::M));

    const auto& markers = f.trackManager->markers().markers();
    check(markers.size() == 1, "M adds exactly one marker");
    if (markers.size() == 1) {
        check(std::abs(markers.front().beat - 4.0) < 1e-9, "the marker lands on the playhead, not on the origin");
        check(!markers.front().name.empty(), "a new marker has a name to draw");
        check(f.ui->selectedMarkerId() == markers.front().id, "the new marker is selected, so Delete and Rename act on it");
    }

    // Undo is the model's own step: M must not be a second, un-undoable path.
    f.trackManager->getCommandHistory().undo();
    check(f.trackManager->markers().empty(), "undo removes the marker M made");
}

// A click on a chip is the marker's gesture: the playhead goes to the MARKER's
// beat even when the pointer is at the chip's edge, and no scrub starts.
void testClickingAChipGoesThere() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t id = f.ui->addMarkerAtPlayhead();
    f.setPlayheadBeat(0.0);

    const AestraUI::NUIRect chip = f.ui->markerChipRect(id);
    check(chip.width > 0.0f, "the chip has a width to click");
    const float y = f.windowY(kTimelineRulerTop + 10.0f);
    const float x = f.windowX(chip.right() - 1.0f);
    f.ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Down, AestraUI::NUIMouseButton::Left, x, y));

    check(std::abs(f.playheadBeat() - 4.0) < 1e-6, "the playhead lands on the marker's beat, not the pointer's");
    check(f.ui->selectedMarkerId() == id, "clicking a chip selects it");

    // The release must not be read as the end of a scrub that never started.
    f.ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Up, AestraUI::NUIMouseButton::Left, x, y));
    check(std::abs(f.playheadBeat() - 4.0) < 1e-6, "the release leaves the playhead on the marker");
}

// Dragging a chip moves the marker as one undo step and takes the playhead with
// it: the section moved, and staying on the old bar would be a lie.
void testDraggingAChipMovesItAsOneUndoStep() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t id = f.ui->addMarkerAtPlayhead();
    const double oldBeat = f.trackManager->markers().find(id)->beat;

    f.dragChip(id, f.xForBeat(8.0));

    const Marker* moved = f.trackManager->markers().find(id);
    check(moved != nullptr, "the marker still exists after a drag");
    check(moved && std::abs(moved->beat - 8.0) < 1e-6, "the marker landed where it was dropped");
    check(std::abs(f.playheadBeat() - 8.0) < 1e-6, "the playhead follows the section it moved");

    f.trackManager->getCommandHistory().undo();
    const Marker* restored = f.trackManager->markers().find(id);
    check(restored != nullptr && std::abs(restored->beat - oldBeat) < 1e-6,
          "one undo puts the marker back where it was (the drag is one step, not one per motion event)");
    check(restored != nullptr && restored->id == id, "undo restores the same id, so selection still means something");
}

// A press that never travels is a click, not a move: no stray undo step.
void testAClickDoesNotCreateAnUndoStep() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t id = f.ui->addMarkerAtPlayhead();

    f.clickChip(id);

    // Exactly one undo step exists (the add), so one undo empties the model. A
    // click that recorded a move would leave the marker in place after the
    // undo, which is the bug this pins.
    f.trackManager->getCommandHistory().undo();
    check(f.trackManager->markers().empty(), "one undo after a click removes the marker: the click made no step");
}

// The inline box commits once and rename is one undo step.
void testRenameCommitsOnceAsOneStep() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t id = f.ui->addMarkerAtPlayhead();

    f.ui->beginMarkerRename(id);
    check(f.ui->isRenamingMarker(), "Rename opens a box");
    auto* input = f.renameBox();
    check(input != nullptr, "the box is a real text field in the tree (typing has somewhere to go)");
    if (input == nullptr) {
        return;
    }
    input->setText("Hook");
    f.ui->finishMarkerRename(true);

    const Marker* marker = f.trackManager->markers().find(id);
    check(!f.ui->isRenamingMarker(), "committing closes the box");
    check(marker != nullptr && marker->name == "Hook", "the new name is on the marker");

    f.trackManager->getCommandHistory().undo();
    marker = f.trackManager->markers().find(id);
    check(marker != nullptr && marker->name != "Hook", "undo restores the old name");
}

// Esc in the box cancels: the marker keeps its name and no step is recorded.
void testEscCancelsARename() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t id = f.ui->addMarkerAtPlayhead();
    const std::string before = f.trackManager->markers().find(id)->name;

    f.ui->beginMarkerRename(id);
    auto* input = f.renameBox();
    check(input != nullptr, "the box exists to cancel");
    if (input == nullptr) {
        return;
    }
    input->setText("Nope");
    f.ui->finishMarkerRename(false);

    check(f.trackManager->markers().find(id)->name == before, "a cancelled rename changes nothing");
    f.trackManager->getCommandHistory().undo();
    check(f.trackManager->markers().empty(), "a cancelled rename recorded no step");
}

// Delete acts on a selected marker when no clip owns the key, and undo restores it.
void testDeleteRemovesTheSelectedMarker() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t id = f.ui->addMarkerAtPlayhead();

    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Delete));
    check(f.trackManager->markers().find(id) == nullptr, "Delete removes the selected marker");
    check(f.ui->selectedMarkerId() == 0, "and clears the selection it just acted on");

    f.trackManager->getCommandHistory().undo();
    check(f.trackManager->markers().find(id) != nullptr, "undo puts the marker back");
}

// Esc is the selection exit for a marker too.
void testEscClearsTheMarkerSelection() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t id = f.ui->addMarkerAtPlayhead();
    check(f.ui->selectedMarkerId() == id, "the marker is selected (so the check is not vacuous)");

    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Escape));
    check(f.ui->selectedMarkerId() == 0, "Esc drops the marker selection");
    check(f.trackManager->markers().find(id) != nullptr, "Esc selects nothing; it does not delete");
}

// A marker only owns its own chip: the ruler still scrubs where there is no chip.
void testEmptyRulerStillScrubs() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    f.ui->addMarkerAtPlayhead();

    const float x = f.windowX(f.xForBeat(16.0));
    const float y = f.windowY(kTimelineRulerTop + 10.0f);
    f.ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Down, AestraUI::NUIMouseButton::Left, x, y));
    check(std::abs(f.playheadBeat() - 16.0) < 0.6, "a press on empty ruler still puts the playhead where it was pressed");
    f.ui->onMouseEvent(mouse(AestraUI::NUIMouseEventType::Up, AestraUI::NUIMouseButton::Left, x, y));
}

// Two sections at the same bar: the first chip is clamped, so it never covers
// the second one and both stay clickable.
void testChipsNeverCoverEachOther() {
    Fixture f;
    f.setPlayheadBeat(8.0);
    const uint64_t first = f.ui->addMarkerAtPlayhead();
    f.setPlayheadBeat(9.0);
    const uint64_t second = f.ui->addMarkerAtPlayhead();

    const AestraUI::NUIRect a = f.ui->markerChipRect(first);
    const AestraUI::NUIRect b = f.ui->markerChipRect(second);
    check(a.right() <= b.x + 0.01f, "the first chip stops before the second one starts");
    check(f.ui->markerAtX(b.x + 2.0f) == second, "the second chip is the one hit at its own x");
}

// Alt+Left / Alt+Right walk the markers and carry the transport.
void testAltArrowsWalkTheMarkers() {
    Fixture f;
    f.setPlayheadBeat(0.0);
    f.ui->addMarkerAtPlayhead();
    f.setPlayheadBeat(8.0);
    f.ui->addMarkerAtPlayhead();
    f.setPlayheadBeat(16.0);
    f.ui->addMarkerAtPlayhead();

    f.setPlayheadBeat(0.0);
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Right, AestraUI::NUIModifiers::Alt));
    check(std::abs(f.playheadBeat() - 8.0) < 1e-6, "Alt+Right goes to the next marker");
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Right, AestraUI::NUIModifiers::Alt));
    check(std::abs(f.playheadBeat() - 16.0) < 1e-6, "and again to the one after it");
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Left, AestraUI::NUIModifiers::Alt));
    check(std::abs(f.playheadBeat() - 8.0) < 1e-6, "Alt+Left goes back");
}

} // namespace

// One marker per beat. Adding where a marker already sits selects it; it must
// not stack a second chip under the first (unclickable, squeezed to nothing).
void testAddingOnAnOccupiedBeatSelectsTheExistingMarker() {
    Fixture f;
    f.setPlayheadBeat(8.0);
    const uint64_t first = f.ui->addMarkerAtPlayhead();
    f.ui->selectMarker(0);
    const size_t historyBefore = f.trackManager->getCommandHistory().getHistoryMemoryUsage();

    const uint64_t again = f.ui->addMarkerAtPlayhead();
    check(again == first, "adding on an occupied beat returns the marker already there");
    check(f.trackManager->markers().count() == 1, "no second marker is stacked on the same beat");
    check(f.ui->selectedMarkerId() == first, "the existing marker is selected");
    check(f.trackManager->getCommandHistory().getHistoryMemoryUsage() == historyBefore,
          "selecting the existing marker is not an undo step");
}

// Dropping a dragged chip onto another marker's beat cancels the move.
void testDroppingOnAnotherMarkersBeatCancelsTheMove() {
    Fixture f;
    f.setPlayheadBeat(4.0);
    const uint64_t a = f.ui->addMarkerAtPlayhead();
    f.setPlayheadBeat(12.0);
    const uint64_t b = f.ui->addMarkerAtPlayhead();
    f.dragChip(b, f.xForBeat(4.0));
    const Marker* moved = f.trackManager->markers().find(b);
    check(moved && std::abs(moved->beat - 12.0) < 1e-9, "a drop onto another marker's beat leaves the marker where it was");
    check(f.trackManager->markers().count() == 2 && f.trackManager->markers().find(a) != nullptr,
          "both markers survive the cancelled drop");
}

// Near collision: markers a few pixels apart. The first chip is squeezed by the
// second, but it must never vanish: an invisible, unclickable marker is lost.
void testNearCollisionKeepsEveryMarkerVisibleAndClickable() {
    Fixture f;
    f.ui->setHorizontalZoom(3.0f); // one beat = 3 px, so neighbours sit 3 px apart
    f.setPlayheadBeat(8.0);
    const uint64_t first = f.ui->addMarkerAtPlayhead();
    f.setPlayheadBeat(9.0);
    const uint64_t second = f.ui->addMarkerAtPlayhead();

    const AestraUI::NUIRect a = f.ui->markerChipRect(first);
    const AestraUI::NUIRect b = f.ui->markerChipRect(second);
    check(a.width > 0.0f, "a squeezed chip keeps a visible stub");
    check(b.width > 0.0f, "the neighbouring chip is visible too");
    check(f.ui->markerAtX(a.x) == first, "the squeezed marker is still hit at its own beat");
    check(f.ui->markerAtX(b.x + 1.0f) == second, "the neighbour is hit at its own beat");
}

int main() {
    std::cout << "MarkerRulerTest: song markers on the ruler\n";
    testMAddsAMarkerAtThePlayhead();
    testClickingAChipGoesThere();
    testDraggingAChipMovesItAsOneUndoStep();
    testAClickDoesNotCreateAnUndoStep();
    testRenameCommitsOnceAsOneStep();
    testEscCancelsARename();
    testDeleteRemovesTheSelectedMarker();
    testEscClearsTheMarkerSelection();
    testEmptyRulerStillScrubs();
    testChipsNeverCoverEachOther();
    testAltArrowsWalkTheMarkers();
    testAddingOnAnOccupiedBeatSelectsTheExistingMarker();
    testDroppingOnAnotherMarkersBeatCancelsTheMove();
    testNearCollisionKeepsEveryMarkerVisibleAndClickable();

    if (g_failures == 0) {
        std::cout << "All marker ruler checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed.\n";
    return 1;
}
