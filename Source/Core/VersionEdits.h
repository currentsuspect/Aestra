// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"
#include "VersionSteps.h"

#include "Commands/CommandHistory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Aestra {

/**
 * @brief How far the open project has moved from the version it was opened on (SPEC-017).
 *
 * A version's step log lives in the manifest, not in the command history: opening a version clears the
 * history, so the stack holds only what was done since, and a save in the same session leaves the whole
 * log on the stack. `Baseline` says which of the two is true by naming the commands that were applied when
 * the version was saved, so one rule answers both.
 *
 * Commands are compared by identity, not by depth, label or time: a redo puts the very same command back
 * (the project is the version again), while a new command at the same depth is a different project. A
 * depth-to-depth subtraction reads both as nothing. Pure: reads the history and touches nothing.
 */
namespace VersionEdits {

struct Baseline {
    /// @brief The log the open version has saved, oldest first — the rows the manifest holds.
    std::vector<TakeManager::TakeEntry::Step> recorded;
    /**
     * @brief The commands applied when the version was last saved: the state it holds, oldest first.
     *
     * Empty when the version was opened rather than saved in this session (the history was cleared, so
     * nothing on the stack is the version's).
     */
    std::vector<uint64_t> applied;
    /**
     * @brief The identities that already have a row in `recorded` (a superset of `applied`).
     *
     * A command taken back is still logged, so a redo of it must not append a second row.
     */
    std::vector<uint64_t> chronicled;
    /// @brief The history's eviction count when the version was saved; more evictions since means rows were lost.
    uint64_t dropped{0};
};

struct State {
    /** @brief Steps made since the version was saved. Exact only when `counted` is true. */
    int steps{0};
    /** @brief True when the project is not the version's state, so switching would change or lose work. */
    bool diverged{false};
    /** @brief True when `steps` is an exact count, so it can be shown as a number. */
    bool counted{false};
};

/**
 * @brief How many of the stack's leading commands are the version's own, oldest first.
 *
 * Everything applied when the version was saved sits at the bottom of the stack; anything on top of it is
 * what has been done since. Where the two part company, the project has moved off the version's state.
 */
inline size_t stillInPlace(const Audio::CommandHistory& history, const Baseline& baseline) {
    const auto& undo = history.getUndoStack();
    const size_t limit = std::min(undo.size(), baseline.applied.size());
    size_t i = 0;
    for (; i < limit; ++i) {
        if (!undo[i] || undo[i]->identity() != baseline.applied[i]) break;
    }
    return i;
}

/// What the title bar and the switch prompt need: the step count, and whether the project is still the version.
inline State measure(const Audio::CommandHistory& history, const Baseline& baseline) {
    State state;
    const auto& undo = history.getUndoStack();
    const size_t inPlace = stillInPlace(history, baseline);
    if (inPlace == baseline.applied.size() && undo.size() == baseline.applied.size()) {
        state.counted = true; // the project is the version, and that is an exact zero
        return state;
    }
    state.diverged = true;
    if (inPlace == baseline.applied.size()) {
        // Work sits on top of the version's own steps, so the count is exact — unless a history limit threw
        // rows away, where the stack no longer describes everything that was done.
        state.steps = static_cast<int>(undo.size() - baseline.applied.size());
        state.counted = history.droppedCount() == baseline.dropped;
    }
    return state;
}

/// The same, for a version opened this session (its log is on disk, the history was cleared).
inline State measure(const Audio::CommandHistory& history, const TakeManager::TakeEntry* version) {
    Baseline baseline;
    if (version) baseline.recorded = version->steps;
    return measure(history, baseline);
}

/**
 * @brief The step log a save should write: the version's own rows, then a row for every command it lacks.
 *
 * Never the raw stack: after reopening a version the stack holds only the new commands, and writing it
 * alone would erase the version's recorded steps. Rows are picked by identity, so a command taken back and
 * redone does not get a second row, and a command that replaced one below the save point still gets its own.
 */
inline std::vector<TakeManager::TakeEntry::Step> stepsToRecord(const Audio::CommandHistory& history,
                                                              const Baseline& baseline) {
    std::vector<TakeManager::TakeEntry::Step> steps = baseline.recorded;
    std::unordered_set<uint64_t> chronicled(baseline.chronicled.begin(), baseline.chronicled.end());
    for (const auto& command : history.getUndoStack()) {
        if (!command) continue;
        if (!chronicled.insert(command->identity()).second) continue;
        steps.push_back(stepFor(*command));
    }
    return steps;
}

/// @brief The save point after `rows` were written: everything applied is part of the version now.
inline Baseline saved(const Audio::CommandHistory& history, const Baseline& before,
                      std::vector<TakeManager::TakeEntry::Step> rows) {
    Baseline after;
    after.recorded = std::move(rows);
    after.chronicled = before.chronicled; // the rows already written keep their identities
    std::unordered_set<uint64_t> chronicled(before.chronicled.begin(), before.chronicled.end());
    for (const auto& command : history.getUndoStack()) {
        if (!command) continue;
        after.applied.push_back(command->identity());
        if (chronicled.insert(command->identity()).second) after.chronicled.push_back(command->identity());
    }
    after.dropped = history.droppedCount();
    return after;
}

/**
 * @brief What asking for a version came to: opened, the user declined the prompt, or the switch failed.
 *
 * Declining is not a failure: the Versions dock reports "Could not open the version" for a failure only.
 */
enum class OpenOutcome { Opened, Declined, Failed };

/**
 * @brief The three answers a version switch can take (SPEC-017 scope 5), and what each one does.
 *
 * Cancel is the default answer and the one every unhandled input lands on, so no path discards work
 * without the user asking for it.
 */
enum class Choice { Cancel, Save, SaveAsNewBranch, Discard };

struct Plan {
    bool proceed{false};         ///< go ahead with the switch
    bool saveCurrent{false};     ///< write the working state into the open version first
    bool saveAsNewBranch{false}; ///< save it as a new version on a new branch first
    bool discard{false};         ///< leave the working state behind
};

inline Plan plan(Choice choice) {
    switch (choice) {
    case Choice::Save:
        return {true, true, false, false};
    case Choice::SaveAsNewBranch:
        return {true, false, true, false};
    case Choice::Discard:
        return {true, false, false, true};
    case Choice::Cancel:
    default:
        return {};
    }
}

} // namespace VersionEdits
} // namespace Aestra
