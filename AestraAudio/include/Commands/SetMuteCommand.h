// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Core/MixerChannel.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to toggle a channel's mute state
 */
class SetMuteCommand : public ICommand {
public:
    SetMuteCommand(MixerChannel& channel, bool newMute)
        : m_channel(channel), m_newMute(newMute) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalMute = m_channel.isMuted();
        m_channel.setMute(m_newMute);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        m_channel.setMute(m_originalMute);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_channel.setMute(m_newMute);
        m_executed = true;
    }

    std::string getName() const override { return m_newMute ? "Mute" : "Unmute"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    MixerChannel& m_channel;
    bool m_newMute;
    bool m_originalMute = false;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
