// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to add a mixer channel to TrackManager
 */
class AddChannelCommand : public ICommand {
public:
    /**
     * @brief Create an undoable add-channel command.
     * @param manager Track manager that owns the mixer channels.
     * @param name Optional channel name. When empty, TrackManager generates a default name.
     */
    AddChannelCommand(TrackManager& manager, const std::string& name = "")
        : m_manager(manager), m_name(name) {}

    /**
     * @brief Add the channel if the command has not been executed yet.
     */
    void execute() override {
        if (m_executed) return;
        if (auto* channel = m_manager.addChannel(m_name)) {
            m_createdChannelId = channel->getChannelId();
            m_executed = true;
        }
    }

    /**
     * @brief Detach the channel created by this command, keeping it alive.
     *
     * The mirror of the delete case (#611). Destroying it here and building a
     * fresh one in redo() would give the channel a new id — orphaning anything
     * routed to the old one — and leave every command that captured a
     * `MixerChannel&` to it dangling, so add / set volume / undo / undo / redo /
     * redo wrote through freed memory.
     */
    void undo() override {
        if (!m_executed) return;
        size_t index = 0;
        if (auto detached = m_manager.detachChannelById(m_createdChannelId, index)) {
            m_detached = std::move(detached);
            m_detachedIndex = index;
            m_executed = false;
        }
    }

    /**
     * @brief Put the same channel back after an undo.
     */
    void redo() override {
        if (m_executed) return;
        if (m_detached) {
            if (m_manager.reinsertChannel(std::move(m_detached), m_detachedIndex)) {
                m_executed = true;
            }
            return;
        }
        // No detached channel means undo never ran (or failed); fall back to
        // creating one, which is what execute() would have done.
        if (auto* channel = m_manager.addChannel(m_name)) {
            m_createdChannelId = channel->getChannelId();
            m_executed = true;
        }
    }

    /**
     * @brief Get the display name used by command history.
     * @return Human-readable command name.
     */
    std::string getName() const override { return "Add Channel"; }
    /**
     * @brief Estimate the in-memory footprint of this command.
     * @return Size of the command object in bytes.
     */
    size_t getSizeInBytes() const override { return sizeof(*this); }
    /**
     * @brief Report whether this command mutates persisted project state.
     * @return Always true for channel creation/removal.
     */
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_manager;
    std::string m_name;
    bool m_executed = false;
    uint32_t m_createdChannelId = 0;
    /** The channel itself while undone, so redo restores it rather than a copy (#611). */
    std::unique_ptr<MixerChannel> m_detached;
    size_t m_detachedIndex = 0;
};

} // namespace Audio
} // namespace Aestra
