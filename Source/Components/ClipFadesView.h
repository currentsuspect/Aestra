// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/CrossfadeResolve.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

/**
 * @brief The fades the timeline should draw for a clip: its own, with any crossfade it takes part in laid over them.
 *
 * The same resolution the playback snapshot uses (V83-E10, FD-94), so what is drawn is what is heard. Two clips that
 * crossfade each draw one ramp across their overlap, and together the ramps are the X.
 */
inline ResolvedFades fadesForDisplay(const TrackManager* trackManager, const PlaylistLaneID& lane,
                                     const ClipInstance& clip) {
    if (trackManager) {
        if (const PlaylistLane* l = const_cast<TrackManager*>(trackManager)->getPlaylistModel().getLane(lane))
            return resolveClipFades(l->clips, clip);
    }
    return resolveClipFades({}, clip);
}

} // namespace Audio
} // namespace Aestra
