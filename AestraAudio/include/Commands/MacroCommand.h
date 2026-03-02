// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ICommand.h"
#include <vector>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Groups multiple commands into a single undoable transaction
 * 
 * Usage:
 *   auto macro = std::make_shared<MacroCommand>();
 *   macro->setName("Move Clips");
 *   macro->addCommand(cmd1);
 *   macro->addCommand(cmd2);
 *   commandHistory.pushAndExecute(macro);
 */
class MacroCommand : public ICommand {
public:
    MacroCommand() = default;
    explicit MacroCommand(const std::string& name) : m_name(name) {}
    
    /**
     * @brief Add a command to the macro (executed in order, undone in reverse)
     */
    void addCommand(std::shared_ptr<ICommand> cmd) {
        if (cmd) {
            m_commands.push_back(cmd);
        }
    }
    
    /**
     * @brief Set a human-readable name for the macro
     */
    void setName(const std::string& name) {
        m_name = name;
    }
    
    void execute() override {
        for (auto& cmd : m_commands) {
            cmd->execute();
        }
    }
    
    void undo() override {
        // Reverse order for undo
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
            (*it)->undo();
        }
    }
    
    void redo() override {
        // Forward order for redo
        for (auto& cmd : m_commands) {
            cmd->redo();
        }
    }
    
    std::string getName() const override {
        return m_name.empty() ? "Macro" : m_name;
    }
    
    size_t getSizeInBytes() const override {
        size_t total = sizeof(*this);
        for (const auto& cmd : m_commands) {
            total += cmd->getSizeInBytes();
        }
        return total;
    }
    
    bool changesProjectState() const override {
        for (const auto& cmd : m_commands) {
            if (cmd->changesProjectState()) {
                return true;
            }
        }
        return false;
    }
    
    size_t getCommandCount() const {
        return m_commands.size();
    }
    
    bool empty() const {
        return m_commands.empty();
    }

private:
    std::vector<std::shared_ptr<ICommand>> m_commands;
    std::string m_name;
};

} // namespace Audio
} // namespace Aestra
