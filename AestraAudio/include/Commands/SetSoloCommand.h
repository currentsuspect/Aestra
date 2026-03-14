// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Core/MixerChannel.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to toggle a channel's solo state
 */
class SetSoloCommand : public ICommand {
public:
    SetSoloCommand(MixerChannel& channel, bool newSolo)
        : m_channel(channel), m_newSolo(newSolo) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalSolo = m_channel.isSoloed();
        m_channel.setSolo(m_newSolo);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        m_channel.setSolo(m_originalSolo);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_channel.setSolo(m_newSolo);
        m_executed = true;
    }

    std::string getName() const override { return m_newSolo ? "Solo" : "Unsolo"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    MixerChannel& m_channel;
    bool m_newSolo;
    bool m_originalSolo = false;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
