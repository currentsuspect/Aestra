// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/DeleteLaneCommand.h"

#include "Models/TrackManager.h"

#include <algorithm>
#include <sstream>

namespace Aestra {
namespace Audio {

DeleteLaneCommand::DeleteLaneCommand(TrackManager& trackManager, PlaylistLaneID laneId)
    : m_trackManager(trackManager), m_laneId(laneId) {}

void DeleteLaneCommand::execute() {
    if (m_executed) {
        return;
    }
    auto& model = m_trackManager.getPlaylistModel();
    const PlaylistLane* lane = model.getLane(m_laneId);
    if (!lane) {
        return;
    }
    m_name = lane->name;
    m_trackId = lane->trackId;
    m_clips = lane->clips;
    m_playlistIndex = model.getLaneIndex(m_laneId);
    if (m_trackId != 0) {
        if (const Track* track = m_trackManager.getTrack(m_trackId)) {
            const auto it = std::find(track->laneIds.begin(), track->laneIds.end(), m_laneId);
            if (it != track->laneIds.end()) {
                m_laneIdsIndex = static_cast<int>(it - track->laneIds.begin());
            }
        }
        m_trackManager.detachLaneFromTrack(m_trackId, m_laneId);
    }
    model.removeLane(m_laneId);
    m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = true;
}

void DeleteLaneCommand::undo() {
    if (!m_executed) {
        return;
    }
    auto& model = m_trackManager.getPlaylistModel();
    const PlaylistLaneID restored = model.createLaneWithId(m_laneId, m_name);
    for (const auto& clip : m_clips) {
        model.addClip(restored, clip);
    }
    if (m_trackId != 0) {
        m_trackManager.attachLaneToTrack(m_trackId, restored);
        if (m_laneIdsIndex >= 0) {
            m_trackManager.moveLaneWithinTrack(m_trackId, restored, static_cast<size_t>(m_laneIdsIndex));
        }
    }
    if (m_playlistIndex >= 0) {
        model.moveLaneToIndex(restored, static_cast<size_t>(m_playlistIndex));
    }
    m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = false;
}

void DeleteLaneCommand::redo() {
    if (m_executed) {
        return;
    }
    auto& model = m_trackManager.getPlaylistModel();
    if (m_trackId != 0) {
        m_trackManager.detachLaneFromTrack(m_trackId, m_laneId);
    }
    model.removeLane(m_laneId);
    m_trackManager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_executed = true;
}

std::string DeleteLaneCommand::serialize() const {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"delete_lane\","
        << "\"lane_id\":\"" << m_laneId.toString() << "\""
        << "}";
    return oss.str();
}

} // namespace Audio
} // namespace Aestra
