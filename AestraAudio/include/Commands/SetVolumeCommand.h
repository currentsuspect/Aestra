// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Core/MixerChannel.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to change a channel's volume
 */
class SetVolumeCommand : public ICommand {
public:
    SetVolumeCommand(MixerChannel& channel, float newVolume)
        : m_channel(channel), m_newVolume(newVolume) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalVolume = m_channel.getVolume();
        m_channel.setVolume(m_newVolume);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        m_channel.setVolume(m_originalVolume);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_channel.setVolume(m_newVolume);
        m_executed = true;
    }

    std::string getName() const override { return "Set Volume"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    MixerChannel& m_channel;
    float m_newVolume;
    float m_originalVolume = 0.0f;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
