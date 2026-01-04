// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>

namespace Nomad {
namespace Audio {

/**
 * @brief Abstract base class for all undoable operations
 */
class ICommand {
public:
    virtual ~ICommand() = default;

    /**
     * @brief Execute the command (first time)
     */
    virtual void execute() = 0;

    /**
     * @brief Undo the command
     */
    virtual void undo() = 0;

    /**
     * @brief Redo the command (default implementation calls execute)
     */
    virtual void redo() {
        execute();
    }

    /**
     * @brief Get a human-readable name for the command (for UI)
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Memory size approximation for this command (for history limits)
     */
    virtual size_t getSizeInBytes() const { return sizeof(*this); }
    
    /**
     * @brief Whether this command modifies the project state (dirty flag)
     */
    virtual bool changesProjectState() const { return true; }
};

} // namespace Audio
} // namespace Nomad
