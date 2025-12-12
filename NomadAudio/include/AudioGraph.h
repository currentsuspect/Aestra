// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief Render-time clip state used by the audio thread.
 *
 * All pointers/offsets must be validated and set up off the RT thread before
 * becoming visible to the audio callback.
 */
struct ClipRenderState {
    const float* audioData{nullptr};    // Interleaved stereo (engine format)
    uint64_t startSample{0};            // Absolute project sample (engine rate)
    uint64_t endSample{0};              // Exclusive end
    uint64_t sampleOffset{0};           // Offset into audioData in frames
    uint64_t totalFrames{0};            // Bounds for audioData to guard OOB
    double sourceSampleRate{48000.0};   // Original clip sample rate
    float gain{1.0f};
    float pan{0.0f};
};

/**
 * @brief Render-time track state.
 */
struct TrackRenderState {
    uint32_t trackId{0};                 // Stable track identity
    uint32_t trackIndex{0};              // Compact zero-based index in TrackManager ordering
    std::vector<ClipRenderState> clips;
    float volume{1.0f};
    float pan{0.0f};
    bool mute{false};
    bool solo{false};
};

/**
 * @brief Immutable graph snapshot consumed by the audio thread.
 */
struct AudioGraph {
    std::vector<TrackRenderState> tracks;
};

} // namespace Audio
} // namespace Nomad
