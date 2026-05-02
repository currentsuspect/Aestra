// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/CommandHistory.h"

#include <iostream>

namespace Aestra {
namespace Audio {

CommandHistory::CommandHistory() {
    m_undoStack.reserve(100);
    m_redoStack.reserve(100);
}

void CommandHistory::pushAndExecute(std::shared_ptr<ICommand> cmd) {
    if (!cmd)
        return;

    // If a transaction is active, add to the transaction instead of executing
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeTransaction && m_transactionNestingLevel > 0) {
            m_activeTransaction->add(cmd);
            return;
        }
    }

    // Execute BEFORE acquiring lock to prevent deadlock if command
    // triggers callbacks that re-enter CommandHistory
    try {
        cmd->execute();
    } catch (const std::exception& e) {
        std::cerr << "Command execution failed: " << e.what() << std::endl;
        return;
    }

    bool stateChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Add to undo stack
        m_undoStack.push_back(cmd);

        // Clear redo stack on new action
        m_redoStack.clear();

        // Trim history if needed
        trimHistory();

        stateChanged = true;
    }

    // Notify listeners AFTER releasing lock to prevent deadlock
    // if callback queries CommandHistory state
    if (stateChanged) {
        for (const auto& cb : m_onStateChangedCallbacks) { if (cb) cb(); }
    }
}

void CommandHistory::beginTransaction(std::shared_ptr<CommandTransaction> transaction) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_transactionNestingLevel == 0) {
        m_activeTransaction = transaction;
    }
    m_transactionNestingLevel++;
}

void CommandHistory::commitTransaction() {
    std::shared_ptr<CommandTransaction> transactionToCommit;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_transactionNestingLevel <= 0) {
            std::cerr << "CommandHistory::commitTransaction: No active transaction" << std::endl;
            return;
        }
        m_transactionNestingLevel--;
        if (m_transactionNestingLevel == 0) {
            transactionToCommit = m_activeTransaction;
            m_activeTransaction.reset();
        }
    }

    if (transactionToCommit && transactionToCommit->isValid()) {
        // Execute the transaction (this executes all commands in it)
        try {
            transactionToCommit->execute();
        } catch (const std::exception& e) {
            std::cerr << "Transaction execution failed: " << e.what() << std::endl;
            return;
        }

        bool stateChanged = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_undoStack.push_back(transactionToCommit);
            m_redoStack.clear();
            trimHistory();
            stateChanged = true;
        }

        if (stateChanged) { for (const auto& cb : m_onStateChangedCallbacks) { if (cb) cb(); } }
    }
}

void CommandHistory::rollbackTransaction() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_transactionNestingLevel <= 0) {
        std::cerr << "CommandHistory::rollbackTransaction: No active transaction" << std::endl;
        return;
    }
    m_transactionNestingLevel--;
    if (m_transactionNestingLevel == 0) {
        m_activeTransaction.reset();
    }
}

bool CommandHistory::isTransactionActive() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_transactionNestingLevel > 0;
}

bool CommandHistory::undo() {
    std::shared_ptr<ICommand> cmd;
    
    // Phase 1: Pop from undo stack under lock
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_undoStack.empty())
            return false;
        cmd = m_undoStack.back();
        m_undoStack.pop_back();
    }

    // Phase 2: Execute undo outside lock to prevent deadlock
    // if the command's undo triggers code that re-enters CommandHistory
    bool success = false;
    try {
        cmd->undo();
        success = true;
    } catch (const std::exception& e) {
        std::cerr << "Command undo failed: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_undoStack.push_back(cmd);
    } catch (...) {
        std::cerr << "Command undo failed with unknown exception" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_undoStack.push_back(cmd);
    }

    // Phase 3: Push to redo stack under lock
    if (success) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_redoStack.push_back(cmd);
    }

    // Phase 4: Notify listeners outside lock
    if (success) {
        for (const auto& cb : m_onStateChangedCallbacks) { if (cb) cb(); }
    }
    return success;
}

bool CommandHistory::redo() {
    std::shared_ptr<ICommand> cmd;
    
    // Phase 1: Pop from redo stack under lock
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_redoStack.empty())
            return false;
        cmd = m_redoStack.back();
        m_redoStack.pop_back();
    }

    // Phase 2: Execute redo outside lock to prevent deadlock
    // if the command's redo triggers code that re-enters CommandHistory
    bool success = false;
    try {
        cmd->redo();
        success = true;
    } catch (const std::exception& e) {
        std::cerr << "Command redo failed: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_redoStack.push_back(cmd);
    } catch (...) {
        std::cerr << "Command redo failed with unknown exception" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_redoStack.push_back(cmd);
    }

    // Phase 3: Push to undo stack under lock
    if (success) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_undoStack.push_back(cmd);
    }

    // Phase 4: Notify listeners outside lock
    if (success) {
        for (const auto& cb : m_onStateChangedCallbacks) { if (cb) cb(); }
    }
    return success;
}

bool CommandHistory::canUndo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_undoStack.empty();
}

bool CommandHistory::canRedo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_redoStack.empty();
}

std::string CommandHistory::getUndoName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_undoStack.empty())
        return "";
    return m_undoStack.back()->getName();
}

std::string CommandHistory::getRedoName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_redoStack.empty())
        return "";
    return m_redoStack.back()->getName();
}

void CommandHistory::clear() {
    bool stateChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_undoStack.clear();
        m_redoStack.clear();
        stateChanged = true;
    }
    // Notify AFTER releasing lock
    if (stateChanged) { for (const auto& cb : m_onStateChangedCallbacks) { if (cb) cb(); } }
}

void CommandHistory::setMaxHistorySize(size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxHistorySize = size;
    trimHistory();
}

void CommandHistory::setMaxHistoryMemory(size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxHistoryMemory = bytes;
    trimHistoryByMemory();
}

size_t CommandHistory::getHistoryMemoryUsage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return calculateMemoryUsage();
}

void CommandHistory::trimHistory() {
    if (m_maxHistorySize == 0 && m_maxHistoryMemory == 0)
        return;

    // Trim by count first
    if (m_maxHistorySize > 0 && m_undoStack.size() > m_maxHistorySize) {
        size_t excess = m_undoStack.size() - m_maxHistorySize;
        m_undoStack.erase(m_undoStack.begin(), m_undoStack.begin() + excess);
    }

    // Then trim by memory if still over limit
    if (m_maxHistoryMemory > 0) {
        trimHistoryByMemory();
    }
}

void CommandHistory::trimHistoryByMemory() {
    if (m_maxHistoryMemory == 0)
        return;

    while (!m_undoStack.empty() && calculateMemoryUsage() > m_maxHistoryMemory) {
        // Remove oldest commands until under memory limit
        m_undoStack.erase(m_undoStack.begin());
    }
}

size_t CommandHistory::calculateMemoryUsage() const {
    size_t total = 0;
    for (const auto& cmd : m_undoStack) {
        if (cmd) {
            total += cmd->getSizeInBytes();
        }
    }
    return total;
}

void CommandHistory::undoTo(int targetIndex) {
    std::vector<std::shared_ptr<ICommand>> cmds;

    // Phase 1: Pop all commands to undo under lock
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (static_cast<int>(m_undoStack.size()) > targetIndex && !m_undoStack.empty()) {
            cmds.push_back(m_undoStack.back());
            m_undoStack.pop_back();
        }
    }

    // Phase 2: Execute all undos outside lock
    std::vector<std::shared_ptr<ICommand>> successfulUndos;
    size_t nextIndex = 0;
    try {
        for (; nextIndex < cmds.size(); ++nextIndex) {
            cmds[nextIndex]->undo();
            successfulUndos.push_back(cmds[nextIndex]);
        }
    } catch (const std::exception& e) {
        std::cerr << "Command undoTo failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Command undoTo failed with unknown exception" << std::endl;
    }

    // Phase 3: Move only successful undos to redo and restore the rest.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = cmds.size(); i > nextIndex; --i) {
            m_undoStack.push_back(cmds[i - 1]);
        }
        for (auto& cmd : successfulUndos) {
            m_redoStack.push_back(cmd);
        }
    }

    // Phase 4: Notify listeners outside lock
    if (!successfulUndos.empty()) {
        for (const auto& cb : m_onStateChangedCallbacks) { if (cb) cb(); }
    }
}

void CommandHistory::redoTo(int targetIndex) {
    std::vector<std::shared_ptr<ICommand>> cmds;

    // Phase 1: Pop all commands to redo under lock
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (static_cast<int>(m_redoStack.size()) > targetIndex && !m_redoStack.empty()) {
            cmds.push_back(m_redoStack.back());
            m_redoStack.pop_back();
        }
    }

    // Phase 2: Execute all redos outside lock
    std::vector<std::shared_ptr<ICommand>> successfulRedos;
    size_t nextIndex = 0;
    try {
        for (; nextIndex < cmds.size(); ++nextIndex) {
            cmds[nextIndex]->redo();
            successfulRedos.push_back(cmds[nextIndex]);
        }
    } catch (const std::exception& e) {
        std::cerr << "Command redoTo failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Command redoTo failed with unknown exception" << std::endl;
    }

    // Phase 3: Move only successful redos to undo and restore the rest.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = cmds.size(); i > nextIndex; --i) {
            m_redoStack.push_back(cmds[i - 1]);
        }
        for (auto& cmd : successfulRedos) {
            m_undoStack.push_back(cmd);
        }
    }

    // Phase 4: Notify listeners outside lock
    if (!successfulRedos.empty()) {
        for (const auto& cb : m_onStateChangedCallbacks) { if (cb) cb(); }
    }
}

} // namespace Audio
} // namespace Aestra
