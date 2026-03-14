// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to duplicate a clip at a new position
 */
class DuplicateClipCommand : public ICommand {
public:
    /**
     * @brief Construct a duplicate command
     * @param model The playlist model
     * @param sourceClipId The clip to duplicate
     * @param targetStartBeat Where to place the duplicate
     * @param targetLaneId Lane for duplicate (invalid = same as source)
     */
    DuplicateClipCommand(PlaylistModel& model, ClipInstanceID sourceClipId, double targetStartBeat,
                         PlaylistLaneID targetLaneId = PlaylistLaneID())
        : m_model(model), m_sourceClipId(sourceClipId), m_targetStartBeat(targetStartBeat), m_targetLaneId(targetLaneId) {}

    void execute() override {
        if (m_executed)
            return;

        auto* sourceClip = m_model.getClip(m_sourceClipId);
        if (!sourceClip)
            return;

        // Determine target lane
        PlaylistLaneID laneId = m_targetLaneId;
        if (!laneId.isValid()) {
            laneId = m_model.findClipLane(m_sourceClipId);
        }

        // Create duplicate
        ClipInstance duplicate = *sourceClip;
        duplicate.id = ClipInstanceID::generate();  // New ID for the duplicate
        duplicate.startBeat = m_targetStartBeat;

        m_duplicateId = duplicate.id;

        // Add the duplicate
        m_model.addClip(laneId, duplicate);

        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        // Remove the duplicate
        m_model.removeClip(m_duplicateId);

        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;

        execute();
    }

    std::string getName() const override { return "Duplicate Clip"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    /**
     * @brief Get the ID of the created duplicate (valid after execute)
     */
    ClipInstanceID getDuplicateId() const { return m_duplicateId; }

private:
    PlaylistModel& m_model;
    ClipInstanceID m_sourceClipId;
    double m_targetStartBeat;
    PlaylistLaneID m_targetLaneId;

    ClipInstanceID m_duplicateId;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
