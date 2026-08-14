// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AutomationCurve.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace Aestra {
namespace Audio {

struct AudioBuffer; // Forward declaration (defined in SamplePool.h)
struct AudioBufferData;
class EffectChain;  // Forward declaration

/**
 * @brief Render-time clip state used by the audio thread.
 *
 * All pointers/offsets must be validated and set up off the RT thread before
 * becoming visible to the audio callback.
 */
struct ClipRenderState {
    std::shared_ptr<const AudioBuffer> buffer; // Owns audioData lifetime for the snapshot
    std::shared_ptr<const AudioBufferData> bufferOwner;
    const float* audioData{nullptr};           // Interleaved stereo (engine format)
    uint64_t startSample{0};                   // Absolute project sample (engine rate)
    uint64_t endSample{0};                     // Exclusive end
    double sampleOffset{0.0};                  // Offset into audioData in frames (double for sub-sample precision)
    uint64_t totalFrames{0};                   // Bounds for audioData to guard OOB
    double sourceSampleRate{48000.0};          // Original clip sample rate
    uint32_t channels{2};                      // Source channels (1=mono, 2=stereo)
    float gain{1.0f};
    float pan{0.0f};
    float playbackRate{1.0f};
    uint64_t fadeInSamples{0};
    uint64_t fadeOutSamples{0};
};

/**
 * @brief Represents a routing connection (User/UI Layer)
 */
class EffectChainSnapshot; // Forward declaration for Pass 3

struct AudioRoute {
    uint32_t targetChannelId; // Destination ID (or SPECIAL_ID_MASTER)
    float gain{1.0f};         // Send Level (Linear)
    float pan{0.0f};          // Send Pan (-1.0 to 1.0)
    bool postFader{true};     // Pre/Post Fader tap
    bool mute{false};         // Mute this specific send
    bool sidechainOnly{false}; // Route exists for sidechain/control input, not audible mix
    uint64_t sendId{0};       // Stable per-channel send identity (Contract D2).
                              // Minted by MixerChannel on creation; survives
                              // index shifts and undo; 0 = unassigned.
};

/**
 * @brief Render-time track state.
 */
struct TrackRenderState {
    uint32_t trackId{0};    // Stable track identity
    uint32_t trackIndex{0}; // Compact zero-based index in TrackManager ordering
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
    std::shared_ptr<const EffectChainSnapshot> effectChainSnapshot{nullptr}; // Pass 3: snapshot-based effect chain
};

/**
 * @brief Immutable graph snapshot consumed by the audio thread.
 */
struct AudioGraph {
    static constexpr size_t kInvalidTrackIndex = std::numeric_limits<size_t>::max();

    std::vector<TrackRenderState> tracks;
    /** Audio clips whose source routes directly to Master. */
    std::vector<ClipRenderState> masterClips;
    /** Master strip insert chain snapshot (processed on the summed master
     * buffer before the master fader and safety limiter). Immutable once the
     * graph is published, like channel chain snapshots. */
    std::shared_ptr<const EffectChainSnapshot> masterEffectChainSnapshot;
    bool anySolo{false};
    // Precomputed max end sample across all clips (engine sample rate).
    // Used for transport looping without scanning clips on the RT thread.
    uint64_t timelineEndSample{0};
    double bpm{120.0};

    // Routing topology compiled off the audio thread.
    std::vector<size_t> trackIndexById;
    std::vector<std::vector<size_t>> audibleDownstream;
    std::vector<std::vector<size_t>> audibleIncoming;
    std::vector<std::vector<size_t>> sidechainIncoming;
    std::vector<std::vector<size_t>> topologicalEdges;
    std::vector<size_t> topologicalOrder;
    bool hasRoutingCycle{false};
};

void finalizeAudioGraphRouting(AudioGraph& graph);

} // namespace Audio
} // namespace Aestra
