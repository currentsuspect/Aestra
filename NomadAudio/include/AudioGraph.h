// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "AutomationCurve.h"

namespace Nomad {
namespace Audio {

struct AudioBuffer; // Forward declaration (defined in SamplePool.h)

/**
 * @brief Render-time clip state used by the audio thread.
 *
 * All pointers/offsets must be validated and set up off the RT thread before
 * becoming visible to the audio callback.
 */
struct ClipRenderState {
    std::shared_ptr<const AudioBuffer> buffer; // Owns audioData lifetime for the snapshot
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
 * @brief Represents a routing connection (User/UI Layer)
 */
struct AudioRoute {
    uint32_t targetChannelId; // Destination ID (or SPECIAL_ID_MASTER)
    float gain{1.0f};         // Send Level (Linear)
    float pan{0.0f};          // Send Pan (-1.0 to 1.0)
    bool postFader{true};     // Pre/Post Fader tap
    bool mute{false};         // Mute this specific send
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
    bool isSoloSafe{false};
    std::vector<AutomationCurve> automationCurves;
    
    // Routing (v3.1)
    uint32_t mainOutputId{0xFFFFFFFF}; // Master
    std::vector<AudioRoute> sends;
};

/**
 * @brief Immutable graph snapshot consumed by the audio thread.
 */
struct AudioGraph {
    std::vector<TrackRenderState> tracks;
    bool anySolo{false};
    // Precomputed max end sample across all clips (engine sample rate).
    // Used for transport looping without scanning clips on the RT thread.
    uint64_t timelineEndSample{0};
    double bpm{120.0};
};

} // namespace Audio
} // namespace Nomad
