// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Double-precision linear-ramp parameter smoother for zipper-free automation.
 *
 * Advances one sample at a time via next(). Call beginRamp() after setTarget()
 * to start a finite-length linear ramp; snap() jumps immediately.
 * NaN/Inf values are sanitized at every entry point to prevent propagation.
 */
struct SmoothedParamD {
    double current{1.0};      ///< Current output value.
    double target{1.0};       ///< Target value the ramp is heading toward.
    double step{0.0};         ///< Per-sample increment (0 when idle).
    uint32_t samplesRemaining{0}; ///< Frames left in the current ramp.

    /** @brief Advance the smoother by one sample and return the current value. */
    inline double next() {
        if (samplesRemaining > 0) {
            current += step;
            --samplesRemaining;
            if (samplesRemaining == 0) {
                current = target;
                step = 0.0;
            }
        }
        return current;
    }

    /** @brief Set a new target value. NaN/Inf are replaced with current to avoid clicks. */
    void setTarget(double t) {
        // Sanitize target to prevent NaN/Inf propagation
        // Fall back to current value, then to 0.0, to guarantee finite output
        if (std::isfinite(t)) {
            target = t;
        } else if (std::isfinite(current)) {
            target = current;
        } else {
            target = 0.0;
        }
    }

    /**
     * @brief Begin a linear ramp from current to target over @p samples frames.
     * @param samples Number of frames for the ramp. 0 or already-at-target snaps immediately.
     */
    void beginRamp(uint32_t samples) {
        // Sanitize both current and target before computing step
        // Mirror setTarget behavior: fall back to the other finite value to avoid clicks
        if (!std::isfinite(current)) current = std::isfinite(target) ? target : 0.0;
        if (!std::isfinite(target)) target = current;

        if (samples == 0 || current == target) {
            current = target;
            step = 0.0;
            samplesRemaining = 0;
            return;
        }
        samplesRemaining = samples;
        step = (target - current) / static_cast<double>(samples);

        // Validate step is finite, otherwise bypass ramp
        if (!std::isfinite(step)) {
            step = 0.0;
            samplesRemaining = 0;
            current = target;
        }
    }
    /** @brief Snap current to target immediately, cancelling any active ramp. */
    void snap() {
        // Sanitize target before assigning — same contract as setTarget()/beginRamp()
        if (!std::isfinite(target)) target = std::isfinite(current) ? current : 0.0;
        current = target;
        step = 0.0;
        samplesRemaining = 0;
    }
};

/**
 * @brief Per-outgoing-edge compensation state for graph-aware PDC (v2 P4b.2+).
 *
 * Owned by `TrackRTState`. One slot for the track's mainOutputId edge, one per
 * send. Storage is a power-of-two heap-allocated ring buffer so RT-side index
 * arithmetic can use mask-based wraparound.
 *
 * RT safety (P4b.3): the buffer pointer + sizing fields are atomically
 * published by the off-RT apply pass. The audio thread reads them via
 * acquire-load and uses the captured snapshot for the entire block. When the
 * off-RT apply pass grows a buffer, the previous allocation is retired into
 * `retiredBuffer` and kept alive for one full recompute cycle; this gives the
 * audio thread a guaranteed-valid pointer for any block in flight at the
 * moment of growth.
 *
 * NOT copyable (atomic members); moved-from state is invalid for RT use.
 */
struct EdgeDelayState {
    EdgeDelayState() = default;
    EdgeDelayState(const EdgeDelayState&) = delete;
    EdgeDelayState& operator=(const EdgeDelayState&) = delete;
    EdgeDelayState(EdgeDelayState&&) = delete;
    EdgeDelayState& operator=(EdgeDelayState&&) = delete;

    /** @brief Samples of delay to apply (0 = no compensation). RT reads. */
    std::atomic<uint32_t> compensationSamples{0};
    /** @brief Ring-buffer capacity minus one (power of two semantics). */
    std::atomic<uint32_t> capacityMask{0};
    /** @brief Stereo-interleaved buffer pointer (capacityMask+1 frames). RT reads. */
    std::atomic<float*> bufferPtr{nullptr};
    /** @brief RT-side write cursor (frame count); RT updates each block; off-RT zeroes on growth. */
    std::atomic<uint32_t> writePos{0};

    // Off-RT-owned storage. RT never touches these directly; it goes through
    // bufferPtr.
    std::unique_ptr<float[]> ownedBuffer;
    /** @brief Previous allocation kept alive one generation to outlive in-flight RT blocks. */
    std::unique_ptr<float[]> retiredBuffer;
};

/**
 * @brief One immutable compensation-ring publication (PR #730 follow-up).
 *
 * This is the coherent snapshot unit: RT acquire-loads the `published`
 * pointer once per block and reads every field from the single descriptor it
 * lands on. Separate atomics (like EdgeDelayState's compensationSamples /
 * bufferPtr / capacityMask) cannot be read as one consistent tuple — a
 * control-thread publication can interleave the loads and give RT fields from
 * two generations. A single pointer to an immutable struct removes that class
 * of hazard entirely.
 */
struct CompensationRingDescriptor {
    CompensationRingDescriptor() = default;
    CompensationRingDescriptor(const CompensationRingDescriptor&) = delete;
    CompensationRingDescriptor& operator=(const CompensationRingDescriptor&) = delete;
    CompensationRingDescriptor(CompensationRingDescriptor&&) = delete;
    CompensationRingDescriptor& operator=(CompensationRingDescriptor&&) = delete;

    /** @brief Monotonic per-publish counter. RT acks the generation it observed. */
    uint32_t generation{0};
    /** @brief Samples of delay to apply; 0 = no compensation (RT gate). */
    uint32_t delaySamples{0};
    /** @brief Ring capacity in frames (power of two). */
    uint32_t capacityFrames{0};
    /** @brief Stereo-interleaved buffer, capacityFrames * 2 floats. Immutable post-publish. */
    float* buffer{nullptr};
    /** @brief Control-owned storage backing @ref buffer. Each generation owns its own
     *         ring, so a retired generation stays alive (with its ring) until acked. */
    std::unique_ptr<float[]> ownedBuffer;
};

/**
 * @brief Ownership + RT slot for one track's compensation ring.
 *
 * The published unit is a single immutable @ref CompensationRingDescriptor
 * behind one atomic pointer, so RT sees {ring, capacity, delay, generation} as
 * one consistent tuple (PR #730 follow-up). When control publishes a new
 * generation the previous one is moved into @ref retired and kept alive until
 * the RT thread acknowledges a newer generation. This is the buffer retirement
 * protocol: `publish N -> RT consumes N -> RT acks N -> only then may control
 * reclaim anything retired behind N`. No buffer is ever cleared or released
 * that RT could still be reading; "reset" is replacement, never fill_n against
 * published storage.
 *
 * The RT write cursor is deliberately NOT in the descriptor (descriptors are
 * immutable) and NOT atomic: control never mutates a published ring or its
 * cursor, so the cursor is RT-owned plain state. Atomics exist only where
 * ownership crosses threads (published pointer, acked generation).
 *
 * NOT copyable / movable (atomic members). Lives behind a unique_ptr.
 */
struct TrackCompensationState {
    TrackCompensationState() = default;
    TrackCompensationState(const TrackCompensationState&) = delete;
    TrackCompensationState& operator=(const TrackCompensationState&) = delete;
    TrackCompensationState(TrackCompensationState&&) = delete;
    TrackCompensationState& operator=(TrackCompensationState&&) = delete;

    /** @brief Descriptor of the currently published generation. RT acquire-loads per block. */
    std::atomic<const CompensationRingDescriptor*> published{nullptr};
    /** @brief Highest generation RT has acknowledged consuming. Control acquire-loads
     *         before reclaiming retired generations. */
    std::atomic<uint32_t> ackedGeneration{0};

    /** @brief RT-side write cursor (frames). RT updates it each block; control never
     *         writes it. Plain, not atomic: single-owner (RT).
     *
     * Deliberately lives on the slot (state object) rather than in the
     * descriptor, so it survives descriptor replacement: when control publishes a
     * new generation, RT keeps cycling the same cursor into the new ring at the
     * same phase. Copying the cursor into a fresh descriptor per publish would
     * both mutate an "immutable" unit and lose the position continuity across
     * migrations; keeping it slot-owned means a mid-block publication cannot
     * split-cursor a mixed buffer. Control never touches it, so no atomic fence
     * is needed. */
    uint32_t compensationWritePos{0};

    // Control-owned. RT never touches these.
    /** @brief Monotonic publisher counter; assigned to the next descriptor's generation. */
    uint32_t nextGeneration{1};
    /** @brief Stable trackId that owns the published generation (see @ref prepareCompensationRing).
     *
     * Control stamps this on every publication. It exists to detect POSITIONAL
     * slot reuse: `m_trackState` is indexed by graph loop ordinal, so deleting a
     * channel shifts every later track into the previous occupant's slot. If the
     * new occupant's delay equals the old occupant's, the no-op gate below would
     * otherwise keep publishing nothing and RT would keep reading the previous
     * track's ring contents (audible old-audio leakage). An owner mismatch forces
     * a fresh zeroed-ring publication even when the delay is unchanged.
     *
     * Zero-delay publications ignore ownership: with no ring in use there is no
     * content to leak, so re-clearing stays cheap. */
    uint32_t ownerTrackId{0};
    /** @brief Descriptor of the currently published generation (storage holder). */
    std::unique_ptr<CompensationRingDescriptor> currentDesc;
    /** @brief Previous publications, retired until acked (see class comment). */
    std::vector<std::unique_ptr<CompensationRingDescriptor>> retired;
};

struct TrackRTState {
    // Optimized: Store smoothed L/R gains directly to avoid per-sample sin/cos
    SmoothedParamD gainL;
    SmoothedParamD gainR;
    std::vector<SmoothedParamD> sendGainL;
    std::vector<SmoothedParamD> sendGainR;
    std::vector<double> preFaderBuffer;

    // Logical state for command updates (snapshot of last known values)
    float currentVolume{1.0f};
    float currentPan{0.0f};

    bool mute{false};
    bool solo{false};
    bool soloSafe{false};

    // Track the last active send count so renderGraph() can detect when
    // sends are added/removed without resizing the pre-sized vectors.
    size_t lastActiveSendCount{0};

    // === Plugin Delay Compensation ===
    uint32_t pluginLatencySamples{0};        // Total latency from effect chain

    // Compensation ring (stereo interleaved), allocated ONLY for tracks that
    // actually carry a nonzero delay: the 128 KiB ring lives inside
    // CompensationRingDescriptor and is allocated on the FIRST nonzero-delay
    // publication. The tiny slot below (~200 B) is pre-created on TrackRTState
    // construction so the shared pointer is immutable for the RT thread.
    //
    // This was a std::array<float, 32768> — 128 KiB embedded BY VALUE in every
    // TrackRTState. m_trackState.reserve(kMaxTracks) therefore asked for a
    // single 512.8 MiB allocation up front (131,281 B * 4096), which was ~98%
    // untouched under demand paging until the audio stream's
    // mlockall(MCL_CURRENT|MCL_FUTURE) faulted every page in and pinned it —
    // 520 MiB resident and unswappable on a 3.68 GiB machine (#727). Physical
    // cost is now proportional to the tracks that need compensation and to the
    // delay they actually require; the 4096 logical track ceiling is unchanged.
    //
    // Keeping this struct small also matters for iteration: it dropped from
    // ~128 KiB to ~200 B.
    //
    // The ring + delay are published as an immutable CompensationRingDescriptor
    // behind an atomic pointer on the slot (see TrackCompensationState): RT
    // reads {published, capacity, delay, generation} as one consistent tuple,
    // and generations are retired only after RT acknowledges them.
    //
    // The slot itself is pre-created when the TrackRTState is constructed and
    // NEVER reassigned afterward, so `compensation.get()` is immutable for the
    // RT thread: the whole 128 KiB ring stays lazy (behind `published`, which
    // control fills on the first nonzero-delay publication). This removes the
    // first-use assignment entirely, so control can never write this unique_ptr
    // while an RT block is reading it (CodeRabbit, PR #730).
    std::unique_ptr<TrackCompensationState> compensation{std::make_unique<TrackCompensationState>()};

    // PDC v2 (P4b.2/P4b.3): per-outgoing-edge compensation state. Populated
    // off-RT by AudioEngine::calculateLatencyCompensation() from
    // SolvedLatencyTopology::edges; consumed RT-side in processBlock.
    //
    // Stored by unique_ptr because EdgeDelayState contains std::atomic members
    // (non-movable). Lazily allocated by the apply pass when an edge actually
    // needs compensation. A nullptr means "no compensation for this edge".
    std::unique_ptr<EdgeDelayState> mainOutEdgeDelay;
    std::vector<std::unique_ptr<EdgeDelayState>> sendEdgeDelays;
};

struct RuntimeConnection {
    double* destinationBufferL{nullptr}; // Raw pointer to Target Left Buffer
    double* destinationBufferR{nullptr}; // Raw pointer to Target Right Buffer
    size_t stride{2};                    // Interleaved stride (2 for stereo)
    double gainL{1.0};                   // Left Channel Gain (Linear)
    double gainR{1.0};                   // Right Channel Gain (Linear)
};

struct RenderTrack {
    uint32_t trackIndex{0};
    double* selfBuffer{nullptr};                      // Pointer to this track's output buffer (interleaved)
    std::vector<RuntimeConnection> activeConnections; // Pre-resolved targets
};

/**
 * @brief Encapsulates the complete state required to render audio for a moment in time.
 * This object can be deep-copied to create a snapshot for the background thread.
 */
struct AudioGraphState {
    // The ordered list of tracks to render (Topology)
    std::vector<RenderTrack> renderTracks;

    // The runtime state of each track (Volume, Pan, Mute)
    // Indexed by trackIndex
    std::vector<TrackRTState> trackStates;

    // === Plugin Delay Compensation State ===
    uint32_t maxProjectLatencySamples{0};    // Slowest track latency
    bool latencyCompensationEnabled{true};   // Global enable/disable

    // Note: Plugin state will be added here later (v4.0 Hybrid Engine)
    // For now, plugins are still referenced via pointers in the AudioGraph or global lookups.
    // In the true Hybrid engine, we will need to clone plugin instances or parameters here.
};

} // namespace Audio
} // namespace Aestra
