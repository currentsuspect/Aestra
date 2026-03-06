// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ICommand.h"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Manages undo/redo stacks for commands
 */
class CommandHistory {
public:
    CommandHistory();
    ~CommandHistory() = default;

    /**
     * @brief Execute a command and push it to the undo stack
     * Clears the redo stack.
     */
    void pushAndExecute(std::shared_ptr<ICommand> cmd);

    /**
     * @brief Perform undo
     * @return true if successful
     */
    bool undo();

    /**
     * @brief Perform redo
     * @return true if successful
     */
    bool redo();

    bool canUndo() const;
    bool canRedo() const;

    std::string getUndoName() const;
    std::string getRedoName() const;

    void clear();

    void setMaxHistorySize(size_t size);

    // Callbacks for UI updates
    using StateChangedCallback = std::function<void()>;
    void setOnStateChanged(StateChangedCallback cb) { m_onStateChanged = cb; }

private:
    std::vector<std::shared_ptr<ICommand>> m_undoStack;
    std::vector<std::shared_ptr<ICommand>> m_redoStack;

    size_t m_maxHistorySize = 100;

    mutable std::mutex m_mutex;
    StateChangedCallback m_onStateChanged;

    void trimHistory();
};

} // namespace Audio
} // namespace Aestra
