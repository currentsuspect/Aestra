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
using Aestra::VersionEdits::measure;
using Aestra::VersionEdits::saved;
using Aestra::VersionEdits::State;
using Aestra::VersionEdits::stepsToRecord;

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

/// A version opened this session: its log is on disk and the history was cleared, so it holds no applied commands.
Baseline openedFrom(const TakeManager::TakeEntry& version) {
    Baseline baseline;
    baseline.recorded = version.steps;
    return baseline;
}

/// A version's log as the rows a save writes, then the save point that save leaves behind.
Baseline saveNow(const Aestra::Audio::CommandHistory& history, const Baseline& before) {
    return saved(history, before, stepsToRecord(history, before));
}

} // namespace

int main() {
    {   // Just saved: nothing is unsaved, and the number is exact.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        push(history, "Set swing 18%");
        const auto baseline = saveNow(history, Baseline{});
        const State state = measure(history, baseline);
        require(state.steps == 0 && state.counted && !state.diverged, "the saved state is clean");
    }

    {   // Three steps made since the save.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        const auto baseline = saveNow(history, Baseline{});
        push(history, "Move clip · Pad");
        push(history, "Set volume");
        push(history, "Rename track");
        const State state = measure(history, baseline);
        require(state.steps == 3 && state.counted && state.diverged, "three steps since the saved version");
    }

    {   // Steps taken back: the project is not the version, and no number is claimed.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 5; ++i) push(history, "Step " + std::to_string(i));
        const auto baseline = saveNow(history, Baseline{});
        history.undo();
        history.undo();
        const State state = measure(history, baseline);
        require(state.diverged, "undoing steps leaves the project different from the version");
        require(state.steps == 0 && !state.counted, "nothing new was made, so no count is shown");
    }

    {   // Undoing every step the version recorded (#1345): the undo stack is empty, the redo stack is not.
        // Reading only the undo depth called this clean, and the save that followed wrote the emptied stack
        // over the version's step log.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 3; ++i) push(history, "Step " + std::to_string(i));
        const auto baseline = saveNow(history, Baseline{});
        history.undo();
        history.undo();
        history.undo();
        require(history.getUndoStack().empty() && !history.getRedoStack().empty(),
                "every recorded step is on the redo side");
        const State state = measure(history, baseline);
        require(state.diverged, "a version whose own steps were taken back is not the version");
        require(!state.counted, "and it claims no number");
        const auto written = stepsToRecord(history, baseline);
        require(written.size() == baseline.recorded.size() && written[0].label == baseline.recorded[0].label,
                "and saving keeps the recorded steps instead of erasing them");
    }

    {   // The regression this exists for: undo past the save point, then make as many steps as were undone.
        Aestra::Audio::CommandHistory history;
        for (int i = 0; i < 5; ++i) push(history, "Step " + std::to_string(i));
        const auto baseline = saveNow(history, Baseline{});
        history.undo();
        history.undo();
        history.undo();
        push(history, "Different A");
        push(history, "Different B");
        push(history, "Different C");
        require(history.getUndoStack().size() == baseline.applied.size(),
                "the stack is exactly as deep as the save point (the old counter's blind spot)");
        const State state = measure(history, baseline);
        require(state.diverged, "three steps replaced is not the version, however deep the stack is");
        require(!state.counted, "and the replacement steps are not a count against the version");
    }

    {   // Made some, took them all back: the project is the version again, whatever the log holds.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_open";
        version.steps = {{"Add unit · Kick", "t", 1}};
        const auto baseline = openedFrom(version);
        push(history, "Add unit · Kick");
        push(history, "Set volume");
        history.undo();
        history.undo();
        const State state = measure(history, baseline);
        require(!state.diverged && state.steps == 0, "undoing every step made since the open returns to the version");
        require(stepsToRecord(history, baseline).size() == baseline.recorded.size(), "and the version's log is untouched");
    }

    {   // A step undone and redone is the same command, so the project is the version again — and a save must
        // not log it a second time.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        push(history, "Gain");
        const auto baseline = saveNow(history, Baseline{});
        history.undo();
        require(measure(history, baseline).diverged, "one step taken back is not the version");
        history.redo();
        require(!measure(history, baseline).diverged, "redoing it puts the project back on the version");
        const auto written = stepsToRecord(history, baseline);
        require(written.size() == 2 && written[1].label == "Gain", "and the redo does not add a second row");
    }

    {   // A version opened fresh: its history was cleared, so nothing has been done since.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.steps = {{"Step 0", "t", 1}, {"Step 1", "t", 2}, {"Step 2", "t", 3}};
        const State state = measure(history, openedFrom(version));
        require(!state.diverged && state.steps == 0 && state.counted, "an opened version is not unsaved work");
    }

    {   // Reopened a recorded version and made one step (#1345): the first step is an exact "1 unsaved",
        // not an unnameable re-trunk.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_reopened";
        version.steps = {{"Add unit · Kick", "t", 1}, {"Gain", "t", 2}};
        push(history, "Move clip · Pad");
        const State state = measure(history, openedFrom(version));
        require(state.steps == 1 && state.counted && state.diverged, "one step after a reopen is exactly one");
        const auto written = stepsToRecord(history, openedFrom(version));
        require(written.size() == 3 && written[0].label == "Add unit · Kick" && written[2].label == "Move clip · Pad",
                "saving after a reopen keeps the recorded steps and appends the new one");
    }

    {   // Reopened, edited, then saved: the log inherits the version's rows, but the save point is the commands
        // that were actually applied. Comparing the stack with the log's own rows read this as diverged and
        // asked to save again right after a successful save.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_inherit";
        version.steps = {{"Add unit · Kick", "t", 1}, {"Gain", "t", 2}};
        Baseline baseline = openedFrom(version);
        push(history, "Move clip · Pad");
        const auto rows = stepsToRecord(history, baseline);
        require(rows.size() == 3, "the save appends the new step after the inherited rows");
        baseline = saved(history, baseline, rows);
        const State state = measure(history, baseline);
        require(!state.diverged && state.counted && state.steps == 0, "a save after a reopen leaves the project clean");
        require(stepsToRecord(history, baseline).size() == 3, "and saving again records nothing new");
    }

    {   // A command that replaced one below the save point: the old rows stay, and the new command gets its own
        // row even though the stack is not deeper than the save point.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        push(history, "Gain");
        Baseline baseline = saveNow(history, Baseline{});
        history.undo();
        push(history, "Set volume");
        require(measure(history, baseline).diverged, "a replacement below the save point is not the version");
        const auto written = stepsToRecord(history, baseline);
        require(written.size() == 3, "the replaced step is kept and the new one is added");
        require(written[1].label == "Gain" && written[2].label == "Set volume", "in the order they were made");
        baseline = saved(history, baseline, written);
        require(!measure(history, baseline).diverged, "and the save that wrote it leaves the project clean");
        require(stepsToRecord(history, baseline).size() == 3, "with nothing left to record");
    }

    {   // A second save in the same session does not record the new steps twice.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.steps = {{"Add unit · Kick", "t", 1}};
        Baseline baseline = openedFrom(version);
        push(history, "Gain");
        require(stepsToRecord(history, baseline).size() == 2, "the first save appends the new step");
        baseline = saveNow(history, baseline);
        push(history, "Rename track");
        require(stepsToRecord(history, baseline).size() == 3, "the second save appends only the step made after it");
    }

    {   // A version saved before any step: every step on the stack is unsaved.
        Aestra::Audio::CommandHistory history;
        TakeManager::TakeEntry version;
        version.id = "take_empty";
        push(history, "Add unit · Kick");
        const State state = measure(history, openedFrom(version));
        require(state.steps == 1 && state.diverged, "steps made against an empty version are unsaved");
    }

    {   // Nothing open.
        Aestra::Audio::CommandHistory history;
        push(history, "Add unit · Kick");
        const State state = measure(history, nullptr);
        require(state.steps == 1 && state.diverged, "with no version open, every step is unsaved");
    }

    {
        Aestra::Audio::CommandHistory empty;
        const State state = measure(empty, nullptr);
        require(state.steps == 0 && !state.diverged, "an untouched project with no version is clean");
    }

    {   // A history at its limit lost the oldest edit, which is still applied: the stack cannot say how many
        // steps there really are, so no number is claimed.
        Aestra::Audio::CommandHistory history;
        history.setMaxHistorySize(100);
        for (int i = 0; i < 101; ++i) push(history, "Edit " + std::to_string(i));
        require(history.getUndoStack().size() == 100, "the limit holds the stack at 100");
        const State state = measure(history, Baseline{});
        require(state.diverged, "100 edits in an empty version is unsaved work");
        require(!state.counted, "but the evicted history cannot claim an exact count");
    }

    {   // The limits tell callers they changed something: a trim drops commands the cached answer was measured on.
        Aestra::Audio::CommandHistory history;
        history.setMaxHistorySize(3);
        for (int i = 0; i < 3; ++i) push(history, "Step " + std::to_string(i));
        const auto mutationsBefore = history.mutationCount();
        history.setMaxHistorySize(1);
        require(history.getUndoStack().size() == 1, "a smaller limit trims the stack");
        require(history.mutationCount() != mutationsBefore, "and the history says it changed");
        require(history.droppedCount() == 2, "two commands were thrown away");
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

    {   // Command identity: two commands are never the same command, however alike they look.
        const NamedCommand a("Set volume");
        const NamedCommand b("Set volume");
        require(a.identity() != b.identity(), "two commands with one label are two commands");
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
