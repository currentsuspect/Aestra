// © 2026 Aestra Studios — All Rights Reserved.
// Arsenal unit lifecycle regression coverage: unit creation/deletion must not leave orphan patterns
// and both operations must restore the same stable identity on undo/redo.

#include "Commands/AddUnitCommand.h"
#include "Commands/CommandHistory.h"
#include "Commands/DeleteUnitCommand.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++failures;
}
}

int main() {
    {
        TrackManager manager;
        manager.getUnitManager().setPatternManager(&manager.getPatternManager()); // the app wires this (AestraContent)
        CommandHistory& history = manager.getCommandHistory();
        auto add = std::make_shared<AddUnitCommand>(manager.getUnitManager(), "Hi-Hat", UnitType::Sampler);
        history.pushAndExecute(add);
        const UnitID id = add->getCreatedUnitId();
        const PatternID pattern = manager.getUnitManager().getUnit(id)->defaultPatternId;

        check(id != 0 && manager.getUnitManager().getUnit(id) != nullptr, "unit is created");
        check(pattern.isValid() && manager.getPatternManager().getPattern(pattern) != nullptr, "default pattern is created with the unit");

        history.undo();
        check(manager.getUnitManager().getUnit(id) == nullptr, "undo removes the unit");
        check(manager.getPatternManager().getPattern(pattern) == nullptr, "undo removes the unit's default pattern");

        history.redo();
        check(manager.getUnitManager().getUnit(id) != nullptr, "redo restores the same unit id");
        check(manager.getUnitManager().getUnit(id)->defaultPatternId == pattern, "redo restores the same default pattern id");
    }

    {
        TrackManager manager;
        manager.getUnitManager().setPatternManager(&manager.getPatternManager()); // the app wires this (AestraContent)
        auto& units = manager.getUnitManager();
        const UnitID id = units.createUnit("Snare", UnitType::Sampler);
        const PatternID pattern = units.getUnit(id)->defaultPatternId;
        auto* source = manager.getPatternManager().getPattern(pattern);
        source->getMidiNotes().push_back(MidiNote{38, 0.0, 0.25, 0.8f, 0.0f, id});

        auto command = std::make_shared<DeleteUnitCommand>(manager, id, 0);
        manager.getCommandHistory().pushAndExecute(command);
        check(units.getUnit(id) == nullptr, "delete removes the unit");
        check(manager.getPatternManager().getPattern(pattern) == nullptr, "delete removes an unplaced default pattern");
        check(command->isUndoable(), "delete command remains undoable");

        manager.getCommandHistory().undo();
        check(units.getUnit(id) != nullptr, "undo restores the same unit id");
        check(manager.getPatternManager().getPattern(pattern) != nullptr, "undo restores the default pattern");
        check(manager.getPatternManager().getPattern(pattern)->getMidiNotes().size() == 1, "undo restores the unit's notes");

        manager.getCommandHistory().redo();
        check(units.getUnit(id) == nullptr && manager.getPatternManager().getPattern(pattern) == nullptr,
              "redo deletes the same unit and pattern again");
    }

    std::cout << (failures == 0 ? "ArsenalUnitCommandsTest: all passed\n" : "ArsenalUnitCommandsTest: FAILURES\n");
    return failures == 0 ? 0 : 1;
}
