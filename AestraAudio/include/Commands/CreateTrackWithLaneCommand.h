// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraUUID.h"
#include "Commands/ICommand.h"
#include "Models/PlaylistModel.h"

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Create a lane AND its owning Track as one atomic operation (FD-14).
 *
 * The app creates tracks in two UI paths that must undo as ONE step: the
 * toolbar's add-track and a file/pattern drop that appends a new row. The pair
 * is a single user action — "make a track" — so splitting it across
 * CreateLaneCommand + a bare TrackManager::createTrack() call left the Track
 * behind: undoing the drop removed the lane and an orphaned Track with zero
 * owned lanes remained, and a failed import rolled back the lane but not its
 * Track (CodeRabbit #816).
 *
 * execute() creates the lane — identity-stable via createLaneWithId, same
 * semantics as CreateLaneCommand — then its Track. undo() removes the Track
 * first (detaching owned lanes), then the lane. redo() recreates both: the
 * lane id is restored (transaction members still hold it), the track id is
 * fresh (nothing references tracks by stable id from within a transaction).
 */
class CreateTrackWithLaneCommand : public ICommand {
public:
    CreateTrackWithLaneCommand(TrackManager& trackManager, const std::string& name = "");
    ~CreateTrackWithLaneCommand() override = default;

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Create Track"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_name.size(); }
    bool changesProjectState() const override { return true; }

    std::string serialize() const override;
    std::string type() const override { return "create_track_lane"; }

    PlaylistLaneID getLaneId() const { return m_laneId; }
    uint64_t getTrackId() const { return m_trackId; }

private:
    TrackManager& m_trackManager;
    PlaylistModel& m_model;
    std::string m_name;
    PlaylistLaneID m_laneId;
    uint64_t m_trackId{0};
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra