// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioGraph.h"
#include "TrackManager.h"

namespace Aestra {
namespace Audio {

/**
 * @brief Builds AudioGraph snapshots from higher-level track state.
 *
 * This runs off the real-time thread. The resulting graph is immutable and can
 * be swapped into EngineState for RT consumption.
 */
class AudioGraphBuilder {
public:
    /**
     * @brief Build a render graph from the current TrackManager state.
     *
     * @param trackManager Source track manager (UI/engine thread)
     * @param includeClips When false the graph carries the full mixer topology
     *        and effect chains but no playlist clips, so sources are silent while
     *        effects keep processing their decay. Used by the export tail (#992).
     */
    static AudioGraph buildFromTrackManager(TrackManager& trackManager, bool includeClips = true);
};

} // namespace Audio
} // namespace Aestra
