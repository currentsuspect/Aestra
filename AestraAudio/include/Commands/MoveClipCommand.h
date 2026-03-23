// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"

#include <memory>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to move a clip to a new position (and optionally a new lane)
 */
class MoveClipCommand : public ICommand {
public:
    /**
     * @brief Construct a move command
     * @param model The playlist model
     * @param clipId The clip to move
     * @param newStartBeat New start position in beats
     * @param newLaneId New lane (invalid = stay in current lane)
     */
    MoveClipCommand(PlaylistModel& model, ClipInstanceID clipId, double newStartBeat,
                    PlaylistLaneID newLaneId = PlaylistLaneID())
        : m_model(model), m_clipId(clipId), m_newStartBeat(newStartBeat), m_newLaneId(newLaneId) {}

    void execute() override {
        if (m_executed)
            return;

        // Capture original state
        auto* clip = m_model.getClip(m_clipId);
        if (!clip)
            return;

        m_originalStartBeat = clip->startBeat;
        m_originalLaneId = m_model.findClipLane(m_clipId);

        // Perform the move
        m_model.moveClip(m_clipId, m_newStartBeat, m_newLaneId);

        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        // Move back to original position and lane
        m_model.moveClip(m_clipId, m_originalStartBeat, m_originalLaneId);

        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        m_model.moveClip(m_clipId, m_newStartBeat, m_newLaneId);
        m_executed = true;
    }

    std::string getName() const override { return "Move Clip"; }

    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PlaylistModel& m_model;
    ClipInstanceID m_clipId;
    double m_newStartBeat;
    PlaylistLaneID m_newLaneId;

    // Original state for undo
    double m_originalStartBeat = 0.0;
    PlaylistLaneID m_originalLaneId;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
