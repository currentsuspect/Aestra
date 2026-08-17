// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to toggle a channel's input-monitoring arm state
 *
 * Mirrors SetMuteCommand: the mixer slot is the input-monitoring trigger
 * (FD-14 #6), and routing the toggle through the command history gives the
 * project dirty flag and undo/redo parity with mute/solo/pan. The engine
 * monitor-route snapshot refreshes through setArmed's own notification.
 */
class SetMonitoringCommand : public ICommand {
public:
    SetMonitoringCommand(TrackManager& trackManager, MixerChannel& channel, bool newArmed)
        : m_trackManager(trackManager), m_channel(channel), m_newArmed(newArmed) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalArmed = m_channel.isArmed();
        m_channel.setArmed(m_newArmed);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        m_channel.setArmed(m_originalArmed);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_channel.setArmed(m_newArmed);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
    }

    std::string getName() const override { return m_newArmed ? "Enable Input Monitoring" : "Disable Input Monitoring"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    bool m_newArmed;
    bool m_originalArmed = false;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra