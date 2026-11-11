// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"

#include "Commands/CommandHistory.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Aestra {

/**
 * @brief How far the open project has moved from the version it was opened on (SPEC-017).
 *
 * "How many commands are on the stack now, minus how many the version recorded" is only true while the
 * recorded steps are still the leading commands: undo past the save point and make a new step and the
 * subtraction reads zero while the project differs from the version. This walks the recorded steps
 * against the undo stack instead, so the answer stays true after undo. Pure: reads, changes nothing.
 *
 * Two states are indistinguishable from the history alone — a version just opened, and a version whose
 * every step was taken back. Both leave an empty stack, and both are treated as "the version": prompting
 * on every switch after opening a version would be worse than the case it would catch.
 */
namespace VersionEdits {

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

/// How many of the version's recorded steps are still the leading commands on the undo stack, oldest first.
inline size_t stepsStillInPlace(const Audio::CommandHistory& history, const std::vector<TakeManager::TakeEntry::Step>& recordedSteps) {
    const auto& undo = history.getUndoStack();
    const size_t limit = std::min(undo.size(), recordedSteps.size());
    size_t i = 0;
    for (; i < limit; ++i) {
        const auto& command = undo[i];
        if (!command) break;
        const auto& step = recordedSteps[i];
        if (command->createdAtMs() != step.atMs || stepLabel(*command) != step.label) break;
    }
    return i;
}

inline size_t stepsStillInPlace(const Audio::CommandHistory& history, const TakeManager::TakeEntry& version) {
    return stepsStillInPlace(history, version.steps);
}

/// What the title bar and the switch prompt need: the step count, and whether the project is still the version.
inline State measureSteps(const Audio::CommandHistory& history, const std::vector<TakeManager::TakeEntry::Step>& recordedSteps) {
    State state;
    const int depth = static_cast<int>(history.getUndoStack().size());
    if (depth == 0) { // opened, or every step taken back: nothing has been done since
        state.counted = true;
        return state;
    }
    if (recordedSteps.empty()) { // a version saved before any step: every step on the stack is new
        state.steps = depth;
        state.counted = true;
        state.diverged = true;
        return state;
    }

    const int recorded = static_cast<int>(recordedSteps.size());
    const int inPlace = static_cast<int>(stepsStillInPlace(history, recordedSteps));
    if (inPlace == depth) { // every live command is one of the version's own steps
        state.counted = depth == recorded;
        state.diverged = !state.counted;
        return state;
    }
    if (inPlace == recorded) { // the version's steps are all still in place, plus new ones
        state.steps = depth - recorded;
        state.counted = true;
        state.diverged = true;
        return state;
    }
    // The history was re-trunked: steps were taken back past the save point, then new ones made.
    state.steps = std::max(0, depth - inPlace);
    state.diverged = true;
    return state;
}

inline State measure(const Audio::CommandHistory& history, const std::vector<TakeManager::TakeEntry::Step>& recordedSteps) {
    return measureSteps(history, recordedSteps);
}

inline State measure(const Audio::CommandHistory& history, const TakeManager::TakeEntry* version) {
    return measureSteps(history, version ? version->steps : std::vector<TakeManager::TakeEntry::Step>{});
}

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
