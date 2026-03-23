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
 *
 * Thread-safe implementation with configurable limits.
 * Supports both count-based and memory-based history limits.
 */
class CommandHistory {
public:
    CommandHistory();
    ~CommandHistory() = default;

    // Disable copy/move
    CommandHistory(const CommandHistory&) = delete;
    CommandHistory& operator=(const CommandHistory&) = delete;
    CommandHistory(CommandHistory&&) = delete;
    CommandHistory& operator=(CommandHistory&&) = delete;

    /**
     * @brief Execute a command and push it to the undo stack
     * Clears the redo stack.
     * @param cmd Command to execute and track
     */
    void pushAndExecute(std::shared_ptr<ICommand> cmd);

    /**
     * @brief Perform undo
     * @return true if successful, false if nothing to undo
     */
    bool undo();

    /**
     * @brief Perform redo
     * @return true if successful, false if nothing to redo
     */
    bool redo();

    bool canUndo() const;
    bool canRedo() const;

    std::string getUndoName() const;
    std::string getRedoName() const;

    void clear();

    /**
     * @brief Set maximum number of commands in history
     * @param size Maximum number of commands (0 = unlimited)
     */
    void setMaxHistorySize(size_t size);

    /**
     * @brief Set maximum memory usage for history
     * @param bytes Maximum bytes (0 = unlimited, default = 100MB)
     */
    void setMaxHistoryMemory(size_t bytes);

    /**
     * @brief Get current memory usage of history
     * @return Approximate memory usage in bytes
     */
    size_t getHistoryMemoryUsage() const;

    // Callbacks for UI updates
    using StateChangedCallback = std::function<void()>;
    void setOnStateChanged(StateChangedCallback cb) { m_onStateChanged = cb; }

private:
    std::vector<std::shared_ptr<ICommand>> m_undoStack;
    std::vector<std::shared_ptr<ICommand>> m_redoStack;

    size_t m_maxHistorySize = 100;
    size_t m_maxHistoryMemory = 100 * 1024 * 1024; // 100MB default

    mutable std::mutex m_mutex;
    StateChangedCallback m_onStateChanged;

    void trimHistory();
    void trimHistoryByMemory();
    size_t calculateMemoryUsage() const;
};

} // namespace Audio
} // namespace Aestra
