// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ICommand.h"
#include <vector>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Groups multiple commands into a single undoable transaction (v1.0)
 * 
 * Use CommandTransaction to group related edits into a single undo step.
 * This is essential for operations that touch multiple parts of the project.
 * 
 * Example:
 *   auto transaction = std::make_shared<CommandTransaction>("Move Clips");
 *   transaction->add(clipMoveCommand1);
 *   transaction->add(clipMoveCommand2);
 *   transaction->add(clipMoveCommand3);
 *   history.pushAndExecute(transaction);  // Undoes all 3 as one step
 */
class CommandTransaction : public ICommand {
public:
    explicit CommandTransaction(const std::string& name = "Transaction");
    ~CommandTransaction() override = default;
    
    // ICommand interface
    void execute() override;
    void undo() override;
    void redo() override;
    std::string getName() const override;
    
    // Check if transaction is valid (has at least one valid command)
    bool isValid() const;
    
    // Transaction interface
    void add(std::shared_ptr<ICommand> cmd);
    size_t size() const { return m_commands.size(); }
    bool isEmpty() const { return m_commands.empty(); }
    void clear() { m_commands.clear(); }
    
    // Set display name
    void setName(const std::string& name) { m_name = name; }
    
private:
    std::vector<std::shared_ptr<ICommand>> m_commands;
    std::string m_name;
    bool m_executed = false;
};

/**
 * @brief RAII guard for batching commands into a transaction
 * 
 * Usage:
 *   {
 *       CommandTransactionGuard guard(history, "Bulk Edit");
 *       
 *       // All commands added here become part of the transaction
 *       history.pushAndExecute(cmd1);
 *       history.pushAndExecute(cmd2);
 *       history.pushAndExecute(cmd3);
 *       
 *   } // Transaction is automatically committed when guard goes out of scope
 * 
 * If an exception occurs, the transaction is rolled back.
 */
class CommandTransactionGuard {
public:
    CommandTransactionGuard(class CommandHistory& history, const std::string& name);
    ~CommandTransactionGuard();
    
    // Explicitly commit the transaction early
    void commit();
    
    // Rollback without committing
    void rollback();
    
    // Check if transaction is active
    bool isActive() const { return m_active; }
    
private:
    CommandHistory& m_history;
    std::shared_ptr<CommandTransaction> m_transaction;
    bool m_active = true;
    bool m_committed = false;
};

} // namespace Audio
} // namespace Aestra
