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

using Aestra::VersionEdits::Baseline;
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

/// A version holding the history's current steps, the way a save in this session records them.
TakeManager::TakeEntry versionOf(const Aestra::Audio::CommandHistory& history) {
    TakeManager::TakeEntry version;
    version.id = "take_test";
    version.steps = Aestra::collectVersionSteps(history);
    return version;
}

/// The save point: the version's log, and the stack depth it covers.
Baseline savedAt(const Aestra::Audio::CommandHistory& history) {
    Baseline baseline;
    baseline.recorded = Aestra::collectVersionSteps(history);
    baseline.chronicled = history.getUndoStack().size();
    return baseline;
}

/// Opening (or switching to) a version: the log is on disk and the history was cleared.
Baseline openedFrom(const TakeManager::TakeEntry& version) {
    return Baseline{version.steps, 0};
}

} // namespace

int main() {
    {   // Just saved: nothing is unsaved, and the number is exact.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        push(history, "Set swing 18%");
        const auto baseline = savedAt(history);
        const State state = Aestra::VersionEdits::measure(history, baseline);
        require(state.steps == 0 && state.counted && !state.diverged, "the saved state is clean");
    }

    {   // Three steps made since the save.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        const auto baseline = savedAt(history);
        push(history, "Move clip · Pad");
        push(history, "Set volume");
        push(history, "Rename track");
        const State state = Aestra::VersionEdits::measure(history, baseline);
        require(state.steps == 3 && state.counted && state.diverged, "three steps since the saved version");
    }

    {   // Steps taken back: the project is not the version, and no number is claimed.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 5; ++i) push(history, "Step " + std::to_string(i));
        const auto baseline = savedAt(history);
        history.undo();
        history.undo();
        const State state = Aestra::VersionEdits::measure(history, baseline);
        require(state.diverged, "undoing steps leaves the project different from the version");
        require(state.steps == 0 && !state.counted, "nothing new was made, so no count is shown");
    }

    {   // Undoing every step the version recorded (#1345): the undo stack is empty, the redo stack is not.
        // Reading only the undo depth called this clean, and the save that followed wrote the emptied stack
        // over the version's step log.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 3; ++i) push(history, "Step " + std::to_string(i));
        const auto baseline = savedAt(history);
        history.undo();
        history.undo();
        history.undo();
        require(history.getUndoStack().empty() && !history.getRedoStack().empty(),
                "every recorded step is on the redo side");
        const State state = Aestra::VersionEdits::measure(history, baseline);
        require(state.diverged, "a version whose own steps were taken back is not the version");
        require(!state.counted, "and it claims no number");
        const auto written = Aestra::VersionEdits::stepsToRecord(history, baseline);
        require(written.size() == baseline.recorded.size() && written[0].label == baseline.recorded[0].label,
                "and saving keeps the recorded steps instead of erasing them");
    }

    {   // The regression this exists for: undo past the save point, then make as many steps as were undone.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 5; ++i) push(history, "Step " + std::to_string(i));
        const auto baseline = savedAt(history);
        history.undo();
        history.undo();
        push(history, "Different A");
        push(history, "Different B");
        require(static_cast<int>(history.getUndoStack().size()) == static_cast<int>(baseline.recorded.size()),
                "the stack is as deep as the version recorded (the old counter's blind spot)");
        const State state = Aestra::VersionEdits::measure(history, baseline);
        require(state.diverged, "a re-trunked history is still different from the version");
        require(state.steps == 2 && !state.counted, "the two new steps are not a count against the version");
    }

    {   // Made some, took them all back: the project is the version again, whatever the log holds.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_open";
        version.steps = {{"Add unit · Kick", "t", 1}};
        const Baseline baseline = openedFrom(version);
        push(history, "Add unit · Kick");
        push(history, "Set volume");
        history.undo();
        history.undo();
        const State state = Aestra::VersionEdits::measure(history, baseline);
        require(!state.diverged && state.steps == 0, "undoing every step made since the open returns to the version");
        require(Aestra::VersionEdits::stepsToRecord(history, baseline).size() == baseline.recorded.size(),
                "and the version's log is untouched");
    }

    {   // A version opened fresh: its history was cleared, so nothing has been done since.
        Aestra::Audio::CommandHistory history;
        {
            Aestra::Audio::CommandHistory saved;
            for (int i = 0; i < 5; ++i) push(saved, "Step " + std::to_string(i));
            const auto version = versionOf(saved);
            const State state = Aestra::VersionEdits::measure(history, openedFrom(version));
            require(!state.diverged && state.steps == 0 && state.counted, "an opened version is not unsaved work");
        }
    }

    {   // Reopened a recorded version and made one step (#1345): the first step is an exact "1 unsaved",
        // not an unnameable re-trunk.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_reopened";
        version.steps = {{"Add unit · Kick", "t", 1}, {"Gain", "t", 2}};
        push(history, "Move clip · Pad");
        const State state = Aestra::VersionEdits::measure(history, openedFrom(version));
        require(state.steps == 1 && state.counted && state.diverged, "one step after a reopen is exactly one");
        const auto written = Aestra::VersionEdits::stepsToRecord(history, openedFrom(version));
        require(written.size() == 3 && written[0].label == "Add unit · Kick" && written[2].label == "Move clip · Pad",
                "saving after a reopen keeps the recorded steps and appends the new one");
    }

    {   // A second save in the same session does not record the new steps twice.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.steps = {{"Add unit · Kick", "t", 1}};
        Baseline baseline = openedFrom(version);
        push(history, "Gain");
        auto written = Aestra::VersionEdits::stepsToRecord(history, baseline);
        require(written.size() == 2, "the first save appends the new step");
        baseline.recorded = written;
        baseline.chronicled = history.getUndoStack().size();
        push(history, "Rename track");
        written = Aestra::VersionEdits::stepsToRecord(history, baseline);
        require(written.size() == 3, "the second save appends only the step made after it");
    }

    {   // A version saved before any step: every step on the stack is unsaved.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_empty";
        push(history, "Add unit · Kick");
        const State state = Aestra::VersionEdits::measure(history, openedFrom(version));
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

    {   // The history's mutation count moves even when the depths do not (#1345): at the limit a new command
        // replaces the oldest, so a cached measure keyed on the two depths would never refresh.
        Aestra::Audio::CommandHistory history;
        history.setMaxHistorySize(3);
        for (int i = 0; i < 3; ++i) push(history, "Step " + std::to_string(i));
        const auto depthBefore = history.getUndoStack().size();
        const auto mutationsBefore = history.mutationCount();
        push(history, "Step 3");
        require(history.getUndoStack().size() == depthBefore, "the stack stays at its limit");
        require(history.mutationCount() != mutationsBefore, "but the history says it changed");
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
