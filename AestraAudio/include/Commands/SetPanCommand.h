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
 * @brief Command to change a channel's pan position
 *
 * Requests an audio graph rebuild on every mutation (execute/undo/redo):
 * the strip pan reads TrackRenderState.pan, baked into the immutable graph
 * snapshot at build time — a pan change without a rebuild leaves the snapshot
 * stale (the T-4 mute-bug class).
 */
class SetPanCommand : public ICommand {
public:
    SetPanCommand(TrackManager& trackManager, MixerChannel& channel, float newPan)
        : m_trackManager(trackManager), m_channel(channel), m_newPan(newPan) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalPan = m_channel.getPan();
        m_channel.setPan(m_newPan);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        m_channel.setPan(m_originalPan);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_channel.setPan(m_newPan);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
    }

    std::string getName() const override { return "Set Pan"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    float m_newPan;
    float m_originalPan = 0.0f;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
