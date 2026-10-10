// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/ClipPlacement.h"

#include "Commands/AddClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "Commands/CreateTrackWithLaneCommand.h"
#include "Models/TrackManager.h"

#include <memory>

namespace Aestra {
namespace Audio {

bool placeClipAsOneUndoStep(TrackManager& trackManager, const PlaylistLaneID& laneId, const ClipInstance& clip,
                            const std::string& newLaneName, const std::string& stepName) {
    auto& playlist = trackManager.getPlaylistModel();

    // Each member is executed and validated against the state its predecessor produced,
    // then the whole is adopted as one executed transaction (as a drop does).
    std::shared_ptr<CreateTrackWithLaneCommand> laneCommand;
    PlaylistLaneID target = laneId;
    if (!target.isValid()) {
        laneCommand = std::make_shared<CreateTrackWithLaneCommand>(trackManager, newLaneName);
        laneCommand->execute();
        target = laneCommand->getLaneId();
        if (!target.isValid()) {
            return false;
        }
    }

    ClipInstance placed = clip; // the id is fixed here so success can be checked against it
    if (!placed.id.isValid()) {
        placed.id = ClipInstanceID::generate();
    }
    auto clipCommand = std::make_shared<AddClipCommand>(playlist, target, placed);
    clipCommand->execute();
    if (!playlist.getClip(placed.id)) {
        if (laneCommand) {
            laneCommand->undo();
        }
        return false;
    }

    auto transaction = std::make_shared<CommandTransaction>(stepName);
    if (laneCommand) {
        transaction->add(laneCommand);
    }
    transaction->add(clipCommand);
    transaction->markExecuted();
    return trackManager.getCommandHistory().pushExecuted(transaction);
}

} // namespace Audio
} // namespace Aestra
