// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/Footprint.h"

#include <atomic>
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

    /**
     * @brief What a value-setting command left behind, as the object displays it.
     *
     * set_effect_param answers "3.5kHz" here, so a caller that asked for a value
     * in a unit can confirm what landed without a follow-up query. Empty for
     * commands that set nothing (the default).
     */
    virtual std::string displayValue() const { return {}; }

    /// What this step touched, for pickable undo (SPEC-026). Commands that do not say count as touching
    /// everything, so they are never undone around. New commands declare it (AGENTS.md "Compounding Rules").
    virtual Footprint footprint() const { return Footprint::everything(); }

    /// When the command was made (epoch ms): the moment of the user's action, which the Versions steps list shows.
    uint64_t createdAtMs() const { return m_createdAtMs; }

    /**
     * @brief This command as an object, for the life of the process.
     *
     * A save point is a set of commands, and a label plus a millisecond is not one: two edits of the same kind in
     * the same millisecond are indistinguishable there. Identities are never reused, so comparing them says
     * whether the stack still holds the commands a version was saved with — including after a redo, which puts
     * the very same command back.
     */
    uint64_t identity() const { return m_identity; }

private:
    static uint64_t nextIdentity() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t m_createdAtMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    uint64_t m_identity = nextIdentity();
};

} // namespace Audio
} // namespace Aestra
