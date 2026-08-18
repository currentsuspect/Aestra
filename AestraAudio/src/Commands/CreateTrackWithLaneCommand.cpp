// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/CreateTrackWithLaneCommand.h"

#include "Models/TrackManager.h"

#include <sstream>

namespace Aestra {
namespace Audio {

CreateTrackWithLaneCommand::CreateTrackWithLaneCommand(TrackManager& trackManager, const std::string& name)
    : m_trackManager(trackManager), m_model(trackManager.getPlaylistModel()), m_name(name) {}

void CreateTrackWithLaneCommand::execute() {
    if (m_executed) {
        return;
    }

    // Identity-stable lane restore, same rule as CreateLaneCommand: a
    // CommandTransaction replays execute() after an undo, and its clip member
    // still holds the ORIGINAL lane id — a fresh id would silently drop the
    // clip onto a lane that no longer exists.
    m_laneId = m_model.createLaneWithId(m_laneId, m_name);
    if (!m_laneId.isValid()) {
        return;
    }
    m_trackId = m_trackManager.createTrack(m_laneId, m_name);
    if (m_trackId == 0) {
        // Lane created but the track mint failed: roll back so execute() is
        // atomic — no half of the pair may outlive the other.
        m_model.removeLane(m_laneId);
        m_laneId = PlaylistLaneID{};
        return;
    }
    m_executed = true;
}

void CreateTrackWithLaneCommand::undo() {
    if (!m_executed) {
        return;
    }
    if (m_trackId != 0) {
        // Track first: detaches its owned lanes, then the lane itself goes.
        m_trackManager.removeTrack(m_trackId);
        m_trackId = 0;
    }
    m_model.removeLane(m_laneId);
    m_executed = false;
}

void CreateTrackWithLaneCommand::redo() {
    if (m_executed) {
        return;
    }
    m_laneId = m_model.createLaneWithId(m_laneId, m_name);
    if (!m_laneId.isValid()) {
        return;
    }
    m_trackId = m_trackManager.createTrack(m_laneId, m_name);
    m_executed = true;
}

std::string CreateTrackWithLaneCommand::serialize() const {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"create_track_lane\","
        << "\"name\":\"" << m_name << "\","
        << "\"lane_id\":\"" << m_laneId.toString() << "\""
        << "}";
    return oss.str();
}

} // namespace Audio
} // namespace Aestra