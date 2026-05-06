// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioGraph.h"
#include "GraphDirtyReason.h"
#include <atomic>
#include <cstdint>

namespace Aestra {
namespace Audio {
    class AudioEngine;
    class TrackManager;

/**
 * @brief Canonical controller for playback graph rebuild draining.
 *
 * This class is the single authoritative consumer for graph dirty state.
 * TrackManager owns the dirty request state (m_graphDirty).
 * PlaybackGraphController is the only consumer via drainIfDirty().
 *
 * Usage pattern:
 *   PlaybackGraphController::requestRebuild(reason) - any state change (delegates to TrackManager)
 *   PlaybackGraphController::drainIfDirty(sampleRate) - AestraApp::run() only
 */
class PlaybackGraphController {
public:
    using GraphDirtyReason = Aestra::Audio::GraphDirtyReason;

    PlaybackGraphController() = default;
    ~PlaybackGraphController() = default;

    PlaybackGraphController(const PlaybackGraphController&) = delete;
    PlaybackGraphController& operator=(const PlaybackGraphController&) = delete;

    PlaybackGraphController(PlaybackGraphController&&) = default;
    PlaybackGraphController& operator=(PlaybackGraphController&&) = default;

    /**
     * @brief Check if a rebuild has been requested.
     */
    bool isDirty() const;

    /**
     * @brief Get the request generation counter.
     */
    uint64_t requestGeneration() const;

    /**
     * @brief Get the graph generation counter.
     */
    uint64_t graphGeneration() const;

    /**
     * @brief Get the last reason for a rebuild request.
     */
    GraphDirtyReason lastReason() const;

    /**
     * @brief Set the TrackManager for graph building.
     */
    void setTrackManager(TrackManager* trackManager);

    /**
     * @brief Set the AudioEngine for graph publishing.
     */
    void setAudioEngine(AudioEngine* engine);

    /**
     * @brief Request a graph rebuild (forwards to TrackManager).
     * This is the canonical request method for all graph dirty state.
     */
    void requestRebuild(GraphDirtyReason reason);

    /**
     * @brief Drain dirty state and rebuild/publish the graph if needed.
     * This is the ONLY canonical drain method.
     * @param sampleRate The output sample rate.
     * @return true if a graph was built and published.
     */
    bool drainIfDirty(double sampleRate);

private:
    void rebuildGraph(double sampleRate);

    TrackManager* m_trackManager{nullptr};
    AudioEngine* m_engine{nullptr};

    std::atomic<uint64_t> m_graphGeneration{0};
};

} // namespace Audio
} // namespace Aestra