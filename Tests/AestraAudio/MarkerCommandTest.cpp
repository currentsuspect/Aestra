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

} // namespace

int main() {
    testAddUndoRedoKeepsTheId();
    testDeleteUndoRestoresPositionAndName();
    testRenameUndoRestoresTheOldName();
    testCommandsOnAMissingMarkerDoNothing();
    testMarkersMarkTheProjectModified();

    if (g_failures) {
        std::printf("%d MarkerCommand test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All MarkerCommand tests passed\n");
    return 0;
}
