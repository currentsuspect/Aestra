// © 2026 Aestra Studios — All Rights Reserved.
//
// MarkerCommandTest — markers are undoable edits, and undo restores the same marker id.
//
// The id is the contract: the ruler's selection and anything holding a marker id must still point at the same marker
// after undo/redo. A fresh id after undo is the bug this test exists to catch (the same trap CreateTrackWithLaneCommand
// documents for lanes).

#include "Commands/CommandHistory.h"
#include "Commands/MarkerCommands.h"
#include "Models/TrackManager.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

void testAddUndoRedoKeepsTheId() {
    auto tm = std::make_shared<TrackManager>();
    auto& history = tm->getCommandHistory();

    auto add = std::make_shared<AddMarkerCommand>(*tm, 16.0, "Verse");
    history.pushAndExecute(add);
    const uint64_t id = add->markerId();
    check(id != 0 && tm->markers().count() == 1, "AddCreatesTheMarker");

    check(history.undo(), "UndoReturnsTrue");
    check(tm->markers().empty(), "UndoRemovesTheMarker");

    check(history.redo(), "RedoReturnsTrue");
    const Marker* marker = tm->markers().find(id);
    check(marker != nullptr && marker->name == "Verse" && marker->beat == 16.0, "RedoRestoresTheSameId");
}

void testDeleteUndoRestoresPositionAndName() {
    auto tm = std::make_shared<TrackManager>();
    auto& history = tm->getCommandHistory();

    auto add = std::make_shared<AddMarkerCommand>(*tm, 32.0, "Hook");
    history.pushAndExecute(add);
    const uint64_t id = add->markerId();

    history.pushAndExecute(std::make_shared<DeleteMarkerCommand>(*tm, id));
    check(tm->markers().empty(), "DeleteRemovesTheMarker");

    check(history.undo(), "UndoDeleteReturnsTrue");
    const Marker* marker = tm->markers().find(id);
    check(marker != nullptr && marker->name == "Hook" && marker->beat == 32.0, "UndoBringsBackTheSameMarker");
}

void testRenameUndoRestoresTheOldName() {
    auto tm = std::make_shared<TrackManager>();
    auto& history = tm->getCommandHistory();

    auto add = std::make_shared<AddMarkerCommand>(*tm, 8.0, "Verse 1");
    history.pushAndExecute(add);
    const uint64_t id = add->markerId();

    history.pushAndExecute(std::make_shared<RenameMarkerCommand>(*tm, id, "Chorus"));
    check(tm->markers().find(id)->name == "Chorus", "RenameApplies");

    check(history.undo(), "UndoRenameReturnsTrue");
    check(tm->markers().find(id)->name == "Verse 1", "UndoRestoresTheOldName");

    check(history.redo(), "RedoRenameReturnsTrue");
    check(tm->markers().find(id)->name == "Chorus", "RedoAppliesTheNewName");
}

void testCommandsOnAMissingMarkerDoNothing() {
    auto tm = std::make_shared<TrackManager>();
    auto& history = tm->getCommandHistory();

    history.pushAndExecute(std::make_shared<DeleteMarkerCommand>(*tm, 4242));
    history.pushAndExecute(std::make_shared<RenameMarkerCommand>(*tm, 4242, "Nope"));
    check(tm->markers().empty(), "CommandsOnAMissingMarkerChangeNothing");
}

void testMarkersMarkTheProjectModified() {
    auto tm = std::make_shared<TrackManager>();
    tm->setModified(false);
    tm->getCommandHistory().pushAndExecute(std::make_shared<AddMarkerCommand>(*tm, 4.0, "Intro"));
    check(tm->isModified(), "AddingAMarkerMarksTheProjectModified");
}

struct Snap {
    uint64_t id;
    std::string name;
    double beat;
    bool operator==(const Snap& o) const { return id == o.id && name == o.name && beat == o.beat; }
};

std::vector<Snap> snapshot(TrackManager& tm) {
    std::vector<Snap> out;
    for (const auto& m : tm.markers().markers()) out.push_back({m.id, m.name, m.beat});
    return out;
}

// A mixed session undoes step by step back to empty, then redoes forward, and
// every intermediate state (ids, names, beats, order) matches the first pass.
void testAMixedSessionUndoesAndRedoesStepForStep() {
    auto tm = std::make_shared<TrackManager>();
    auto& history = tm->getCommandHistory();
    std::vector<std::vector<Snap>> states;
    states.push_back(snapshot(*tm));

    auto a = std::make_shared<AddMarkerCommand>(*tm, 4.0, "A");
    history.pushAndExecute(a);
    states.push_back(snapshot(*tm));
    auto b = std::make_shared<AddMarkerCommand>(*tm, 8.0, "B");
    history.pushAndExecute(b);
    states.push_back(snapshot(*tm));
    history.pushAndExecute(std::make_shared<RenameMarkerCommand>(*tm, a->markerId(), "A2"));
    states.push_back(snapshot(*tm));
    history.pushAndExecute(std::make_shared<MoveMarkerCommand>(*tm, b->markerId(), 2.0)); // crosses A: re-sorts
    states.push_back(snapshot(*tm));
    history.pushAndExecute(std::make_shared<DeleteMarkerCommand>(*tm, a->markerId()));
    states.push_back(snapshot(*tm));

    bool undoOk = true;
    for (size_t i = states.size() - 1; i > 0; --i) {
        history.undo();
        undoOk = undoOk && snapshot(*tm) == states[i - 1];
    }
    check(undoOk, "EveryUndoStepRestoresTheExactPreviousState");
    check(!history.undo(), "NothingLeftToUndoAfterTheFirstStep");

    bool redoOk = true;
    for (size_t i = 1; i < states.size(); ++i) {
        history.redo();
        redoOk = redoOk && snapshot(*tm) == states[i];
    }
    check(redoOk, "EveryRedoStepReproducesTheState");
}

// A rename that changes nothing, or that is only whitespace, is not an undo
// step: otherwise Ctrl+Z "does nothing" once and the user thinks it is broken.
void testNoOpRenamesAreNotUndoSteps() {
    auto tm = std::make_shared<TrackManager>();
    auto& history = tm->getCommandHistory();
    auto a = std::make_shared<AddMarkerCommand>(*tm, 4.0, "Verse");
    history.pushAndExecute(a);
    history.pushAndExecute(std::make_shared<RenameMarkerCommand>(*tm, a->markerId(), "Verse"));
    history.pushAndExecute(std::make_shared<RenameMarkerCommand>(*tm, a->markerId(), "   "));
    history.undo();
    check(tm->markers().empty(), "NoOpAndBlankRenamesLeaveNoUndoStep");
}

} // namespace

int main() {
    testAddUndoRedoKeepsTheId();
    testDeleteUndoRestoresPositionAndName();
    testRenameUndoRestoresTheOldName();
    testCommandsOnAMissingMarkerDoNothing();
    testMarkersMarkTheProjectModified();
    testAMixedSessionUndoesAndRedoesStepForStep();
    testNoOpRenamesAreNotUndoSteps();

    if (g_failures) {
        std::printf("%d MarkerCommand test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All MarkerCommand tests passed\n");
    return 0;
}
