// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

/** Undoable source-owned routing change for an audio pattern. */
class SetAudioPatternMixerChannelCommand final : public ICommand {
public:
    SetAudioPatternMixerChannelCommand(TrackManager& manager, PatternID patternId, uint32_t newChannelId)
        : m_manager(manager), m_patternId(patternId), m_newChannelId(newChannelId) {}

    void execute() override {
        if (m_executed)
            return;
        const auto* pattern = m_manager.getPatternManager().getPattern(m_patternId);
        if (!pattern || !pattern->isAudio())
            return;
        m_originalChannelId = pattern->getMixerChannelId();
        m_executed = m_manager.setAudioPatternMixerChannel(m_patternId, m_newChannelId);
    }

    void undo() override {
        if (!m_executed)
            return;
        if (m_manager.setAudioPatternMixerChannel(m_patternId, m_originalChannelId)) {
            m_executed = false;
        }
    }

    void redo() override {
        if (m_executed)
            return;
        m_executed = m_manager.setAudioPatternMixerChannel(m_patternId, m_newChannelId);
    }

    std::string getName() const override { return "Route Audio Source"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_manager;
    PatternID m_patternId;
    uint32_t m_originalChannelId{MASTER_MIXER_CHANNEL_ID};
    uint32_t m_newChannelId{MASTER_MIXER_CHANNEL_ID};
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
