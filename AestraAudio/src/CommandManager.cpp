// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "CommandManager.h"

namespace Aestra {
namespace Audio {

CommandManager::CommandManager() {
}

bool CommandManager::undo() {
    return m_history.undo();
}

bool CommandManager::redo() {
    return m_history.redo();
}

} // namespace Audio
} // namespace Aestra