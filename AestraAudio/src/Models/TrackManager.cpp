#include "TrackManager.h"
namespace Aestra {
namespace Audio {

void TrackManager::rebuildAndPushSnapshot() {
    // Effect chain snapshots are retrieved during graph building (AudioGraphBuilder::buildFromTrackManager)
    // via getSnapshot(). Dirty state is consumed by PlaybackGraphController::consumePendingGraphRebuild().
    // This method is kept as a post-graph-rebuild hook for existing callers.
}

} // namespace Audio
} // namespace Aestra
