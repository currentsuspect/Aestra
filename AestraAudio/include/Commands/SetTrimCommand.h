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
 * @brief Command to change a channel's trim offset (dB).
 *
 * Trim is consumed on the RT side through the ContinuousParamBuffer (like the
 * fader/pan readouts), so execute/undo/redo push both the MixerChannel member
 * (serialized project state) and the buffer slot (live RT value), then request
 * a graph rebuild for parity with the other mixer mutations.
 */
class SetTrimCommand : public ICommand {
public:
    SetTrimCommand(TrackManager& trackManager, MixerChannel& channel, uint32_t slotIndex, float newTrimDb)
        : m_trackManager(trackManager), m_channel(channel), m_slotIndex(slotIndex), m_newTrimDb(newTrimDb) {}

    void execute() override {
        if (m_executed)
            return;

        m_originalTrimDb = m_channel.getTrimDb();
        apply(m_newTrimDb);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        apply(m_originalTrimDb);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        apply(m_newTrimDb);
        m_executed = true;
    }

    std::string getName() const override { return "Set Trim"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    void apply(float trimDb) {
        m_channel.setTrimDb(trimDb);
        if (auto* continuous = m_trackManager.getContinuousParams().get()) {
            continuous->setTrimDb(m_slotIndex, trimDb);
        }
        m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::MixerStateChanged);
    }

    TrackManager& m_trackManager;
    MixerChannel& m_channel;
    uint32_t m_slotIndex;
    float m_newTrimDb;
    float m_originalTrimDb{0.0f};
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
