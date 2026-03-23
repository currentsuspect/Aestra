// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"

namespace Aestra {
namespace Audio {

/**
 * @brief Command to remove a clip from the playlist
 */
class RemoveClipCommand : public ICommand {
public:
    RemoveClipCommand(PlaylistModel& model, ClipInstanceID clipId) : m_model(model), m_clipId(clipId) {}

    void execute() override {
        if (m_executed)
            return;

        // Capture clip state before removal
        auto* clip = m_model.getClip(m_clipId);
        if (!clip)
            return;

        m_clipState = *clip;
        m_laneId = m_model.findClipLane(m_clipId);

        // Remove the clip
        m_model.removeClip(m_clipId);

        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        // Re-add the clip with its original state
        m_model.addClip(m_laneId, m_clipState);

        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_model.removeClip(m_clipId);
        m_executed = true;
    }

    std::string getName() const override { return "Remove Clip"; }

    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PlaylistModel& m_model;
    ClipInstanceID m_clipId;

    // State for undo
    ClipInstance m_clipState;
    PlaylistLaneID m_laneId;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
