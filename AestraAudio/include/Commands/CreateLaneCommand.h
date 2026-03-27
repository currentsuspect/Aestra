// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PlaylistModel.h"
#include "AestraUUID.h"

namespace Aestra {
namespace Audio {

/**
 * @brief Command to create a new lane (track) in the playlist
 */
class CreateLaneCommand : public ICommand {
public:
    CreateLaneCommand(PlaylistModel& model, const std::string& name = "");
    ~CreateLaneCommand() override = default;

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Add Track"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    std::string serialize() const override;
    std::string type() const override { return "create_lane"; }

    PlaylistLaneID getLaneId() const { return m_laneId; }

    static std::shared_ptr<ICommand> deserialize(PlaylistModel& model, const std::string& data);

private:
    PlaylistModel& m_model;
    std::string m_name;
    PlaylistLaneID m_laneId;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra