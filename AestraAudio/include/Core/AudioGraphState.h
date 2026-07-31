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
    // Delay to apply for alignment. Zero means "do not delay this track" —
    // this is the single gate the RT path consults. There is deliberately no
    // per-track enable flag: one existed, nothing ever wrote it, and being
    // pinned true is what let a disabled engine keep compensating (#684).
    // Engine-wide enablement lives in AudioEngine::m_latencyCompensationEnabled
    // and reaches the RT path by zeroing this value.
    uint32_t compensationDelaySamples{0};

    // Fixed-size compensation buffer (stereo interleaved)
    // 16384 samples = ~340ms @ 48kHz, ~170ms @ 96kHz
    std::array<float, 32768> compensationBuffer{}; // 16384 frames * 2 channels
    uint32_t compensationWritePos{0};
    uint32_t compensationReadPos{0};

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
