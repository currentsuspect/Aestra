// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraUUID.h"
#include "Commands/ICommand.h"
#include "Models/PlaylistModel.h"

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Delete a lane and its clips (FD-14 phase-5 lane management).
 *
 * Owned take lanes are detached from their Track before removal; unowned
 * lanes are removed outright. Undo restores the lane with its ORIGINAL id,
 * position, and ownership (createLaneWithId + moveLaneToIndex +
 * moveLaneWithinTrack), so an undone deletion is identity-stable AND stays in
 * its playlist/track order. Deleting a lane does not delete its patterns —
 * same policy as Delete Clip.
 */
class DeleteLaneCommand : public ICommand {
public:
    DeleteLaneCommand(TrackManager& trackManager, PlaylistLaneID laneId);
    ~DeleteLaneCommand() override = default;

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Delete Lane"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_clips.size() * sizeof(ClipInstance); }
    bool changesProjectState() const override { return true; }

    std::string serialize() const override;
    std::string type() const override { return "delete_lane"; }

    static std::shared_ptr<ICommand> deserialize(PlaylistModel& model, const std::string& data);

private:
    TrackManager& m_trackManager;
    PlaylistLaneID m_laneId;
    std::string m_name;
    uint64_t m_trackId{0};
    std::vector<ClipInstance> m_clips;
    int m_playlistIndex{-1};
    int m_laneIdsIndex{-1};
    bool m_wasActiveLane{false};
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
