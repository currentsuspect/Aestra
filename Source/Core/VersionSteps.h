// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"

#include "Commands/CommandHistory.h"

#include <chrono>
#include <typeinfo>
#include <vector>

namespace Aestra {

/**
 * @brief The steps made in the open version, oldest first (SPEC-017): one entry per command on the undo stack.
 *
 * The log is written when a version is saved. Commands carry no timestamps, so every step takes the save time;
 * the order is what the dock shows. Pure: reads the history and touches nothing.
 */
inline std::vector<TakeManager::TakeEntry::Step> collectVersionSteps(const Audio::CommandHistory& history) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<TakeManager::TakeEntry::Step> steps;
    for (const auto& command : history.getUndoStack()) {
        if (!command) continue;
        std::string label = command->getName();
        steps.push_back({label.empty() ? "(unnamed)" : label, typeid(*command).name(), static_cast<uint64_t>(now)});
    }
    return steps;
}

} // namespace Aestra
