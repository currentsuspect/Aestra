// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"
#include "VersionSteps.h"

#include "Commands/CommandHistory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief How far the open project has moved from the version it was opened on (SPEC-017).
 *
 * A version's step log lives in the manifest, not in the command history: opening a version clears the
 * history, so the stack holds only what was done since, and a save in the same session leaves the whole
 * log on the stack. `Baseline` says which of the two is true by counting how many of the stack's leading
 * commands are already in the log, so one rule answers both.
 *
 * "How many commands are on the stack now, minus how many the version recorded" is only true while the
 * recorded steps are still the leading commands: undo past the save point and make a new step and the
 * subtraction reads zero while the project differs from the version. Measuring from the save point's
 * depth instead keeps the answer true after undo, after reopening, and when the history is truncated at
 * its limit. Pure: reads, changes nothing.
 */
namespace VersionEdits {

struct Baseline {
    /// @brief The log the open version saved, oldest first.
    std::vector<TakeManager::TakeEntry::Step> recorded;
    /**
     * @brief How many of the undo stack's leading commands are already in `recorded`.
     *
     * Zero when the version was opened (its log is on disk, the history was cleared); the stack depth at
     * the save when the version was saved in this session. Undoing below it means the version's own steps
     * were taken back.
     */
    size_t chronicled{0};
};

struct State {
    /** @brief Steps made since the version was saved. Exact only when `counted` is true. */
    int steps{0};
    /** @brief True when the project is not the version's state, so switching would change or lose work. */
    bool diverged{false};
    /** @brief True when `steps` is an exact count, so it can be shown as a number. */
    bool counted{false};
};

/// The label a command records into the step log (see VersionSteps.h: an unnamed command still gets a row).
inline std::string stepLabel(const Audio::ICommand& command) {
    const std::string name = command.getName();
    return name.empty() ? std::string("(unnamed)") : name;
}

/// How many of the stack's leading commands are still the rows the version recorded.
inline size_t stepsStillInPlace(const Audio::CommandHistory& history, const Baseline& baseline) {
    const auto& undo = history.getUndoStack();
    const size_t limit = std::min({undo.size(), baseline.recorded.size(), baseline.chronicled});
    size_t i = 0;
    for (; i < limit; ++i) {
        const auto& command = undo[i];
        if (!command) break;
        const auto& step = baseline.recorded[i];
        if (command->createdAtMs() != step.atMs || stepLabel(*command) != step.label) break;
    }
    return i;
}

/// What the title bar and the switch prompt need: the step count, and whether the project is still the version.
inline State measure(const Audio::CommandHistory& history, const Baseline& baseline) {
    State state;
    const int depth = static_cast<int>(history.getUndoStack().size());
    const int saved = static_cast<int>(baseline.chronicled);
    const int inPlace = static_cast<int>(stepsStillInPlace(history, baseline));
    // The save point's own commands are the version's: once they stop, a new step at the same depth is a
    // re-trunked history, which a depth-to-depth subtraction reads as untouched.
    const int expected = static_cast<int>(std::min(baseline.recorded.size(), baseline.chronicled));
    if (depth < saved || inPlace < expected) {
        state.steps = std::max(0, depth - inPlace);
        state.diverged = true;
        return state;
    }
    state.steps = depth - saved;
    // A step taken back leaves the count describing work that is no longer applied, so it is not claimed.
    state.counted = history.getRedoStack().empty();
    state.diverged = state.steps > 0;
    return state;
}

/// The same, for a version entry (or none: every command on the stack is then unsaved work).
inline State measure(const Audio::CommandHistory& history, const TakeManager::TakeEntry* version, size_t chronicled = 0) {
    Baseline baseline;
    if (version) baseline.recorded = version->steps;
    baseline.chronicled = chronicled;
    return measure(history, baseline);
}

/**
 * @brief The step log a save should write: the version's own rows, then the commands made since the save point.
 *
 * Never the raw stack: after reopening a version the stack holds only the new commands, and writing it
 * alone would erase the version's recorded steps. A re-trunked history (steps taken back past the save
 * point) keeps the stored log rather than trading it for a shorter one.
 */
inline std::vector<TakeManager::TakeEntry::Step> stepsToRecord(const Audio::CommandHistory& history,
                                                              const Baseline& baseline) {
    std::vector<TakeManager::TakeEntry::Step> steps = baseline.recorded;
    const auto& undo = history.getUndoStack();
    for (size_t i = std::min(baseline.chronicled, undo.size()); i < undo.size(); ++i) {
        if (!undo[i]) continue;
        steps.push_back(stepFor(*undo[i]));
    }
    return steps;
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
