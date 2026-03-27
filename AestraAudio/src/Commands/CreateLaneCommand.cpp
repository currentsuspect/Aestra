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

    m_laneId = m_model.createLane(m_name);
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

    // Re-create the lane with the same name
    // The ID will be different on redo since we can't reuse the old ID
    m_laneId = m_model.createLane(m_name);
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