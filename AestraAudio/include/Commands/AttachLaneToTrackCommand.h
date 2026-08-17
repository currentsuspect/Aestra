// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraUUID.h"
#include "Commands/ICommand.h"
#include "Models/PlaylistModel.h"

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Attach an existing lane to its owning Track (FD-14 ownership).
 *
 * Joins CreateLaneCommand in a take's transaction so lane ownership is part
 * of the same undoable step: undo detaches the lane from the track before
 * CreateLaneCommand removes the lane, keeping track->laneIds consistent.
 * Ownership is by stable ids only — never positional.
 */
class AttachLaneToTrackCommand : public ICommand {
public:
    AttachLaneToTrackCommand(TrackManager& trackManager, uint64_t trackId, PlaylistLaneID laneId);
    ~AttachLaneToTrackCommand() override = default;

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Attach Lane To Track"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    std::string serialize() const override;
    std::string type() const override { return "attach_lane_to_track"; }

private:
    TrackManager& m_trackManager;
    uint64_t m_trackId{0};
    PlaylistLaneID m_laneId;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra