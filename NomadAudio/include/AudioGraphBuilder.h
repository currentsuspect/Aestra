// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioGraph.h"
#include "TrackManager.h"

namespace Nomad {
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
     * @param outputSampleRate Target sample rate for rendering (engine/device rate)
     */
    static AudioGraph buildFromTrackManager(const TrackManager& trackManager, double outputSampleRate);
};

} // namespace Audio
} // namespace Nomad
