// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Commands/CommandTransaction.h"
#include "Commands/CommandHistory.h"
#include "AestraLog.h"

namespace Aestra {
namespace Audio {

//==============================================================================
// CommandTransaction
//==============================================================================

CommandTransaction::CommandTransaction(const std::string& name)
    : m_name(name)
{
}

void CommandTransaction::execute() {
    if (m_executed) {
        Log::warning("[CommandTransaction] Transaction already executed");
        return;
    }
    
    // Execute all commands in order
    for (auto& cmd : m_commands) {
        if (cmd) {
            cmd->execute();
        }
    }
    
    m_executed = true;
}

void CommandTransaction::undo() {
    if (!m_executed) {
        Log::warning("[CommandTransaction] Cannot undo - not executed");
        return;
    }
    
    // Undo in reverse order
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        if (*it) {
            (*it)->undo();
        }
    }
    
    m_executed = false;
}

void CommandTransaction::redo() {
    execute();
}

std::string CommandTransaction::getName() const {
    return m_name + " (" + std::to_string(m_commands.size()) + " ops)";
}

bool CommandTransaction::isValid() const {
    // Transaction is valid if it has at least one command
    for (const auto& cmd : m_commands) {
        if (cmd) {
            return true;
        }
    }
    return false;
}

void CommandTransaction::add(std::shared_ptr<ICommand> cmd) {
    if (cmd) {
        m_commands.push_back(cmd);
    }
}

//==============================================================================
// CommandTransactionGuard
//==============================================================================

CommandTransactionGuard::CommandTransactionGuard(CommandHistory& history, const std::string& name)
    : m_history(history)
    , m_transaction(std::make_shared<CommandTransaction>(name))
{
    // Start the transaction - commands will be collected
    Log::debug("[CommandTransactionGuard] Starting transaction: " + name);
}

CommandTransactionGuard::~CommandTransactionGuard() {
    if (m_active && !m_committed) {
        // If we get here without explicit commit/rollback, commit by default
        // unless the transaction is empty
        if (m_transaction && !m_transaction->isEmpty()) {
            commit();
        } else {
            rollback();
        }
    }
}

void CommandTransactionGuard::commit() {
    if (!m_active || m_committed) return;
    
    if (m_transaction && !m_transaction->isEmpty()) {
        Log::debug("[CommandTransactionGuard] Committing transaction with " + 
                   std::to_string(m_transaction->size()) + " commands");
        m_history.pushAndExecute(m_transaction);
    }
    
    m_committed = true;
    m_active = false;
}

void CommandTransactionGuard::rollback() {
    if (!m_active || m_committed) return;
    
    Log::debug("[CommandTransactionGuard] Rolling back transaction");
    
    // Undo any commands that were already executed
    // (In practice, commands added to a transaction shouldn't execute until commit)
    m_transaction->undo();
    m_transaction->clear();
    
    m_active = false;
}

} // namespace Audio
} // namespace Aestra
