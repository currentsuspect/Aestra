// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SPEC-017 criterion 4's inputs: how many steps an open version is missing, and the three switch answers.

#include "../../Source/Core/TakeManager.h"
#include "../../Source/Core/VersionEdits.h"
#include "../../Source/Core/VersionSteps.h"
#include "Commands/CommandHistory.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using Aestra::VersionEdits::Choice;
using Aestra::VersionEdits::State;

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

struct NamedCommand : Aestra::Audio::ICommand {
    explicit NamedCommand(std::string n) : name(std::move(n)) {}
    void execute() override {}
    void undo() override {}
    void redo() override {}
    std::string getName() const override { return name; }
    std::string name;
};

void push(Aestra::Audio::CommandHistory& history, const std::string& name) {
    history.pushAndExecute(std::make_shared<NamedCommand>(name));
}

/// A version holding the history's current steps, the way a save records them.
TakeManager::TakeEntry versionOf(const Aestra::Audio::CommandHistory& history) {
    TakeManager::TakeEntry version;
    version.id = "take_test";
    version.steps = Aestra::collectVersionSteps(history);
    return version;
}

} // namespace

int main() {
    {   // Just saved: nothing is unsaved, and the number is exact.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        push(history, "Set swing 18%");
        const auto version = versionOf(history);
        const State state = Aestra::VersionEdits::measure(history, &version);
        require(state.steps == 0 && state.counted && !state.diverged, "the saved state is clean");
    }

    {   // Three steps made since the save.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        const auto version = versionOf(history);
        push(history, "Move clip · Pad");
        push(history, "Set volume");
        push(history, "Rename track");
        const State state = Aestra::VersionEdits::measure(history, &version);
        require(state.steps == 3 && state.counted && state.diverged, "three steps since the saved version");
    }

    {   // Steps taken back: the project is not the version, and no number is claimed.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 5; ++i) push(history, "Step " + std::to_string(i));
        const auto version = versionOf(history);
        history.undo();
        history.undo();
        const State state = Aestra::VersionEdits::measure(history, &version);
        require(state.diverged, "undoing steps leaves the project different from the version");
        require(state.steps == 0 && !state.counted, "nothing new was made, so no count is shown");
    }

    {   // The regression this exists for: undo past the save point, then make as many steps as were undone.
        // The stack depth matches the recorded step count exactly, which the old subtraction read as clean.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 5; ++i) push(history, "Step " + std::to_string(i));
        const auto version = versionOf(history);
        history.undo();
        history.undo();
        push(history, "Different A");
        push(history, "Different B");
        require(static_cast<int>(history.getUndoStack().size()) == static_cast<int>(version.steps.size()),
                "the stack is as deep as the version recorded (the old counter's blind spot)");
        const State state = Aestra::VersionEdits::measure(history, &version);
        require(state.diverged, "a re-trunked history is still different from the version");
        require(state.steps == 2 && !state.counted, "the two new steps are the ones not in the version");
    }

    {   // A version opened fresh: its history was cleared, so nothing has been done since.
        Aestra::Audio::CommandHistory history;
        {
            Aestra::Audio::CommandHistory saved;
            for (int i = 0; i < 5; ++i) push(saved, "Step " + std::to_string(i));
            const auto version = versionOf(saved);
            const State state = Aestra::VersionEdits::measure(history, &version);
            require(!state.diverged && state.steps == 0 && state.counted, "an opened version is not unsaved work");
        }
    }

    {   // A version saved before any step: every step on the stack is unsaved.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_empty";
        push(history, "Add unit · Kick");
        const State state = Aestra::VersionEdits::measure(history, &version);
        require(state.steps == 1 && state.diverged, "steps made against an empty version are unsaved");
    }

    {   // Nothing open.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        const State state = Aestra::VersionEdits::measure(history, nullptr);
        require(state.steps == 1 && state.diverged, "with no version open, every step is unsaved");
    }

    {
        Aestra::Audio::CommandHistory empty;
        const State state = Aestra::VersionEdits::measure(empty, nullptr);
        require(state.steps == 0 && !state.diverged, "an untouched project with no version is clean");
    }

    {   // A load leaves both stacks empty, which cannot describe a version that has steps: keep its log.
        Aestra::Audio::CommandHistory loaded; // history after opening a saved version
        const std::vector<TakeManager::TakeEntry::Step> recorded{{"Add unit · Kick", "t", 1}, {"Gain", "t", 2}};
        require(!Aestra::historyCanRecordSteps(loaded, recorded),
                "an empty history does not erase a version's recorded steps");

        Aestra::Audio::CommandHistory grown;
        push(grown, "Add unit · Kick");
        push(grown, "Gain");
        push(grown, "Rename track");
        require(Aestra::historyCanRecordSteps(grown, recorded), "steps made on top of the version are recorded");

        Aestra::Audio::CommandHistory undone;
        push(undone, "Add unit · Kick");
        undone.undo();
        require(Aestra::historyCanRecordSteps(undone, recorded), "undoing every step is not a load");

        Aestra::Audio::CommandHistory empty;
        require(Aestra::historyCanRecordSteps(empty, {}), "a version with no steps records an empty log");
    }

    {   // The three answers, and the one that keeps work: nothing is discarded unless it was chosen.
        using Aestra::VersionEdits::plan;
        const auto cancel = plan(Choice::Cancel);
        require(!cancel.proceed && !cancel.saveCurrent && !cancel.saveAsNewBranch && !cancel.discard, "cancel does nothing");

        const auto save = plan(Choice::Save);
        require(save.proceed && save.saveCurrent && !save.discard, "save keeps the work in the open version");

        const auto branch = plan(Choice::SaveAsNewBranch);
        require(branch.proceed && branch.saveAsNewBranch && !branch.saveCurrent && !branch.discard,
                "save to a branch keeps the work on a new branch");

        const auto discard = plan(Choice::Discard);
        require(discard.proceed && discard.discard && !discard.saveCurrent && !discard.saveAsNewBranch,
                "discard is the only answer that leaves the work behind");

        for (const auto choice : {Choice::Cancel, Choice::Save, Choice::SaveAsNewBranch})
            require(!plan(choice).discard, "only Discard discards");
    }

    std::cout << "VersionEditsTest passed\n";
    return 0;
}
