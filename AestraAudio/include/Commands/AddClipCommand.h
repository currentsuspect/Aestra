#pragma once

#include "ICommand.h"
#include "PlaylistModel.h"

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

private:
    PlaylistModel& m_playlist;
    PlaylistLaneID m_laneId;
    ClipInstance m_clip;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
