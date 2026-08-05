// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/CreateLaneCommand.h"

#include "AestraAudio.h"
#include <sstream>

namespace Aestra {
namespace Audio {

CreateLaneCommand::CreateLaneCommand(PlaylistModel& model, const std::string& name)
    : m_model(model), m_name(name) {}

void CreateLaneCommand::execute() {
    if (m_executed)
        return;

    // Re-executing after an undo must restore the lane's ORIGINAL identity, not mint
    // a fresh one. Anything grouped with this command in the same undo step still
    // refers to the old ID — an AddClipCommand batched with it holds the lane it was
    // told to add to — so a new ID silently drops those members onto a lane that no
    // longer exists. CommandTransaction replays its members through execute() rather
    // than redo(), so the restore has to live here and not only in redo().
    //
    // On the first execute m_laneId is invalid and createLaneWithId generates one;
    // it also falls back to a generated ID if the requested one has been taken in
    // the meantime (#446).
    m_laneId = m_model.createLaneWithId(m_laneId, m_name);
    m_executed = true;
}

void CreateLaneCommand::undo() {
    if (!m_executed)
        return;

    m_model.removeLane(m_laneId);
    m_executed = false;
}

void CreateLaneCommand::redo() {
    if (m_executed)
        return;

    // Same identity restore as execute(); see the note there.
    m_laneId = m_model.createLaneWithId(m_laneId, m_name);
    m_executed = true;
}

std::string CreateLaneCommand::serialize() const {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"create_lane\","
        << "\"name\":\"" << m_name << "\","
        << "\"lane_id\":\"" << m_laneId.toString() << "\""
        << "}";
    return oss.str();
}

} // namespace Audio
} // namespace Aestra