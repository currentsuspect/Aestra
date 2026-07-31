// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraThreading.h"
#include "AudioCommandQueue.h"
#include "AudioDriverTypes.h"
#include "AudioGraphState.h"
#include "AudioRenderer.h"
#include "AudioTelemetry.h"
#include "ChannelSlotMap.h"
#include "ContinuousParamBuffer.h"
#include "DSP/DCBlocker.h"
#include "DSP/TruePeakMeter.h"
#include "EngineState.h"
#include "GarbageCollector.h"
#include "Interpolators.h"
#include "LatencyTopology.h"
#include "MasterSafetyLimiter.h"
#include "MeterSnapshot.h"
#include "MetronomeEngine.h"     // [NEW]
#include "Models/TrackManager.h" // [NEW] For headless rendering
#include "Playback/LiveMidiQueue.h"
#include "PluginHost.h" // For MidiBuffer [NEW]

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

class UnitManager;
struct AudioArsenalSnapshot;
class PatternPlaybackEngine;
class AuditionEngine;
class PreviewEngine;

namespace Plugins {
class SamplerPlugin; // Forward declare for RT cache
}

/**
 * @brief Real-time audio engine with 144dB dynamic range.
 *
 * Design principles:
 * - Zero allocations in RT thread (all buffers pre-allocated)
 * - Double-precision internal processing (144dB dynamic range)
 * - Lock-free command processing
 * - Multiple interpolation quality modes
 * - Proper headroom management
 * - Soft limiting to prevent digital clipping
 */
class AudioEngine {
    friend class AudioRenderer; // Allow access to private members during hybrid engine transition
    friend class AudioExporter; // Allow access for offline rendering/export
public:
    static constexpr size_t kMaxCachedSamplers = 64;

    // Sampler cache data structures (public for function declarations)
    struct SamplerCacheData {
        std::array<Plugins::SamplerPlugin*, kMaxCachedSamplers> samplers{};
        std::array<std::shared_ptr<Plugins::SamplerPlugin>, kMaxCachedSamplers> owners{};
        size_t count = 0;
    };

    struct SamplerCacheSnapshot {
        std::array<Plugins::SamplerPlugin*, kMaxCachedSamplers> samplers{};
        std::array<std::shared_ptr<Plugins::SamplerPlugin>, kMaxCachedSamplers> owners{};
        size_t count = 0;
    };

    struct UnitManagerSnapshot {
        UnitManager* manager{nullptr};
        SamplerCacheSnapshot cache;
    };

    /** @brief Construct the realtime audio engine. */
    AudioEngine();
    /** @brief Destroy the realtime audio engine and release owned resources. */
    ~AudioEngine();

    // No singleton accessor: every AudioEngine is explicitly owned by its
    // creator (controller unique_ptr, headless tools, tests). Enforced by the
    // NoAudioEngineSingletonGuard source check.

    /**
     * @brief Process a single audio block (driver callback entry).
     * Must remain lock-free, allocation-free.
     */
    int processBlock(float* outputBuffer, const float* inputBuffer, uint32_t numFrames, double streamTime);

    /**
     * @brief Non-real-time maintenance hook for deferred resource reclamation.
     *
     * Safe to call from the UI loop, idle tick, export loop, or headless render
     * loop. Throttled internally so frequent calls stay cheap.
     */
    void performNonRealtimeMaintenance();

    /**
     * @brief Non-real-time shutdown drain for deferred resources.
     *
     * Call only after the audio stream has been stopped and closed.
     */
    void drainDeferredResourcesForShutdown();

    /**
     * @brief Immediate panic/reset (Double Stop).
     * Clears all buffers and resets plugin states. Main Thread.
     */
    void panic();

    /** @brief Audio-input callback signature invoked on the realtime thread. */
    using InputCallback = void (*)(const float* inputBuffer, uint32_t numFrames, void* userData);
    /** @brief Set the optional input callback used by recording flows. */
    void setInputCallback(InputCallback callback, void* userData) {
        m_inputCallback.store(callback);
        m_inputCallbackData.store(userData);
    }

    /** @brief Access the command queue used to communicate with the audio thread. */
    AudioCommandQueue& commandQueue() { return m_commandQueue; }
    /** @brief Access the engine telemetry collector. */
    AudioTelemetry& telemetry() { return m_telemetry; }
    /** @brief Access the mutable engine state object. */
    EngineState& engineState() { return m_state; }
    /** @brief Non-RT diagnostic counter incremented whenever a graph is published. */
    uint64_t graphGeneration() const { return m_graphGeneration.load(std::memory_order_relaxed); }

    /** @brief Set the active sample rate used by the engine. */
    void setSampleRate(uint32_t sampleRate) {
        m_sampleRate.store(sampleRate, std::memory_order_relaxed);
        if (m_channelPrepareConfig) {
            m_channelPrepareConfig->sampleRate.store(sampleRate, std::memory_order_relaxed);
        }
        // True-peak meter is sample-rate agnostic (4x oversampler) but we
        // still publish the rate for diagnostics and reset peak history so
        // stale peaks don't carry across sample-rate changes.
        m_truePeakMeter.initialize(sampleRate);
        m_truePeakLAtomic.store(0.0f, std::memory_order_relaxed);
        m_truePeakRAtomic.store(0.0f, std::memory_order_relaxed);

        // Recompute LUFS K-weighting coefficients for new sample rate.
        // Write into the inactive slot, then publish by flipping the index.
        const uint32_t active = m_activeKWeightIndex.load(std::memory_order_relaxed);
        const uint32_t inactive = 1u - active;
        m_kWeightPreFilterSlots[inactive] = computeKWeightPreFilter(static_cast<double>(sampleRate));
        m_kWeightRlbSlots[inactive] = computeKWeightRLB(static_cast<double>(sampleRate));
        m_activeKWeightIndex.store(inactive, std::memory_order_release);
    }
    /** @brief Get the active sample rate used by the engine. */
    uint32_t getSampleRate() const { return m_sampleRate.load(std::memory_order_relaxed); }

    /** @brief Check if a routing cycle was detected on the audio thread (poll from UI). */
    bool hasRoutingCycleDetected() const { return m_loggedRoutingCycleWarning.load(std::memory_order_relaxed); }

    /** @brief Configure the maximum buffer and output-channel counts. */
    void setBufferConfig(uint32_t maxFrames, uint32_t numChannels);
    /** @brief Set transport running state and mirror it onto the audio command queue. */
    void setTransportPlaying(bool playing) {
        // Update immediately for UI queries, but also enqueue a command so the audio thread
        // can detect edges reliably (stop->play within one buffer, double-stop hard stop, etc.).
        m_transportPlaying.exchange(playing, std::memory_order_relaxed);
        uint64_t pos = m_globalSamplePos.load(std::memory_order_relaxed);

        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = playing ? 1.0f : 0.0f;
        cmd.samplePos = pos;
        m_commandQueue.push(cmd);
    }
    /** @brief Check whether transport playback is active. */
    bool isTransportPlaying() const { return m_transportPlaying.load(std::memory_order_relaxed); }
    /** @brief Replace the active audio graph and compile it for rendering. */
    void setGraph(const AudioGraph& graph) {
        auto preparedGraph = graph;
        finalizeAudioGraphRouting(preparedGraph);
        prepareTrackStateForGraph(preparedGraph);
        m_state.swapGraph(preparedGraph);
        compileGraph();
        m_graphGeneration.fetch_add(1, std::memory_order_release);
    }

    /** @brief Publish the shared meter snapshot buffer used by the UI. Non-RT only. */
    void setMeterSnapshots(std::shared_ptr<MeterSnapshotBuffer> snapshots);

    /** @brief Publish the continuous parameter buffer used for automation. Non-RT only. */
    void setContinuousParams(std::shared_ptr<ContinuousParamBuffer> params);

    /** @brief Publish the channel-slot map used by the audio thread. Non-RT only. */
    void setChannelSlotMap(std::shared_ptr<const ChannelSlotMap> slotMap);

    /** @brief Get the current transport position in samples. */
    uint64_t getGlobalSamplePos() const { return m_globalSamplePos.load(std::memory_order_relaxed); }
    /** @brief Set the current transport position in samples. */
    void setGlobalSamplePos(uint64_t pos) { m_globalSamplePos.store(pos, std::memory_order_relaxed); }
    /** @brief Get the current transport position in seconds. */
    double getPositionSeconds() const {
        uint32_t sr = m_sampleRate.load(std::memory_order_relaxed);
        return sr > 0 ? static_cast<double>(m_globalSamplePos.load(std::memory_order_relaxed)) / sr : 0.0;
    }

    /** @brief Set the active interpolation quality. */
    void setInterpolationQuality(Interpolators::InterpolationQuality q) {
        m_interpQuality.store(q, std::memory_order_relaxed);
    }
    /** @brief Get the active interpolation quality. */
    Interpolators::InterpolationQuality getInterpolationQuality() const {
        return m_interpQuality.load(std::memory_order_relaxed);
    }

    /** @brief Set the master gain target. */
    void setMasterGain(float gain) { m_masterGainTarget.store(gain, std::memory_order_relaxed); }
    /** @brief Get the master gain target. */
    float getMasterGain() const { return m_masterGainTarget.load(std::memory_order_relaxed); }

    /** @brief Get count of NaN/Inf samples sanitized since last reset. */
    uint64_t getNaNCount() const { return m_nanCount.load(std::memory_order_relaxed); }
    /** @brief Get count of samples hard-clipped since last reset. */
    uint64_t getClipCount() const { return m_clipCount.load(std::memory_order_relaxed); }
    /** @brief Reset NaN and clip counters. */
    void resetSignalCounters() {
        m_nanCount.store(0, std::memory_order_relaxed);
        m_clipCount.store(0, std::memory_order_relaxed);
    }

    // === Plugin Delay Compensation ===

    /**
     * @brief Calculate and apply plugin delay compensation across all tracks
     *
     * Computes max latency, sets compensation delays, and publishes new snapshot.
     * NOT RT-SAFE: Call from main thread only.
     *
     * Triggers:
     * - Plugin load/unload
     * - Plugin bypass toggle
     * - Effect chain reorder
     * - Track enable/disable
     */
    void calculateLatencyCompensation();

    /**
     * @brief Enable/disable global latency compensation
     */
    void setLatencyCompensationEnabled(bool enabled);
    bool isLatencyCompensationEnabled() const;

    /**
     * @brief Get current max project latency in samples
     */
    uint32_t getMaxProjectLatency() const;

    /**
     * @brief Access the most recently solved PDC topology.
     *
     * Returns a snapshot of the artifact produced by `solveLatency()` during the
     * last `calculateLatencyCompensation()` call. NOT RT-SAFE: read from main /
     * control thread only.
     *
     * Architectural contract (PDC-v2-Design §4.0): the engine consumes this
     * topology read-only. Tests and tools may inspect it for diagnostics and
     * solver-vs-state equivalence checks.
     */
    SolvedLatencyTopology getLastSolvedLatencyTopology() const;

    /**
     * @brief PDC v2 (P4b.2): per-track per-edge compensation snapshot for tests
     *        and tooling. NOT RT-SAFE: read from main / control thread only.
     *
     * Returns the node latency/compensation values and per-edge delay state
     * that the off-RT apply pass wrote into TrackRTState for the given track
     * index. The buffer contents themselves are not exposed; only the values
     * the RT-side P4b.3 consumer will read.
     */
    struct TrackEdgeDelaySnapshot {
        struct EdgeSlotSnapshot {
            uint32_t compensationSamples{0};
            uint32_t capacityMask{0};
            size_t bufferBytes{0};
            /// RT-side write cursor (frames). Increments only when the RT path
            /// applies this edge's delay (i.e., comp > 0 and buffer is ready).
            uint32_t writePos{0};
        };
        uint32_t pluginLatencySamples{0};
        uint32_t outputCompensationSamples{0};
        /// Engine-wide latency compensation toggle. Per-track enablement is not
        /// implemented, so this is the same value for every track index.
        bool compensationEnabled{false};
        EdgeSlotSnapshot mainOutEdgeDelay;
        std::vector<EdgeSlotSnapshot> sendEdgeDelays;
        bool valid{false};
    };
    TrackEdgeDelaySnapshot getTrackEdgeDelaySnapshot(size_t trackIndex) const;

    /**
     * @brief Whether the published latency topology is pending recalculation.
     *
     * NOT RT-SAFE: read from the main / control thread only. Diagnostic
     * tooling uses this to distinguish a current solution from a stale one;
     * reading it never triggers recalculation.
     */
    bool isLatencyRecalculationPending() const { return m_latencyDirty; }

    /**
     * @brief Mark latency as dirty (needs recalculation)
     * Called internally when plugins change
     */
    void markLatencyDirty();

    /** @brief Set global output headroom in decibels. */
    void setHeadroom(float db) { m_headroomLinear.store(std::pow(10.0f, db / 20.0f), std::memory_order_relaxed); }
    /** @brief Enable or disable the master safety limiter. Default: on. */
    void setSafetyLimiterEnabled(bool enabled) { m_safetyLimiterEnabled.store(enabled, std::memory_order_relaxed); }
    /** @brief Check whether the master safety limiter is enabled. */
    bool isSafetyLimiterEnabled() const { return m_safetyLimiterEnabled.load(std::memory_order_relaxed); }

    /** @brief Enable or disable master-bus DC removal (one-pole DC blocker). Default: off. */
    void setDCRemovalEnabled(bool enabled) { m_dcRemovalEnabled.store(enabled, std::memory_order_relaxed); }
    /** @brief Check whether master-bus DC removal is enabled. */
    bool isDCRemovalEnabled() const { return m_dcRemovalEnabled.load(std::memory_order_relaxed); }

    /** @brief Enable or disable the metronome. */
    void setMetronomeEnabled(bool enabled) { m_metronomeEngine.setEnabled(enabled); }
    /** @brief Check whether the metronome is enabled. */
    bool isMetronomeEnabled() const { return m_metronomeEngine.isEnabled(); }
    /** @brief Set metronome output volume. */
    void setMetronomeVolume(float vol) { m_metronomeEngine.setVolume(vol); }
    /** @brief Set metronome beats-per-bar (time-signature numerator). */
    void setMetronomeBeatsPerBar(int beats) { m_metronomeEngine.setBeatsPerBar(beats); }
    /** @brief Get metronome output volume. */
    float getMetronomeVolume() const { return m_metronomeEngine.getVolume(); }
    /** @brief Set transport tempo in beats per minute. */
    void setBPM(float bpm) { m_metronomeEngine.setBPM(bpm); }
    /** @brief Get transport tempo in beats per minute. */
    float getBPM() const { return m_metronomeEngine.getBPM(); }
    /** @brief Set the time-signature numerator used by the metronome. */
    void setBeatsPerBar(int beats) { m_metronomeEngine.setBeatsPerBar(beats); }
    /** @brief Get the time-signature numerator used by the metronome. */
    int getBeatsPerBar() const { return m_metronomeEngine.getBeatsPerBar(); }
    /** @brief Load downbeat and upbeat click samples for the metronome. */
    void loadMetronomeClicks(const std::string& downbeatPath, const std::string& upbeatPath) {
        if (isTransportPlaying()) {
            // Avoid I/O during playback to prevent dropouts
            return;
        }
        m_metronomeEngine.loadClickSounds(downbeatPath, upbeatPath);
    }
    void startMetronomeCountIn(uint32_t beats);
    void stopMetronomeCountIn();
    bool isMetronomeCountInActive() const { return m_metronomeCountInActive.load(std::memory_order_relaxed); }

    /** @brief Enable or disable transport looping. */
    void setLoopEnabled(bool enabled) { m_loopEnabled.store(enabled, std::memory_order_relaxed); }
    /** @brief Check whether transport looping is enabled. */
    bool isLoopEnabled() const { return m_loopEnabled.load(std::memory_order_relaxed); }
    /** @brief Set the loop region in beats. */
    void setLoopRegion(double startBeat, double endBeat);
    /** @brief Get the loop start position in beats. */
    double getLoopStartBeat() const { return m_loopStartBeat.load(std::memory_order_relaxed); }
    /** @brief Get the loop end position in beats. */
    double getLoopEndBeat() const { return m_loopEndBeat.load(std::memory_order_relaxed); }

    /**
     * @brief Post a live MIDI event (note input) for an Arsenal unit.
     *
     * Lock-free, allocation-free; safe to call from exactly ONE non-RT
     * producer thread (the UI thread today — a hardware MIDI callback thread
     * will get its own queue). Events are drained on the audio thread each
     * block and delivered to the unit's plugin alongside pattern playback,
     * with or without the transport running.
     *
     * @return false if the queue was full and the event was dropped.
     */
    bool postLiveMidiEvent(uint64_t unitId, uint8_t status, uint8_t data1, uint8_t data2) noexcept {
        return m_liveMidiQueue.push(LiveMidiQueue::Event{unitId, status, data1, data2});
    }

    /**
     * @brief Post a live MIDI event from the hardware MIDI input thread.
     *
     * Same contract as postLiveMidiEvent, but on a separate SPSC queue whose
     * single producer is the hardware MIDI callback thread (RtMidi). Both
     * queues are drained together on the audio thread each block.
     */
    bool postHardwareMidiEvent(uint64_t unitId, uint8_t status, uint8_t data1, uint8_t data2) noexcept {
        return m_hardwareMidiQueue.push(LiveMidiQueue::Event{unitId, status, data1, data2});
    }

    /** @brief Enable or disable Arsenal pattern playback mode. */
    void setPatternPlaybackMode(bool enabled, double lengthBeats);
    /** @brief Check whether Arsenal pattern playback mode is active. */
    bool isPatternPlaybackMode() const { return m_patternPlaybackMode.load(std::memory_order_relaxed); }

    /** @brief Bind the unit manager used for Arsenal rendering. */
    void setUnitManager(UnitManager* mgr) {
        // Build complete snapshot (manager + cache) before publishing
        // so audio thread never sees mixed state
        auto* snapshot = new UnitManagerSnapshot();
        snapshot->manager = mgr;
        if (mgr) {
            refreshSamplerCacheToSnapshot(*mgr, snapshot->cache);
        }
        // Atomically publish the complete snapshot
        auto* old = m_unitManagerSnapshot.exchange(snapshot, std::memory_order_release);
        delete old;
        // Also update legacy fields for backward compatibility during transition
        m_unitManager.store(mgr, std::memory_order_release);
        auto cache = snapshot->cache;
        m_cachedSamplers = cache.samplers;
        m_cachedSamplerOwners = cache.owners;
        m_cachedSamplerCount.store(cache.count, std::memory_order_release);

        // Update new thread-safe cache as well
        auto newCache = std::make_shared<SamplerCacheData>();
        if (mgr) {
            refreshSamplerCacheToSnapshot(*mgr, *newCache);
        }
        auto retired = std::move(m_samplerCacheOwned);
        m_samplerCacheOwned = std::move(newCache);
        m_samplerCacheRaw.store(m_samplerCacheOwned.get(), std::memory_order_release);
        GarbageCollector::instance().release(std::move(retired), "AudioEngine::SamplerCache");
    }

    // Internal helper: populate a cache snapshot from a specific UnitManager
    void refreshSamplerCacheToSnapshot(UnitManager& mgr, SamplerCacheData& cache);
    void refreshSamplerCacheToSnapshot(UnitManager& mgr, SamplerCacheSnapshot& snapshot); // Legacy compatibility
    /** @brief Bind the pattern playback engine used for scheduled MIDI. */
    void setPatternPlaybackEngine(PatternPlaybackEngine* engine) {
        m_patternEngine.store(engine, std::memory_order_release);
    }

    /** @brief Request a voice reset after a pattern identity change. */
    void requestVoiceResetOnPatternChange();

    /** @brief Get the latest left peak meter value. */
    float getPeakL() const { return m_peakL.load(std::memory_order_relaxed); }
    /** @brief Get the latest right peak meter value. */
    float getPeakR() const { return m_peakR.load(std::memory_order_relaxed); }
    /** @brief Get the latest left RMS meter value. */
    float getRmsL() const { return m_rmsL.load(std::memory_order_relaxed); }
    /** @brief Get the latest right RMS meter value. */
    float getRmsR() const { return m_rmsR.load(std::memory_order_relaxed); }

    // === True Peak Metering (Phase 2 — ITU-R BS.1770-4 inspired) ===
    /** @brief Latest left-channel true peak (linear, max-hold since last clear). */
    float getTruePeakL() const { return m_truePeakLAtomic.load(std::memory_order_relaxed); }
    /** @brief Latest right-channel true peak (linear, max-hold since last clear). */
    float getTruePeakR() const { return m_truePeakRAtomic.load(std::memory_order_relaxed); }
    /** @brief Latest max-channel true peak in dBTP (decibels True Peak). */
    float getMaxTruePeakdBTP() const {
        const float l = getTruePeakL();
        const float r = getTruePeakR();
        return TruePeakMeter::linearToDbTp(l > r ? l : r);
    }
    /** @brief Enable or disable true-peak metering on the master output (RT-safe). */
    void setTruePeakMeteringEnabled(bool enabled) {
        m_truePeakMeteringEnabled.store(enabled, std::memory_order_relaxed);
    }
    /** @brief Whether true-peak metering is currently enabled. */
    bool isTruePeakMeteringEnabled() const { return m_truePeakMeteringEnabled.load(std::memory_order_relaxed); }
    /** @brief Reset true-peak running max (does not disturb FIR history). */
    void clearTruePeakHold() {
        m_truePeakMeter.clearPeaks();
        m_truePeakLAtomic.store(0.0f, std::memory_order_relaxed);
        m_truePeakRAtomic.store(0.0f, std::memory_order_relaxed);
    }

    /** @brief Set the active dithering mode. */
    void setDitheringMode(DitheringMode mode) { m_ditheringMode.store(mode, std::memory_order_relaxed); }
    /** @brief Get the active dithering mode. */
    DitheringMode getDitheringMode() const { return m_ditheringMode.load(std::memory_order_relaxed); }

    /** @brief Enable or disable the internal test tone. */
    void setTestToneEnabled(bool enabled) { m_testToneEnabled.store(enabled, std::memory_order_relaxed); }
    /** @brief Check whether the internal test tone is enabled. */
    bool isTestToneEnabled() const { return m_testToneEnabled.load(std::memory_order_relaxed); }
    /** @brief Test hook: force bounce write error after one successful full-block write. */
    void setForceBounceWriteErrorForTests(bool enabled) {
        m_forceBounceWriteErrorForTests.store(enabled, std::memory_order_relaxed);
    }
    /** @brief Test hook: indicates whether the last bounce wrote at least one full block. */
    bool didLastBounceWriteAnyFramesForTests() const {
        return m_lastBounceWroteAnyFramesForTests.load(std::memory_order_relaxed);
    }

    /** @brief Get the waveform-history buffer capacity in frames. */
    uint32_t getWaveformHistoryCapacity() const { return m_waveformHistoryFrames.load(std::memory_order_relaxed); }
    /** @brief Copy recent waveform history into an interleaved output buffer. */
    uint32_t copyWaveformHistory(float* outInterleaved, uint32_t maxFrames) const;
    /** @brief Capture an interleaved output block into waveform history. */
    void captureWaveformHistory(const float* interleavedOutput, uint32_t numFrames);

    /** @brief Set the worker-thread count used by the engine. */
    void setThreadCount(int count);
    /** @brief Enable or disable multithreaded processing. */
    void setMultiThreadingEnabled(bool enabled) { m_multiThreadingEnabled.store(enabled, std::memory_order_relaxed); }
    /** @brief Check whether multithreaded processing is enabled. */
    bool isMultiThreadingEnabled() const { return m_multiThreadingEnabled.load(std::memory_order_relaxed); }

    /** @brief Bind the audition engine used by audition mode. */
    void setAuditionEngine(AuditionEngine* engine) { m_auditionEngine.store(engine, std::memory_order_relaxed); }
    /** @brief Enable or disable audition mode. */
    void setAuditionModeEnabled(bool enabled) { m_auditionModeEnabled.store(enabled, std::memory_order_relaxed); }
    /** @brief Check whether audition mode is enabled. */
    bool isAuditionModeEnabled() const { return m_auditionModeEnabled.load(std::memory_order_relaxed); }

    /** @brief Bind the preview engine mixed into the main output. */
    void setPreviewEngine(PreviewEngine* engine) { m_previewEngine.store(engine, std::memory_order_relaxed); }

    /** @brief Source currently responsible for transport ducking. */
    enum class PreviewDuckSource : uint8_t {
        None = 0,
        BrowserPreview = 1,
        Audition = 2,
        ArsenalPreview = 3,
    };

    /** @brief Configure preview duck depth in positive dB; 0 disables ducking. Non-RT only. */
    void setPreviewDuckingAttenuationDb(float attenuationDb);
    /** @brief Configured preview duck depth in positive dB; 0 means disabled. */
    float getPreviewDuckingAttenuationDb() const;
    /** @brief Check whether preview ducking is enabled. */
    bool isPreviewDuckingEnabled() const;
    /** @brief Current transport duck gain caused by audible preview playback. */
    float getPreviewDuckGain() const { return m_previewDuckGain.load(std::memory_order_relaxed); }
    /** @brief Source currently responsible for the published preview duck gain. */
    PreviewDuckSource getPreviewDuckSource() const {
        return static_cast<PreviewDuckSource>(m_previewDuckSource.load(std::memory_order_relaxed));
    }

    /**
     * @brief Snap preview-duck smoothing to unity. Offline render only.
     *
     * Preview ducking is a monitoring convenience; it must never attenuate an
     * offline render. AudioExporter disables the duck depth for the render,
     * and calls this so an already-engaged duck's release tail (~120 ms)
     * cannot bleed into the head of the exported file. Non-RT only — call
     * while no realtime stream is driving processBlock.
     */
    void resetPreviewDuckForOfflineRender() {
        m_smoothedPreviewDuckGain = 1.0f;
        m_previewDuckHoldSecondsRemaining = 0.0f;
        m_previewDuckGain.store(1.0f, std::memory_order_relaxed);
        m_previewDuckSource.store(static_cast<uint8_t>(PreviewDuckSource::None), std::memory_order_relaxed);
    }

    /**
     * @brief Render a range of the timeline (or a specific track) to a WAV file.
     *
     * Uses miniaudio encoder and AudioRenderer for offline rendering.
     * @param startBeat Start position in beats
     * @param endBeat End position in beats
     * @param outputPath Path to save the WAV file
     * @param trackId Specific track ID to bounce, or -1 for Master output
     * @return true if successful
     */
    bool bounceRangeToWav(double startBeat, double endBeat, const std::string& outputPath, int32_t trackId = -1);

    // Input callback state
    std::atomic<InputCallback> m_inputCallback{nullptr};
    std::atomic<void*> m_inputCallbackData{nullptr};

    // ============================================================================
    // HEADLESS / OFFLINE RENDERING API
    // ============================================================================

    /**
     * @brief Set the TrackManager for playlist rendering
     * @param trackManager Shared pointer to track manager
     */
    void setTrackManager(std::shared_ptr<TrackManager> trackManager) {
        if (auto previous = m_trackManager.lock()) {
            previous->setChannelPrepareCallback(nullptr);
        }
        m_trackManager = std::move(trackManager);
        if (auto current = m_trackManager.lock()) {
            auto prepareConfig = m_channelPrepareConfig;
            auto prepareChannel = [prepareConfig](MixerChannel& channel) {
                if (!prepareConfig) {
                    return;
                }
                const uint32_t maxBlockSize = prepareConfig->maxBlockSize.load(std::memory_order_relaxed);
                if (maxBlockSize == 0) {
                    return;
                }
                const double sampleRate =
                    static_cast<double>(prepareConfig->sampleRate.load(std::memory_order_relaxed));
                channel.prepareProcessingBuffers(maxBlockSize);
                channel.getEffectChain().prepare(sampleRate, maxBlockSize);
            };
            current->setChannelPrepareCallback(prepareChannel);
            const size_t channelCount = current->getChannelCount();
            for (size_t i = 0; i < channelCount; ++i) {
                if (auto* channel = current->getChannel(i)) {
                    prepareChannel(*channel);
                    channel->setEffectChainLatencyCallback([this]() { calculateLatencyCompensation(); });
                }
            }
            calculateLatencyCompensation();
        }
    }

    /**
     * @brief Get the current TrackManager
     * @return Shared pointer to track manager (may be null)
     */
    std::shared_ptr<TrackManager> getTrackManager() const { return m_trackManager.lock(); }

    /**
     * @brief Initialize the engine for rendering
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Set the playhead position in beats
     * @param beat Beat position (0.0 = start)
     */
    void setPlayhead(double beat) {
        uint32_t sr = m_sampleRate.load(std::memory_order_relaxed);
        double bpm = getBPM();
        if (sr > 0 && bpm > 0) {
            double secondsPerBeat = 60.0 / bpm;
            double seconds = beat * secondsPerBeat;
            uint64_t samples = static_cast<uint64_t>(seconds * sr);
            setGlobalSamplePos(samples);
        }
    }

    /**
     * @brief Get the current playhead position in beats
     * @return Current beat position
     */
    double getPlayhead() const {
        uint32_t sr = m_sampleRate.load(std::memory_order_relaxed);
        double bpm = getBPM();
        if (sr > 0 && bpm > 0) {
            double secondsPerBeat = 60.0 / bpm;
            uint64_t samples = getGlobalSamplePos();
            double seconds = static_cast<double>(samples) / sr;
            return seconds / secondsPerBeat;
        }
        return 0.0;
    }

private:
    struct ChannelPrepareConfig {
        std::atomic<uint32_t> sampleRate{48000};
        std::atomic<uint32_t> maxBlockSize{4096};
    };

    static constexpr size_t kMaxTracks = 4096;
    static constexpr size_t kMaxSendsPerTrack = 256; // Matches PROJECT_MAX_SENDS_PER_LANE
    static constexpr size_t kMaxEdgesPerTrack = 16;  // Conservative max routing edges per track
    static constexpr uint32_t kWaveformHistoryFramesDefault = 2048;
    static constexpr uint32_t kInternalRenderChannels = 2;

    // Fast Xorshift32 RNG for dither
    struct FastRNG {
        uint32_t state = 2463534242;
        inline uint32_t next() {
            uint32_t x = state;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            state = x;
            return x;
        }
        inline void setSeed(uint32_t seed) {
            state = (seed == 0) ? 0xDEADBEEF : seed; // Avoid 0 state for Xorshift
        }
        // Returns uniform float [0, 1)
        inline float nextFloat() { return (next() & 0xFFFFFF) * (1.0f / 16777216.0f); }
    };
    std::atomic<DitheringMode> m_ditheringMode{DitheringMode::Triangular}; // Default TPDF

    TrackRTState& ensureTrackState(uint32_t trackId);
    void renderGraph(const AudioGraph& graph, uint32_t numFrames, uint32_t bufferOffset, uint64_t patternFrameStart);
    // Block-stable values renderGraph() derives once per call and every
    // per-track render reads. Bundled so renderTrack() takes them as one
    // argument instead of re-loading atomics per track.
    struct RenderContext {
        uint32_t numFrames;
        double* masterBuf;
        size_t availableTracks;
        uint64_t blockStart;
        uint64_t blockEnd;
        bool isPlaying;
        bool anySolo;
        bool anyUnitSolo;
        uint32_t cachedSampleRate;
        const ChannelSlotMap* cachedSlotMap;
        ContinuousParamBuffer* cachedParams;
        MeterSnapshotBuffer* cachedSnaps;
        bool cachedPatternMode;
        Interpolators::InterpolationQuality cachedInterpQuality;
        const AudioArsenalSnapshot* unitSnapshot;
        const PatternPlaybackEngine::UnitMidiRoute* unitMidiRoutes;
        size_t unitMidiRouteCount;
    };
    // Renders one track of graph.topologicalOrder (audio thread only):
    // params/automation, clips, unit render, effect chain, PDC, routing to
    // master/destination/sends, meter publication. Verbatim extraction of the
    // renderGraph() per-track loop body; srcActiveThisBlock is the loop-carried
    // "any resampling happened" telemetry flag.
    // renderTrack phase helpers (verbatim extractions; RT path — no allocation).
    void renderClips(const std::vector<ClipRenderState>& clips, double* destination, const RenderContext& ctx,
                     bool& srcActiveThisBlock);
    void renderTrackUnits(uint32_t mixerChannelId, std::vector<double>& buffer, const RenderContext& ctx);
    float processTrackEffects(const TrackRenderState& track, uint32_t trackIdx, std::vector<double>& buffer,
                              uint32_t numFrames);
    void mixAndMeterTrack(const TrackRenderState& track, uint32_t trackIdx, uint32_t slot, TrackRTState& state,
                          std::vector<double>& buffer, const RenderContext& ctx, double volTarget, double panTarget,
                          float trackSidechainPeak, bool muted, bool audibleEligible);
    void renderTrack(const AudioGraph& graph, size_t orderedIndex, const RenderContext& ctx, bool& srcActiveThisBlock);
    void prepareTrackStateForGraph(const AudioGraph& graph);
    void applyPendingCommands();
    void applyPendingMetronomeCountInRt();
    void clearMetronomeCountInRt();
    void processArsenalUnits(uint32_t numFrames, uint32_t bufferOffset, uint64_t startFrame,
                             double* targetBuffer = nullptr, int64_t isolatedMixerChannelId = -1);
    // processBlock leaf phases (audio thread only). Each is a verbatim
    // extraction of a self-contained section of processBlock; state lives in
    // the members they always used.
    void mixTestTone(uint32_t numFrames, uint32_t currentSampleRate);
    // Per-block preview duck-gain ramp. The master output stage interpolates duck
    // gain from `start` (the previous block's end value) to `end` (this block's
    // smoothed target) once per sample — mirroring the master fader's per-sample
    // ramp — so the 50ms/120ms fade never steps at block boundaries (zipper noise).
    struct PreviewDuckRamp {
        double start = 1.0;
        double end = 1.0;
    };
    // Advances the smoothed duck gain one block and returns its start/end for the ramp.
    PreviewDuckRamp computePreviewDuckGain(uint32_t numFrames, uint32_t currentSampleRate, bool isPlaying);
    void mixMetronomeClicks(float* outputBuffer, uint32_t numFrames);
    void updateTruePeakMeters(const float* outputBuffer, uint32_t numFrames, uint32_t numOutputChannels);
    void resetCachedSamplerVoicesRt() noexcept;
    void syncCachedSamplerSampleRatesRt(uint32_t sampleRate) noexcept;
    void injectPendingUnitAudition(PatternPlaybackEngine::UnitMidiRoute* routes, size_t routeCount,
                                   uint32_t numFrames) noexcept;
    void drainLiveMidi(PatternPlaybackEngine::UnitMidiRoute* routes, size_t routeCount) noexcept;

    // Live note input. One SPSC queue per producer thread — UI keyboard and
    // the hardware MIDI callback thread — both drained on the audio thread
    // each block by drainLiveMidi().
    LiveMidiQueue m_liveMidiQueue;
    LiveMidiQueue m_hardwareMidiQueue;

    // Pre-allocated buffers for Arsenal unit processing (RT-safe)
    std::vector<double> m_unitBufferD;         // Stereo interleaved unit output
    std::vector<MidiBuffer> m_unitMidiBuffers; // Per-unit MIDI buffers (max 32 units)
    std::vector<float> m_pluginBufferF;        // Stereo de-interleaved float buffer for plugin processing
    std::vector<float> m_silentBufferF;        // Zero buffer for inputs

    // Soft clipper (transparent below unity)
    static inline double softClipD(double x) {
        if (x > 1.5)
            return 1.0;
        if (x < -1.5)
            return -1.0;
        const double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }

    // DC blocker (double precision) — shared one-pole, see DSP/DCBlocker.h
    using DCBlockerD = DCBlocker;

    AudioCommandQueue m_commandQueue;
    AudioTelemetry m_telemetry;
    EngineState m_state;

    std::atomic<uint32_t> m_sampleRate{48000};
    std::atomic<uint32_t> m_maxBufferFrames{4096}; // Larger default for safety
    std::shared_ptr<ChannelPrepareConfig> m_channelPrepareConfig{std::make_shared<ChannelPrepareConfig>()};
    std::atomic<uint32_t> m_outputChannels{2};
    std::atomic<bool> m_transportPlaying{false};
    // RT-side tracking of last known transport state (avoids race with UI atomic updates)
    bool m_rtLastTransportPlaying{false};
    // Transport edge flags (set in applyPendingCommands, consumed in processBlock)
    std::atomic<bool> m_transportRestartRequested{false};
    std::atomic<bool> m_transportStopRequested{false};
    // Hard stop: immediate silence (e.g., stop pressed twice)
    std::atomic<bool> m_transportHardStopRequested{false};
    std::atomic<bool> m_metronomeCountInActive{false};
    std::atomic<uint64_t> m_metronomeCountInRemainingSamples{0};
    std::atomic<uint64_t> m_metronomeCountInSamplePos{0};
    std::atomic<uint32_t> m_pendingMetronomeCountInBeats{0};
    std::atomic<bool> m_pendingMetronomeCountInStop{false};

    // Request MIDI panic (All Notes Off / All Sound Off) injection into unit MIDI buffers.
    std::atomic<bool> m_transportMidiPanicRequested{false};
    std::atomic<uint64_t> m_globalSamplePos{0};

    // Signal integrity counters
    std::atomic<uint64_t> m_nanCount{0};
    std::atomic<uint64_t> m_clipCount{0};

    // Pre-allocated buffers - DOUBLE PRECISION for internal mixing
    std::vector<std::vector<double>> m_trackBuffersD;          // Double precision track buffers
    std::vector<std::vector<double>> m_trackSidechainBuffersD; // Sidechain-only per-track buffers
    std::vector<double> m_masterBufferD;                       // Double precision master
    std::vector<float> m_scratchL;                             // Scratch L for plugins
    std::vector<float> m_scratchR;                             // Scratch R for plugins
    std::vector<float> m_sidechainScratchL;                    // Scratch sidechain L for plugins
    std::vector<float> m_sidechainScratchR;                    // Scratch sidechain R for plugins
    std::vector<float> m_dryBuffer;                            // Dry buffer for EffectChainSnapshot dry/wet mixing
    std::vector<MidiBuffer> m_scratchMidiBuffers;              // [NEW] Scratch MIDI buffers for units

    struct UnitAuditionState {
        UnitID unitId{0};
        uint8_t note{48}; // [FIX] C3 (MIDI 48) — canonical default octave for melodic instruments
        uint8_t velocity{100};
        uint32_t noteOffSamplesRemaining{0};
        bool noteOnPending{false};
        bool active{false};
    };
    // Small slot pool so a chord stamp auditions every pitch, not just the
    // root. Slots are claimed per AuditionUnit command (free-first, else the
    // one closest to its note-off) — all touched on the RT thread only.
    static constexpr size_t kUnitAuditionSlots = 4;
    std::array<UnitAuditionState, kUnitAuditionSlots> m_unitAuditionStates;
    // std::vector<TrackRTState> m_trackState; <-- Moved to m_rtGraphState (actually m_graphStates)
    // But we need persistent state across swaps?
    // TrackRTState contains current Volume/Pan/SmoothedParams.
    // If we move it to AudioGraphState, and we double buffer AudioGraphState,
    // we have SPLIT state. (Frame A has Vol=0.5, Frame B has Vol=0.5).
    // This is GOOD for threading, but bad for persistence if we don't sync them.
    // For now, let's keep m_trackState in AudioEngine as the "Golden State" and just reference it?
    // AudioGraphState has "std::vector<TrackRTState> trackStates".
    // If we duplicate it, we duplicate the SmoothedParams.
    // This implies we need to update BOTH or sync them.

    // DECISION: To avoid complexity in Phase 1, let's KEEP m_trackState in AudioEngine
    // and let AudioGraphState just REFERENCE it via index (which it does).
    // AudioGraphState definition has: std::vector<TrackRTState> trackStates;
    // We should probably NOT include trackStates in AudioGraphState yet if we want a shared state model.
    // OR we move m_trackState entirely to the GraphState and ensure we copy it on swap.

    // Let's modify AudioGraphState.h to NOT own the state yet?
    // No, Hybrid Engine requires OWNED state for background thread.
    // So AudioGraphState MUST own it.

    // Implementation: AudioEngine holds m_trackState (The "Master" State).
    // AudioRenderer uses `state.trackStates`.
    // We must populate `state.trackStates` from `AudioEngine::m_trackState` during compile/update?
    // Or just use AudioEngine's vector for now?

    // Let's use AudioEngine's vector for Phase 1.
    // I will COMMENT OUT the vector in AudioGraphState.h (mentally) or ignore it,
    // and have AudioEngine keep m_trackState.
    // Wait, I define AudioGraphState to have it.
    // Let's use AudioEngine::m_trackState.
    std::vector<TrackRTState> m_trackState;

    // Reused render-graph scratch state to avoid heap churn in the audio callback.
    // All pre-allocated in setBufferConfig() to kMaxTracks capacity.
    // RT path uses index-based writes and .clear() (which preserves capacity).

    // Flat vector replacing unordered_map<uint32_t, size_t>.
    // Indexed by trackId; value is graph index, or kMaxTracks sentinel for "not present".
    // Safe because trackId is bounded by kMaxTracks (enforced by ChannelSlotMap).
    std::vector<size_t> m_rtTrackIndexById;
    size_t m_rtTrackIndexByIdActiveCount{0}; // Number of valid entries written this block

    std::vector<std::vector<size_t>> m_rtAudibleDownstream;
    std::vector<std::vector<size_t>> m_rtAudibleIncoming;
    std::vector<std::vector<size_t>> m_rtSidechainIncoming;
    std::vector<uint8_t>
        m_rtSidechainReceiverFlags; // Indexed by trackIndex: 1 if track received sidechain input last block
    std::vector<std::vector<size_t>> m_rtTopoEdges;
    std::vector<uint32_t> m_rtTopoIndegree;
    std::vector<bool> m_rtAudibleEligible;
    std::vector<bool> m_rtProcessActive;
    std::vector<size_t> m_rtProcessOrder;
    std::vector<size_t> m_rtIndexQueue;
    std::vector<size_t> m_rtSoloProcessQueue;
    std::vector<bool> m_rtCycleVisited;

    // Scratch buffer for send routing in renderGraph (pre-allocated, avoids local std::vector).
    struct PreparedSendRoute {
        const double* source{nullptr};
        double* dest{nullptr};
        SmoothedParamD* gainL{nullptr};
        SmoothedParamD* gainR{nullptr};
        /// PDC v2 (P4b.3): per-edge compensation slot for this send. Optional —
        /// nullptr or comp==0 means "no delay, mix directly".
        EdgeDelayState* edgeDelay{nullptr};
    };
    std::vector<PreparedSendRoute> m_preparedRoutesScratch;

    // Tracks last sample rate synced to Arsenal units (avoids per-block scans).
    // Replaces static local in processArsenalUnits for RT safety.
    uint32_t m_lastSyncedArsenalSampleRate{0};

    // --- Antigravity Routing Engine (v3.1) ---
    // Moved struct definitions to AudioGraphState.h

    // [HYBRID ENGINE] State & Renderer
    AudioGraphState m_rtGraphState; // The Real-Time Graph State
    // AudioGraphState m_bgGraphState;      // The Background Graph State (Future)

    AudioRenderer m_rtRenderer; // The Real-Time Renderer
    std::atomic<uint64_t> m_graphGeneration{0};

    // Legacy support for AudioEngine::compileGraph populating the state
    // We map m_renderTracks logic to m_rtGraphState.renderTracks
    // For now, keep the member variable name if useful, or refactor usage.
    // Let's refactor usage to use m_rtGraphState.renderTracks.
    // But RenderTrack is now in namespace spec.

    // std::vector<RenderTrack> m_renderTracks[2];  <-- Removed, now in m_rtGraphState
    // But wait, compileGraph uses double buffering of the vector itself?
    // "m_renderTracks[2]"
    // AudioGraphState currently just has "std::vector<RenderTrack> renderTracks;".
    // We should probably keep the double buffering logic here for safety?
    // Or does AudioGraphState handle it?
    // AudioGraphState is a snapshot.
    // AudioEngine needs to hold the "Pending" and "Active" state.

    // To minimize breakage, let's keep the double buffer logic but change type
    AudioGraphState m_graphStates[2];
    std::atomic<int> m_activeRenderTrackIndex{0};
    std::mutex m_graphMutex; // Protects graph compilation / swap

    /**
     * @brief Compile the mix graph for the audio thread.
     *
     * Topologically sorts tracks and swizzles pointers for zero-overhead routing.
     * Must be called when routing changes (Main Thread).
     */
    void compileGraph();

    // Helper for AudioRenderer (Legacy Bridge)
    // void renderClipAudio(...) - Moved
    // void processTrackEffects(...) - Moved

    // Parallel processing internal
    // [HYBRID ENGINE] Parallel dispatch temporarily disabled during refactor
    /*
    uint32_t m_parallelNumFrames{0};
    uint32_t m_parallelBufferOffset{0};
    std::vector<void*> m_parallelTrackPointers;
    static void parallelTrackDispatcher(void* context, void* taskData);
    void parallelProcessTrack(const RenderTrack& track);
    */

    // -----------------------------------------

    // Interpolation quality
    std::atomic<Interpolators::InterpolationQuality> m_interpQuality{Interpolators::InterpolationQuality::Cubic};

    // Master output processing (double precision)
    std::atomic<float> m_masterGainTarget{1.0f};
    std::atomic<float> m_headroomLinear{1.0f}; // 0dB headroom (standard DAW behavior)
    SmoothedParamD m_smoothedMasterGain;
    MasterSafetyLimiter m_safetyLimiter;
    std::atomic<bool> m_safetyLimiterEnabled{true};

    // Master-bus DC removal (off by default; see setDCRemovalEnabled).
    // m_dcRemovalPrevOn is RT-thread-only bookkeeping used to clear the blocker
    // state on the off->on transition so enabling never injects a stale-state step.
    std::atomic<bool> m_dcRemovalEnabled{false};
    DCBlockerD m_dcBlockerL;
    DCBlockerD m_dcBlockerR;
    bool m_dcRemovalPrevOn{false};

    // Peak detection
    std::atomic<float> m_peakL{0.0f};
    std::atomic<float> m_peakR{0.0f};
    std::atomic<float> m_rmsL{0.0f};
    std::atomic<float> m_rmsR{0.0f};

    // True-peak metering (Phase 2). Meter itself is touched only by the
    // audio thread; cross-thread reads use the published atomics.
    TruePeakMeter m_truePeakMeter;
    std::atomic<bool> m_truePeakMeteringEnabled{true};
    std::atomic<float> m_truePeakLAtomic{0.0f};
    std::atomic<float> m_truePeakRAtomic{0.0f};

    // TrackManager for playlist rendering (headless/offline mode)
    std::weak_ptr<TrackManager> m_trackManager;

    // Mixer meter snapshots (optional; when set, audio thread writes peaks)
    std::shared_ptr<MeterSnapshotBuffer> m_meterSnapshotsOwned;
    std::atomic<MeterSnapshotBuffer*> m_meterSnapshotsRaw{nullptr};
    std::shared_ptr<ContinuousParamBuffer> m_continuousParamsOwned;
    std::atomic<ContinuousParamBuffer*> m_continuousParamsRaw{nullptr};
    std::shared_ptr<const ChannelSlotMap> m_channelSlotMapOwned;
    std::atomic<const ChannelSlotMap*> m_channelSlotMapRaw{nullptr};

    // Audition Mode (Exclusive Bypass)
    std::atomic<AuditionEngine*> m_auditionEngine{nullptr};
    std::atomic<bool> m_auditionModeEnabled{false};

    // File Browser Preview Engine (additive mix into output)
    std::atomic<PreviewEngine*> m_previewEngine{nullptr};

    // Transport-aware preview ducking
    std::atomic<float> m_previewDuckGain{1.0f}; // Linear gain applied to transport when preview is active
    std::atomic<float> m_previewDuckTargetGain{0.5f};
    std::atomic<float> m_previewDuckingAttenuationDb{6.0f};
    std::atomic<uint8_t> m_previewDuckSource{static_cast<uint8_t>(PreviewDuckSource::None)};
    float m_smoothedPreviewDuckGain{1.0f}; // Smoothed version for click-free transitions
    float m_previewDuckHoldSecondsRemaining{0.0f};

    // Recent output ring buffer for oscilloscope/mini-waveform displays.
    std::vector<float> m_waveformHistory;
    std::atomic<uint32_t> m_waveformWriteIndex{0};
    std::atomic<uint32_t> m_waveformHistoryFrames{0};

    // Fade state machine
    enum class FadeState { None, FadingIn, FadingOut, Silent };
    std::atomic<FadeState> m_fadeState{FadeState::None};
    uint32_t m_fadeSamplesRemaining{0};
    static constexpr uint32_t FADE_OUT_SAMPLES = 1024;
    static constexpr uint32_t FADE_IN_SAMPLES = 256;
    static constexpr uint32_t CLIP_EDGE_FADE_SAMPLES = 128;

    // Pre-computed constants
    static constexpr double PI_D = 3.14159265358979323846;
    static constexpr double QUARTER_PI_D = PI_D * 0.25;

    // Meter analysis state (audio thread).
    uint32_t m_meterAnalysisSampleRate{0};
    double m_meterLfCoeff{0.0};
    std::array<double, MeterSnapshotBuffer::MAX_CHANNELS> m_meterLfStateL{};
    std::array<double, MeterSnapshotBuffer::MAX_CHANNELS> m_meterLfStateR{};

    // Metronome Engine (Refactored)
    MetronomeEngine m_metronomeEngine;

    // Loop state
    std::atomic<bool> m_loopEnabled{false};
    std::atomic<double> m_loopStartBeat{0.0};
    std::atomic<double> m_loopEndBeat{4.0}; // Default: 1 bar (4 beats)

    // Pattern Playback Mode State
    std::atomic<bool> m_patternPlaybackMode{false};
    std::atomic<double> m_patternLengthBeats{4.0};
    // Completed pattern-loop passes (audio thread writes at each wrap, reset
    // while stopped). Gives pattern scheduling a MONOTONIC frame domain
    // (iteration * loopLen + wrapped pos) so the next iteration's events are
    // queued before the wrap instead of after a UI maintenance tick.
    std::atomic<uint64_t> m_patternLoopIteration{0};
    // Coherent (iteration * loopLen + wrapped pos) snapshot for maintenance,
    // published in ONE store at the end of each callback alongside
    // m_globalSamplePos. Reading iteration and position as separate atomics
    // could pair a new iteration with a stale position (they are written at
    // different points in the callback) and schedule an epoch ahead.
    std::atomic<uint64_t> m_patternMonotonicFrame{0};
    // Last loop length (samples) used for a maintenance refill. Only touched
    // on the maintenance thread; a change (BPM / sample-rate) means queued
    // timestamps are in a stale domain and the pattern engine must flush.
    uint64_t m_lastRefillLoopLenSamples{0};

    // Test Tone State
    std::atomic<bool> m_testToneEnabled{false};
    std::atomic<bool> m_forceBounceWriteErrorForTests{false};
    std::atomic<bool> m_lastBounceWroteAnyFramesForTests{false};
    double m_testTonePhase{0.0};

    // Dependencies
    std::atomic<UnitManager*> m_unitManager{nullptr};
    std::atomic<PatternPlaybackEngine*> m_patternEngine{nullptr};

    // RT-safe sampler cache (refreshed from main thread when units change)
    // Avoids dynamic_pointer_cast on RT thread during hard stop/restart
    std::atomic<UnitManagerSnapshot*> m_unitManagerSnapshot{nullptr};

    // Legacy separate fields kept for backward compatibility during transition
    // TODO: remove these once all readers migrated to m_unitManagerSnapshot
    // Using double-buffering with atomic pointer swap for thread safety
    std::shared_ptr<SamplerCacheData> m_samplerCacheOwned{std::make_shared<SamplerCacheData>()};
    std::atomic<SamplerCacheData*> m_samplerCacheRaw{m_samplerCacheOwned.get()};
    std::array<Plugins::SamplerPlugin*, kMaxCachedSamplers> m_cachedSamplers{};
    std::array<std::shared_ptr<Plugins::SamplerPlugin>, kMaxCachedSamplers> m_cachedSamplerOwners{};
    std::atomic<size_t> m_cachedSamplerCount{0}; // Kept for compatibility
    std::atomic<uint64_t> m_lastDeferredResourceCollectionNs{0};

public:
    /**
     * @brief Refresh the sampler cache from UnitManager snapshot.
     * Call from main thread after unit changes.
     */
    void refreshSamplerCache();

private:
    // Parallel Processing
    std::unique_ptr<Aestra::RealTimeThreadPool> m_threadPool;
    std::unique_ptr<Aestra::Barrier> m_syncBarrier;
    std::atomic<bool> m_multiThreadingEnabled{false};

    // --- Cockroach-Grade LUFS Metering ---
    struct BiquadCoeff {
        double b0{0.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
    };

    struct BiquadState {
        double z1{0.0}, z2{0.0};

        inline double process(double x, const BiquadCoeff& c) {
            double v = x - c.a1 * z1 - c.a2 * z2;
            double y = c.b0 * v + c.b1 * z1 + c.b2 * z2;
            z2 = z1;
            z1 = v;
            return y;
        }
    };

    // Lock-free queue for passing block energies to worker
    // Fixed size ring buffer, power of 2 for fast wrapping
    static constexpr size_t kLoudnessQueueSize = 1024;
    struct BlockEnergyQueue {
        std::atomic<size_t> writeIdx{0};
        std::atomic<size_t> readIdx{0};
        std::array<double, kLoudnessQueueSize> data;

        bool try_push(double val) {
            size_t currentWrite = writeIdx.load(std::memory_order_relaxed);
            size_t nextWrite = (currentWrite + 1) & (kLoudnessQueueSize - 1);
            if (nextWrite == readIdx.load(std::memory_order_acquire)) {
                return false; // Full
            }
            data[currentWrite] = val;
            writeIdx.store(nextWrite, std::memory_order_release);
            return true;
        }

        bool pop(double& val) {
            size_t currentRead = readIdx.load(std::memory_order_relaxed);
            if (currentRead == writeIdx.load(std::memory_order_acquire)) {
                return false; // Empty
            }
            val = data[currentRead];
            readIdx.store((currentRead + 1) & (kLoudnessQueueSize - 1), std::memory_order_release);
            return true;
        }
    };

    struct alignas(64) LoudnessState {
        // Audio Thread (Write Only)
        BiquadState f1L, f1R; // Stage 1 (Shelf)
        BiquadState f2L, f2R; // Stage 2 (HPF)
        double blockEnergySum{0.0};
        uint32_t blockSamples{0};

        // Inter-thread Queue
        BlockEnergyQueue energyQueue;

        // Worker Thread (Read/Write)
        double gatedSum{0.0};
        uint64_t gatedBlocks{0}; // Count of blocks included in integrated measure

        // "Cockroach" Precision/Safety
        // We store a circular buffer of recent block energies to support the relative gate scanning
        // EBU R128 requires scanning ALL blocks to re-gate. For robust infinite runtime without infinite RAM:
        // We will maintain a histogram or just a "good enough" approximation?
        // Actually, the standard strictly essentially requires 2 passes.
        // Real-time meters often use a "running" approximation or just store finite history.
        // We will store last 3 hours of blocks (~27000 blocks) -> ~200KB. Cheap.
        static constexpr size_t kHistoryCapacity = 32768; // Power of 2
        std::vector<double> blockHistory;                 // Worker thread only
        size_t historyWriteIdx{0};
        size_t historyCount{0};

        // UI Thread (Read Only - Atomic)
        std::atomic<float> integratedLufs{-144.0f}; // Init to silence
        std::atomic<float> momentaryLufs{-144.0f};  // Short term readout

        LoudnessState() { blockHistory.resize(kHistoryCapacity, 0.0); }
    };

    LoudnessState m_loudnessState;
    std::thread m_loudnessThread;
    std::atomic<bool> m_loudnessThreadRunning{false};

    void startLoudnessWorker();
    void stopLoudnessWorker();
    void loudnessWorkerLoop();

public:
    // Reset metering (e.g. on transport start if configured)
    void resetLoudness() {
        // Reset atoms (UI sees it immediately)
        m_loudnessState.integratedLufs.store(-144.0f);
        m_loudnessState.momentaryLufs.store(-144.0f);

        // Request reset in worker (via queue? or just a flag?)
        // Simple flag is enough since exact sample accuracy of reset isn't critical for metering start
        m_loudnessResetRequested.store(true);
    }

private:
    std::atomic<bool> m_loudnessResetRequested{false};
    // Pre-computed filter coefficients (static 48 kHz fallback)
    static const BiquadCoeff kKWeightPreFilter; // HS
    static const BiquadCoeff kKWeightRLB;       // HPF

    // Sample-rate-aware coefficient computation via bilinear transform
    static BiquadCoeff computeKWeightPreFilter(double sampleRate);
    static BiquadCoeff computeKWeightRLB(double sampleRate);

    // Dynamic K-weighting coefficients — double-buffered for lock-free publication.
    // setSampleRate() writes into the inactive slot, then atomically flips the index.
    // processBlock() loads the index with acquire and reads from the active slot.
    BiquadCoeff m_kWeightPreFilterSlots[2];
    BiquadCoeff m_kWeightRlbSlots[2];
    std::atomic<uint32_t> m_activeKWeightIndex{0};

    TrackRTState m_dummyTrackState; // [FIX] Replaces static local to remove priority inversion risk
    std::atomic<bool> m_loggedRoutingCycleWarning{false};

    // Guard for resource loading (e.g., metronome samples)
    std::atomic<bool> m_resourcesLoading{false};

    // === Plugin Delay Compensation State ===
    bool m_latencyCompensationEnabled{true};
    uint32_t m_maxProjectLatency{0};
    bool m_latencyDirty{true}; // Recalculate on next safe opportunity
    // PDC v2 (P3): double-buffered, lock-free publish of SolvedLatencyTopology.
    //
    // The off-RT recompute writes the new topology to the inactive slot, then
    // flips m_activeSolvedTopologyIndex with release ordering. Readers (off-RT
    // tools, tests, P4 RT path) load the index with acquire ordering and read
    // from that slot. No mutex on the read path.
    //
    // NOTE: this index is deliberately separate from m_activeRenderTrackIndex.
    // PDC-v2 §9 suggested sharing the audio-graph atomic so both flip together;
    // unifying is deferred to P4+ once the engine reads compensation values
    // through SolvedLatencyTopology rather than directly from TrackRTState.
    std::array<SolvedLatencyTopology, 2> m_solvedTopologies;
    std::atomic<int> m_activeSolvedTopologyIndex{0};
    uint64_t m_latencyGraphGeneration{0};
};

} // namespace Audio
} // namespace Aestra
