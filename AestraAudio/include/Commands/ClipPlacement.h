// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/PlaylistModel.h"

#include <string>

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Adds @p clip to the arrangement as ONE undo step (V8-W6).
 *
 * When @p laneId is invalid, a lane and its owning Track are created first (FD-14,
 * CreateTrackWithLaneCommand) and named @p newLaneName; the lane and the clip then undo
 * and redo together. Going through the command history is what makes a clip edit
 * undoable, mark the project dirty and reach autosave and recovery.
 *
 * @return false when the clip could not be placed. Nothing is then recorded and any lane
 *         this call created is rolled back: a failed placement is not history.
 */
bool placeClipAsOneUndoStep(TrackManager& trackManager, const PlaylistLaneID& laneId, const ClipInstance& clip,
                            const std::string& newLaneName, const std::string& stepName);

} // namespace Audio
} // namespace Aestra
