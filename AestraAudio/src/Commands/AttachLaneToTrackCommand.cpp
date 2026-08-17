// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/AttachLaneToTrackCommand.h"

#include "Models/TrackManager.h"

#include <sstream>

namespace Aestra {
namespace Audio {

AttachLaneToTrackCommand::AttachLaneToTrackCommand(TrackManager& trackManager, uint64_t trackId, PlaylistLaneID laneId)
    : m_trackManager(trackManager), m_trackId(trackId), m_laneId(laneId) {}

void AttachLaneToTrackCommand::execute() {
    if (m_executed) {
        return;
    }
    m_executed = m_trackManager.attachLaneToTrack(m_trackId, m_laneId);
}

void AttachLaneToTrackCommand::undo() {
    if (!m_executed) {
        return;
    }
    m_trackManager.detachLaneFromTrack(m_trackId, m_laneId);
    m_executed = false;
}

void AttachLaneToTrackCommand::redo() {
    if (m_executed) {
        return;
    }
    m_executed = m_trackManager.attachLaneToTrack(m_trackId, m_laneId);
}

std::string AttachLaneToTrackCommand::serialize() const {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"attach_lane_to_track\","
        << "\"track_id\":" << m_trackId << ","
        << "\"lane_id\":\"" << m_laneId.toString() << "\""
        << "}";
    return oss.str();
}

} // namespace Audio
} // namespace Aestra