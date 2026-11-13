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
 * @brief Command to switch a channel's input monitoring (the mixer strip's IN key)
 *
 * Mirrors SetMuteCommand: the mixer slot is the input-monitoring switch
 * (FD-14 #6), and routing the toggle through the command history gives the
 * project dirty flag and undo/redo parity with mute/solo/pan. It changes
 * monitoring only, never record arm (it used to set the arm flag, so IN armed
 * the channel and the input still was not heard). The engine monitor-route
 * snapshot refreshes through setMonitoringEnabled's own notification.
 */
class SetMonitoringCommand : public ICommand {
public:
    SetMonitoringCommand(TrackManager& trackManager, MixerChannel& channel, bool monitored)
        : m_trackManager(trackManager), m_channel(channel), m_newMonitored(monitored) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalMonitored = m_channel.isMonitoringEnabled();
        m_channel.setMonitoringEnabled(m_newMonitored);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        m_channel.setMonitoringEnabled(m_originalMonitored);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_channel.setMonitoringEnabled(m_newMonitored);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
    }

    std::string getName() const override { return m_newMonitored ? "Enable Input Monitoring" : "Disable Input Monitoring"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    bool m_newMonitored;
    bool m_originalMonitored = false;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra