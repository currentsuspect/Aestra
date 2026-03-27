#pragma once

#include "ICommand.h"
#include "Models/PlaylistModel.h"
#include "AestraUUID.h"

namespace Aestra {
namespace Audio {

class AddClipCommand : public ICommand {
public:
    AddClipCommand(PlaylistModel& playlist, PlaylistLaneID laneId, const ClipInstance& clip);

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Add Clip"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    std::string serialize() const override;
    std::string type() const override { return "add_clip"; }

    static std::shared_ptr<ICommand> deserialize(PlaylistModel& playlist, const std::string& data);

private:
    PlaylistModel& m_playlist;
    PlaylistLaneID m_laneId;
    ClipInstance m_clip;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
