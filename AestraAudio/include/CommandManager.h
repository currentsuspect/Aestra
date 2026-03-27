// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Commands/CommandHistory.h"
#include "Commands/CommandTransaction.h"
#include "Commands/AddClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/SplitClipCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Commands/CreateLaneCommand.h"
#include "Commands/SetVolumeCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetSoloCommand.h"

namespace Aestra {
namespace Audio {

/**
 * @brief Canonical Command Model for Aestra
 * 
 * This is the foundation for all undo/redo operations in the DAW.
 * Every edit operation must be wrapped in a reversible command object.
 * 
 * Architecture:
 * 
 *   ICommand (interface)
 *     ├── execute()   - Perform the operation
 *     ├── undo()      - Reverse the operation
 *     ├── redo()      - Re-perform the operation
 *     ├── serialize() - Save to project file
 *     └── type()      - Command type identifier
 * 
 *   CommandHistory (manager)
 *     - Maintains undo/redo stacks
 *     - Thread-safe implementation
 *     - Configurable memory limits
 *     - State change callbacks for UI
 * 
 *   CommandTransaction (composite)
 *     - Groups multiple commands into one undo step
 *     - Supports nested transactions
 * 
 * Usage:
 * 
 *   // Simple command
 *   auto cmd = std::make_shared<MoveClipCommand>(playlist, clipId, newBeat);
 *   history.pushAndExecute(cmd);
 * 
 *   // Grouped commands as one undo step
 *   {
 *       CommandTransactionGuard guard(history, "Move Clip Group");
 *       history.pushAndExecute(cmd1);
 *       history.pushAndExecute(cmd2);
 *   }
 * 
 * Keyboard shortcuts:
 *   Ctrl+Z - Undo
 *   Ctrl+Y - Redo
 *   Ctrl+Shift+Z - Redo (alternative)
 */
class CommandManager {
public:
    CommandManager();
    ~CommandManager() = default;

    CommandHistory& getHistory() { return m_history; }
    const CommandHistory& getHistory() const { return m_history; }

    bool undo();
    bool redo();
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }

    std::string getUndoName() const { return m_history.getUndoName(); }
    std::string getRedoName() const { return m_history.getRedoName(); }

    void setOnStateChanged(CommandHistory::StateChangedCallback cb) {
        m_history.setOnStateChanged(std::move(cb));
    }

private:
    CommandHistory m_history;
};

} // namespace Audio
} // namespace Aestra