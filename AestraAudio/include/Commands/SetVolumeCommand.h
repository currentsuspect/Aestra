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
 * @brief Command to change a channel's volume
 *
 * Requests an audio graph rebuild on every mutation (execute/undo/redo):
 * the strip gain reads TrackRenderState.volume, which is baked into the
 * immutable graph snapshot at build time — a volume change that only updates
 * the channel atomic leaves the snapshot stale and the fader cosmetic until
 * an unrelated operation rebuilds the graph (the T-4 mute-bug class).
 */
class SetVolumeCommand : public ICommand {
public:
    SetVolumeCommand(TrackManager& trackManager, MixerChannel& channel, float newVolume)
        : m_trackManager(trackManager), m_channel(channel), m_newVolume(newVolume) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalVolume = m_channel.getVolume();
        m_channel.setVolume(m_newVolume);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
        Log::info("[SetVolume] execute: " + std::to_string(m_originalVolume) + " -> " + std::to_string(m_newVolume));
    }

    void undo() override {
        if (!m_executed)
            return;

        m_channel.setVolume(m_originalVolume);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = false;
        Log::info("[SetVolume] undo: " + std::to_string(m_newVolume) + " -> " + std::to_string(m_originalVolume));
    }

    void redo() override {
        if (m_executed)
            return;

        m_channel.setVolume(m_newVolume);
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
        m_executed = true;
    }

    std::string getName() const override { return "Set Volume"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    float m_newVolume;
    float m_originalVolume = 0.0f;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
