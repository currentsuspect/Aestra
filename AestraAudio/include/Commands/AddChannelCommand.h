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
    AddChannelCommand(TrackManager& manager, const std::string& name = "")
        : m_manager(manager), m_name(name) {}

    void execute() override {
        if (m_executed) return;
        m_manager.addChannel(m_name);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_manager.removeLastChannel();
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        m_manager.addChannel(m_name);
        m_executed = true;
    }

    std::string getName() const override { return "Add Channel"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_manager;
    std::string m_name;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
