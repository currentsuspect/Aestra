// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

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
     * @brief Remove the specific channel created by this command.
     */
    void undo() override {
        if (!m_executed) return;
        if (m_manager.removeChannelById(m_createdChannelId)) {
            m_executed = false;
        }
    }

    /**
     * @brief Recreate the channel after an undo operation.
     */
    void redo() override {
        if (m_executed) return;
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
};

} // namespace Audio
} // namespace Aestra
