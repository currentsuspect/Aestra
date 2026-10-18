// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace Aestra {
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
     * @brief Redo the command (must be explicitly implemented)
     * Implementers should consider: is redo() exactly execute(), or different?
     */
    virtual void redo() = 0;

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

    /**
     * @brief Whether the most recent execute/redo produced an undoable change.
     */
    virtual bool isUndoable() const { return true; }

    /**
     * @brief Serialize command to JSON string for persistence
     * @return JSON representation of command data
     */
    virtual std::string serialize() const { return {}; }

    /**
     * @brief Get command type identifier for factory reconstruction
     * @return String identifier like "add_clip", "move_pattern", etc.
     */
    virtual std::string type() const { return {}; }

    /// When the command was made (epoch ms): the moment of the user's action, which the Versions steps list shows.
    uint64_t createdAtMs() const { return m_createdAtMs; }

private:
    uint64_t m_createdAtMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
};

} // namespace Audio
} // namespace Aestra
