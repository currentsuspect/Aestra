// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioCommandQueue.h"
#include "AudioTelemetry.h"
#include "ChannelSlotMap.h"
#include "ContinuousParamBuffer.h"
#include "EngineState.h"
#include "Interpolators.h"
#include "MeterSnapshot.h"
#include <cstdint>
#include <cmath>
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include <string>

namespace Nomad {
namespace Audio {

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
public:
    AudioEngine() = default;

    /**
     * @brief Process a single audio block (driver callback entry).
     * Must remain lock-free, allocation-free.
     */
    void processBlock(float* outputBuffer,
                      const float* inputBuffer,
                      uint32_t numFrames,
                      double streamTime);

    AudioCommandQueue& commandQueue() { return m_commandQueue; }
    AudioTelemetry& telemetry() { return m_telemetry; }
    EngineState& engineState() { return m_state; }

    void setSampleRate(uint32_t sampleRate) { m_sampleRate.store(sampleRate, std::memory_order_relaxed); }
    uint32_t getSampleRate() const { return m_sampleRate.load(std::memory_order_relaxed); }

    void setBufferConfig(uint32_t maxFrames, uint32_t numChannels);
    void setTransportPlaying(bool playing) { m_transportPlaying.store(playing, std::memory_order_relaxed); }
    bool isTransportPlaying() const { return m_transportPlaying.load(std::memory_order_relaxed); }
    void setGraph(const AudioGraph& graph) { m_state.swapGraph(graph); }

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
    void setMetronomeEnabled(bool enabled) { m_metronomeEnabled.store(enabled, std::memory_order_relaxed); }
    bool isMetronomeEnabled() const { return m_metronomeEnabled.load(std::memory_order_relaxed); }
    void setMetronomeVolume(float vol) { m_metronomeVolume.store(vol, std::memory_order_relaxed); }
    float getMetronomeVolume() const { return m_metronomeVolume.load(std::memory_order_relaxed); }
    void setBPM(float bpm) { m_bpm.store(bpm, std::memory_order_relaxed); }
    float getBPM() const { return m_bpm.load(std::memory_order_relaxed); }
    void setBeatsPerBar(int beats) { m_beatsPerBar.store(beats, std::memory_order_relaxed); m_currentBeat = 0; }
    int getBeatsPerBar() const { return m_beatsPerBar.load(std::memory_order_relaxed); }
    void loadMetronomeClicks(const std::string& downbeatPath, const std::string& upbeatPath);

    // Loop control
    void setLoopEnabled(bool enabled) { m_loopEnabled.store(enabled, std::memory_order_relaxed); }
    bool isLoopEnabled() const { return m_loopEnabled.load(std::memory_order_relaxed); }
    void setLoopRegion(double startBeat, double endBeat);
    double getLoopStartBeat() const { return m_loopStartBeat.load(std::memory_order_relaxed); }
    double getLoopEndBeat() const { return m_loopEndBeat.load(std::memory_order_relaxed); }

    // Metering (read on UI thread)
    float getPeakL() const { return m_peakL.load(std::memory_order_relaxed); }
    float getPeakR() const { return m_peakR.load(std::memory_order_relaxed); }
    float getRmsL() const { return m_rmsL.load(std::memory_order_relaxed); }
    float getRmsR() const { return m_rmsR.load(std::memory_order_relaxed); }

    // Waveform history (interleaved stereo), safe to read on UI thread.
    uint32_t getWaveformHistoryCapacity() const { return m_waveformHistoryFrames.load(std::memory_order_relaxed); }
    uint32_t copyWaveformHistory(float* outInterleaved, uint32_t maxFrames) const;

private:
    static constexpr size_t kMaxTracks = 4096;
    static constexpr uint32_t kWaveformHistoryFramesDefault = 2048;

    // Double-precision smoothed parameter for zero-zipper automation
    struct SmoothedParamD {
        double current{1.0};
        double target{1.0};
        double coeff{0.001};  // Per-sample coefficient
        
        inline double next() {
            current += coeff * (target - current);
            return current;
        }
        
        void setTarget(double t) { target = t; }
        void snap() { current = target; }  // Instant change
    };

    struct TrackRTState {
        SmoothedParamD volume;
        SmoothedParamD pan;
        bool mute{false};
        bool solo{false};
    };

    TrackRTState& ensureTrackState(uint32_t trackId);
    void renderGraph(const AudioGraph& graph, uint32_t numFrames);
    void applyPendingCommands();
    
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
    std::atomic<uint64_t> m_globalSamplePos{0};
    
    // Pre-allocated buffers - DOUBLE PRECISION for internal mixing
    std::vector<std::vector<double>> m_trackBuffersD;  // Double precision track buffers
    std::vector<double> m_masterBufferD;               // Double precision master
    std::vector<TrackRTState> m_trackState;
    
    // Interpolation quality
    std::atomic<Interpolators::InterpolationQuality> m_interpQuality{Interpolators::InterpolationQuality::Cubic};
    
    // Master output processing (double precision)
    std::atomic<float> m_masterGainTarget{1.0f};
    std::atomic<float> m_headroomLinear{0.5f};  // -6dB headroom
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
    
    // Metronome state
    std::atomic<bool> m_metronomeEnabled{false};
    std::atomic<float> m_metronomeVolume{0.7f};
    std::atomic<float> m_bpm{120.0f};
    std::atomic<int> m_beatsPerBar{4};              // Time signature numerator (4 for 4/4)
    std::vector<float> m_clickSamplesDown;          // Mono click for downbeat (low pitch)
    std::vector<float> m_clickSamplesUp;            // Mono click for upbeat (high pitch)
    uint32_t m_clickSampleRate{48000};              // Sample rate of loaded click
    size_t m_clickPlayhead{0};                      // Current position in click
    bool m_clickPlaying{false};                     // Currently playing a click
    uint64_t m_nextBeatSample{0};                   // Sample position of next beat
    int m_currentBeat{0};                           // Current beat in bar (0-based, 0=downbeat)
    float m_currentClickGain{1.0f};                 // Gain for current click
    const std::vector<float>* m_activeClickSamples{nullptr};  // Points to down or up samples

    // Loop state
    std::atomic<bool> m_loopEnabled{false};
    std::atomic<double> m_loopStartBeat{0.0};
    std::atomic<double> m_loopEndBeat{4.0};         // Default: 1 bar (4 beats)
};

} // namespace Audio
} // namespace Nomad
