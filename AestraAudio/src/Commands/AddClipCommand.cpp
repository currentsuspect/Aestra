#include "Commands/AddClipCommand.h"

#include "AestraAudio.h"
#include <sstream>

namespace Aestra {
namespace Audio {

AddClipCommand::AddClipCommand(PlaylistModel& playlist, PlaylistLaneID laneId, const ClipInstance& clip)
    : m_playlist(playlist), m_laneId(laneId), m_clip(clip) {}

void AddClipCommand::execute() {
    if (m_executed)
        return;

    // Ensure unique ID if not already valid
    if (!m_clip.id.isValid()) {
        m_clip.id = ClipInstanceID::generate();
    }

    m_playlist.addClip(m_laneId, m_clip);
    m_executed = true;
}

void AddClipCommand::undo() {
    if (!m_executed)
        return;

    m_playlist.removeClip(m_clip.id);
    m_executed = false;
}

void AddClipCommand::redo() {
    if (m_executed)
        return;

    // Re-add using the exact same ID so it restores state correctly
    m_playlist.addClip(m_laneId, m_clip);
    m_executed = true;
}

std::string AddClipCommand::serialize() const {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"add_clip\","
        << "\"lane_id\":\"" << m_laneId.toString() << "\","
        << "\"clip_id\":\"" << m_clip.id.toString() << "\","
        << "\"start_beat\":" << m_clip.startBeat << ","
        << "\"end_beat\":" << m_clip.endBeat()
        << "}";
    return oss.str();
}

} // namespace Audio
} // namespace Aestra
