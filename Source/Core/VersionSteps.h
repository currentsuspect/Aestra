// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"

#include "Commands/CommandHistory.h"

#include <chrono>
#include <typeinfo>
#include <vector>

namespace Aestra {

/// The label a command records into the step log: an unnamed command still gets a row.
inline std::string stepLabel(const Audio::ICommand& command) {
    const std::string name = command.getName();
    return name.empty() ? std::string("(unnamed)") : name;
}

/// The row a command leaves in a version's step log.
inline TakeManager::TakeEntry::Step stepFor(const Audio::ICommand& command) {
    return {stepLabel(command), typeid(command).name(), command.createdAtMs()};
}

/**
 * @brief The steps made in the open version, oldest first (SPEC-017): one entry per command on the undo stack.
 *
 * The log is written when a version is saved. Each step keeps the time its command was made. Pure: reads the history and touches nothing.
 */
inline std::vector<TakeManager::TakeEntry::Step> collectVersionSteps(const Audio::CommandHistory& history) {
    std::vector<TakeManager::TakeEntry::Step> steps;
    for (const auto& command : history.getUndoStack()) {
        if (!command) continue;
        steps.push_back(stepFor(*command));
    }
    return steps;
}

} // namespace Aestra
