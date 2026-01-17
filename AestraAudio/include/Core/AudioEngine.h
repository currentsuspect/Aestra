// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioCommandQueue.h"
#include "AudioDriverTypes.h"
#include "AudioTelemetry.h"
#include "ChannelSlotMap.h"
#include "ContinuousParamBuffer.h"
#include "EngineState.h"
#include "Interpolators.h"
#include "MeterSnapshot.h"
#include "PluginHost.h" // For MidiBuffer [NEW]
#include "AestraThreading.h"
#include <cstdint>
#include <cmath>
#include <vector>
#include <atomic>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <memory>
#include <mutex>
#include <array>
#include <string>

#include "AudioGraphState.h"
#include "AudioRenderer.h"
#include "MetronomeEngine.h" // [NEW]

namespace Aestra {
namespace Audio {

class UnitManager;
class PatternPlaybackEngine;
class AuditionEngine;
class PreviewEngine;

namespace Plugins {
    class SamplerPlugin;  // Forward declare for RT cache
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
 *
 * TODO: Future Architecture "Hybrid Engine" (See AESTRA_HYBRID_ENGINE_DESIGN.md)
 * Goal: Decouple rendering logic to support concurrent "Draft" (RT) and "Master" (Background) graphs.
 */
class AudioEngine {
    friend class AudioRenderer; // Allow access to private members during hybrid engine transition
public:
    AudioEngine();
    ~AudioEngine();

     /**
      * @brief Singleton Accessor (v3.1)
      */
    static AudioEngine& getInstance();

     /**
     * @brief Process a single audio block (driver callback entry).
     * Must remain lock-free, allocation-free.
     */
    void processBlock(float* outputBuffer,
                      const float* inputBuffer,
                      uint32_t numFrames,
                      double streamTime);
    
    /**
     * @brief Immediate panic/reset (Double Stop).
     * Clears all buffers and resets plugin states. Main Thread.
     */
    void panic();

    // Recording Callback (called on RT thread)
    using InputCallback = void (*)(const float* inputBuffer, uint32_t numFrames, void* userData);
    void setInputCallback(InputCallback callback, void* userData) {
        m_inputCallback.store(callback);
        m_inputCallbackData.store(userData);
    }

    AudioCommandQueue& commandQueue() { return m_commandQueue; }
    AudioTelemetry& telemetry() { return m_telemetry; }
    EngineState& engineState() { return m_state; }

    void setSampleRate(uint32_t sampleRate) { m_sampleRate.store(sampleRate, std::memory_order_relaxed); }
    uint32_t getSampleRate() const { return m_sampleRate.load(std::memory_order_relaxed); }

    void setBufferConfig(uint32_t maxFrames, uint32_t numChannels);
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
    bool isTransportPlaying() const { return m_transportPlaying.load(std::memory_order_relaxed); }
    void setGraph(const AudioGraph& graph) { 
        m_state.swapGraph(graph); 
        compileGraph();
    }

    // RT-safe metering (written on audio thread, read on UI thread)
    void setMeterSnapshots(std::shared_ptr<MeterSnapshotBuffer> snapshots) {
        m_meterSnapshotsOwned = std::move(snapshots);
        m_meterSnapshotsRaw.store(m_meterSnapshotsOwned.get(), std::memory_order_release);
    }

    // Continuous mixer params (UI writes, audio reads)
    void setContinuousParams(std::shared_ptr<ContinuousParamBuffer> params) {
        m_continuousParamsOwned = std::move(params);
        m_continuousParamsRaw.store(m_continuousParamsOwned.get(), std::memory_order_release);
    }

    // Stable channelId -> dense slot mapping (set only at safe points)
    void setChannelSlotMap(std::shared_ptr<const ChannelSlotMap> slotMap) {
        m_channelSlotMapOwned = std::move(slotMap);
        m_channelSlotMapRaw.store(m_channelSlotMapOwned.get(), std::memory_order_release);
    }
    
    // Position tracking
    uint64_t getGlobalSamplePos() const { return m_globalSamplePos.load(std::memory_order_relaxed); }
    void setGlobalSamplePos(uint64_t pos) { m_globalSamplePos.store(pos, std::memory_order_relaxed); }
    double getPositionSeconds() const { 
        uint32_t sr = m_sampleRate.load(std::memory_order_relaxed);
        return sr > 0 ? static_cast<double>(m_globalSamplePos.load(std::memory_order_relaxed)) / sr : 0.0; 
    }
    
    // Quality settings
    void setInterpolationQuality(Interpolators::InterpolationQuality q) { m_interpQuality.store(q, std::memory_order_relaxed); }
    Interpolators::InterpolationQuality getInterpolationQuality() const { return m_interpQuality.load(std::memory_order_relaxed); }
    
    // Master output control
    void setMasterGain(float gain) { m_masterGainTarget.store(gain, std::memory_order_relaxed); }
    float getMasterGain() const { return m_masterGainTarget.load(std::memory_order_relaxed); }
    void setHeadroom(float db) { m_headroomLinear.store(std::pow(10.0f, db / 20.0f), std::memory_order_relaxed); }
    void setSafetyProcessingEnabled(bool enabled) { m_safetyProcessingEnabled.store(enabled, std::memory_order_relaxed); }
    bool isSafetyProcessingEnabled() const { return m_safetyProcessingEnabled.load(std::memory_order_relaxed); }
    
    // Metronome control
    void setMetronomeEnabled(bool enabled) { m_metronomeEngine.setEnabled(enabled); }
    bool isMetronomeEnabled() const { return m_metronomeEngine.isEnabled(); }
    void setMetronomeVolume(float vol) { m_metronomeEngine.setVolume(vol); }
    float getMetronomeVolume() const { return m_metronomeEngine.getVolume(); }
    void setBPM(float bpm) { m_metronomeEngine.setBPM(bpm); }
    float getBPM() const { return m_metronomeEngine.getBPM(); }
    void setBeatsPerBar(int beats) { m_metronomeEngine.setBeatsPerBar(beats); }
    int getBeatsPerBar() const { return m_metronomeEngine.getBeatsPerBar(); }
    void loadMetronomeClicks(const std::string& downbeatPath, const std::string& upbeatPath) {
        if (isTransportPlaying()) {
             // Avoid I/O during playback to prevent dropouts
             return;
        }
        m_metronomeEngine.loadClickSounds(downbeatPath, upbeatPath);
    }

    // Loop control
    void setLoopEnabled(bool enabled) { m_loopEnabled.store(enabled, std::memory_order_relaxed); }
    bool isLoopEnabled() const { return m_loopEnabled.load(std::memory_order_relaxed); }
    void setLoopRegion(double startBeat, double endBeat);
    double getLoopStartBeat() const { return m_loopStartBeat.load(std::memory_order_relaxed); }
    double getLoopEndBeat() const { return m_loopEndBeat.load(std::memory_order_relaxed); }

    // Pattern Playback Mode (Overrides loop behavior for Arsenal)
    void setPatternPlaybackMode(bool enabled, double lengthBeats) {
        m_patternPlaybackMode.store(enabled, std::memory_order_relaxed);
        m_patternLengthBeats.store(lengthBeats, std::memory_order_relaxed);
    }
    bool isPatternPlaybackMode() const { return m_patternPlaybackMode.load(std::memory_order_relaxed); }
    
    // Antigravity Dependencies (v3.1)
    void setUnitManager(UnitManager* mgr) { m_unitManager.store(mgr, std::memory_order_release); }
    void setPatternPlaybackEngine(PatternPlaybackEngine* engine) { m_patternEngine.store(engine, std::memory_order_release); }
    
    // Pattern change detection - reset voices when pattern ID changes (not on MIDI edits)
    void requestVoiceResetOnPatternChange();

    // Metering (read on UI thread)
    float getPeakL() const { return m_peakL.load(std::memory_order_relaxed); }
    float getPeakR() const { return m_peakR.load(std::memory_order_relaxed); }
    float getRmsL() const { return m_rmsL.load(std::memory_order_relaxed); }
    float getRmsR() const { return m_rmsR.load(std::memory_order_relaxed); }
    
    // Dithering control
    void setDitheringMode(DitheringMode mode) { m_ditheringMode.store(mode, std::memory_order_relaxed); }
    DitheringMode getDitheringMode() const { return m_ditheringMode.load(std::memory_order_relaxed); }

    // Test Tone
    void setTestToneEnabled(bool enabled) { m_testToneEnabled.store(enabled, std::memory_order_relaxed); }
    bool isTestToneEnabled() const { return m_testToneEnabled.load(std::memory_order_relaxed); }


    // Waveform history (interleaved stereo), safe to read on UI thread.
    uint32_t getWaveformHistoryCapacity() const { return m_waveformHistoryFrames.load(std::memory_order_relaxed); }
    uint32_t copyWaveformHistory(float* outInterleaved, uint32_t maxFrames) const;

    // Multi-threading
    void setThreadCount(int count);
    void setMultiThreadingEnabled(bool enabled) { m_multiThreadingEnabled.store(enabled, std::memory_order_relaxed); }
    bool isMultiThreadingEnabled() const { return m_multiThreadingEnabled.load(std::memory_order_relaxed); }

    // Audition Mode
    void setAuditionEngine(AuditionEngine* engine) { m_auditionEngine.store(engine, std::memory_order_relaxed); }
    void setAuditionModeEnabled(bool enabled) { m_auditionModeEnabled.store(enabled, std::memory_order_relaxed); }
    bool isAuditionModeEnabled() const { return m_auditionModeEnabled.load(std::memory_order_relaxed); }
    
    // File Browser Preview (mixes into main output)
    void setPreviewEngine(PreviewEngine* engine) { m_previewEngine.store(engine, std::memory_order_relaxed); }
    
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

private:
    static constexpr size_t kMaxTracks = 4096;
    static constexpr uint32_t kWaveformHistoryFramesDefault = 2048;

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
        inline float nextFloat() {
            return (next() & 0xFFFFFF) * (1.0f / 16777216.0f);
        }
    };
    mutable FastRNG m_ditherRng;
    std::atomic<DitheringMode> m_ditheringMode{DitheringMode::Triangular}; // Default TPDF



    TrackRTState& ensureTrackState(uint32_t trackId);
    void renderGraph(const AudioGraph& graph, uint32_t numFrames, uint32_t bufferOffset = 0);
    void applyPendingCommands();
    void processArsenalUnits(uint32_t numFrames, uint32_t bufferOffset, uint64_t startFrame);
    
    // Pre-allocated buffers for Arsenal unit processing (RT-safe)
    std::vector<double> m_unitBufferD;  // Stereo interleaved unit output
    std::vector<MidiBuffer> m_unitMidiBuffers; // Per-unit MIDI buffers (max 32 units)
    std::vector<float> m_pluginBufferF; // Stereo de-interleaved float buffer for plugin processing
    std::vector<float> m_silentBufferF; // Zero buffer for inputs
    
    // Soft clipper (transparent below unity)
    static inline double softClipD(double x) {
        if (x > 1.5) return 1.0;
        if (x < -1.5) return -1.0;
        const double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }
    
    // DC blocker (double precision)
    struct DCBlockerD {
        double x1{0.0};
        double y1{0.0};
        static constexpr double R = 0.9997;  // Slightly more aggressive
        
        inline double process(double x) {
            double y = x - x1 + R * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    AudioCommandQueue m_commandQueue;
    AudioTelemetry m_telemetry;
    EngineState m_state;

    std::atomic<uint32_t> m_sampleRate{48000};
    std::atomic<uint32_t> m_maxBufferFrames{4096};  // Larger default for safety
    std::atomic<uint32_t> m_outputChannels{2};
    std::atomic<bool> m_transportPlaying{false};
    // RT-side tracking of last known transport state (avoids race with UI atomic updates)
    bool m_rtLastTransportPlaying{false};
    uint64_t m_rtLastTransportPos{0};
    // Transport edge flags (set in applyPendingCommands, consumed in processBlock)
    std::atomic<bool> m_transportRestartRequested{false};
    std::atomic<bool> m_transportStopRequested{false};
    // Hard stop: immediate silence (e.g., stop pressed twice)
    std::atomic<bool> m_transportHardStopRequested{false};

    // Request MIDI panic (All Notes Off / All Sound Off) injection into unit MIDI buffers.
    std::atomic<bool> m_transportMidiPanicRequested{false};
    std::atomic<uint64_t> m_globalSamplePos{0};
    
    // Pre-allocated buffers - DOUBLE PRECISION for internal mixing
    std::vector<std::vector<double>> m_trackBuffersD;  // Double precision track buffers
    std::vector<double> m_masterBufferD;               // Double precision master
    std::vector<float> m_scratchL;                     // Scratch L for plugins
    std::vector<float> m_scratchR;                     // Scratch R for plugins
    std::vector<MidiBuffer> m_scratchMidiBuffers;      // [NEW] Scratch MIDI buffers for units
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
    
    // --- Antigravity Routing Engine (v3.1) ---
    // Moved struct definitions to AudioGraphState.h
    
    // [HYBRID ENGINE] State & Renderer
    AudioGraphState m_rtGraphState;         // The Real-Time Graph State
    // AudioGraphState m_bgGraphState;      // The Background Graph State (Future)
    
    AudioRenderer m_rtRenderer;             // The Real-Time Renderer
    
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
    std::atomic<float> m_headroomLinear{1.0f};  // 0dB headroom (standard DAW behavior)
    SmoothedParamD m_smoothedMasterGain;
    DCBlockerD m_dcBlockerL;
    DCBlockerD m_dcBlockerR;
    std::atomic<bool> m_safetyProcessingEnabled{false};
    
    // Peak detection
    std::atomic<float> m_peakL{0.0f};
    std::atomic<float> m_peakR{0.0f};
    std::atomic<float> m_rmsL{0.0f};
    std::atomic<float> m_rmsR{0.0f};

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


    // Recent output ring buffer for oscilloscope/mini-waveform displays.
    std::vector<float> m_waveformHistory;
    std::atomic<uint32_t> m_waveformWriteIndex{0};
    std::atomic<uint32_t> m_waveformHistoryFrames{0};
    
    // Fade state machine
    enum class FadeState { None, FadingIn, FadingOut, Silent };
    FadeState m_fadeState{FadeState::None};
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
    std::atomic<double> m_loopEndBeat{4.0};         // Default: 1 bar (4 beats)

    // Pattern Playback Mode State
    std::atomic<bool> m_patternPlaybackMode{false};
    std::atomic<double> m_patternLengthBeats{4.0};

    // Test Tone State
    std::atomic<bool> m_testToneEnabled{false};
    double m_testTonePhase{0.0};
    
    // Dependencies
    std::atomic<UnitManager*> m_unitManager{nullptr};
    std::atomic<PatternPlaybackEngine*> m_patternEngine{nullptr};
    
    // RT-safe sampler cache (refreshed from main thread when units change)
    // Avoids dynamic_pointer_cast on RT thread during hard stop/restart
    static constexpr size_t kMaxCachedSamplers = 64;
    std::array<Plugins::SamplerPlugin*, kMaxCachedSamplers> m_cachedSamplers{};
    std::atomic<size_t> m_cachedSamplerCount{0};
    
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
        std::vector<double> blockHistory; // Worker thread only
        size_t historyWriteIdx{0};
        size_t historyCount{0};
        
        // UI Thread (Read Only - Atomic)
        std::atomic<float> integratedLufs{-144.0f}; // Init to silence
        std::atomic<float> momentaryLufs{-144.0f};  // Short term readout
        
        LoudnessState() {
            blockHistory.resize(kHistoryCapacity, 0.0);
        }
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
    // Pre-computed filter coefficients (static)
    static const BiquadCoeff kKWeightPreFilter; // HS
    static const BiquadCoeff kKWeightRLB;       // HPF

    TrackRTState m_dummyTrackState; // [FIX] Replaces static local to remove priority inversion risk

    // Guard for resource loading (e.g., metronome samples)
    std::atomic<bool> m_resourcesLoading{false};
};

} // namespace Audio
} // namespace Aestra
