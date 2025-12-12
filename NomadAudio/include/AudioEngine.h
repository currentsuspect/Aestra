// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioCommandQueue.h"
#include "AudioTelemetry.h"
#include "EngineState.h"
#include "Interpolators.h"
#include <cstdint>
#include <cmath>
#include <atomic>
#include <vector>

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

    void setSampleRate(uint32_t sampleRate) { m_sampleRate = sampleRate; }
    uint32_t getSampleRate() const { return m_sampleRate; }

    void setBufferConfig(uint32_t maxFrames, uint32_t numChannels);
    void setTransportPlaying(bool playing) { m_transportPlaying = playing; }
    bool isTransportPlaying() const { return m_transportPlaying; }
    void setGraph(const AudioGraph& graph) { m_state.swapGraph(graph); }
    
    // Position tracking
    uint64_t getGlobalSamplePos() const { return m_globalSamplePos; }
    void setGlobalSamplePos(uint64_t pos) { m_globalSamplePos = pos; }
    double getPositionSeconds() const { 
        return m_sampleRate > 0 ? static_cast<double>(m_globalSamplePos) / m_sampleRate : 0.0; 
    }
    
    // Quality settings
    void setInterpolationQuality(Interpolators::InterpolationQuality q) { m_interpQuality = q; }
    Interpolators::InterpolationQuality getInterpolationQuality() const { return m_interpQuality; }
    
    // Master output control
    void setMasterGain(float gain) { m_masterGainTarget = gain; }
    float getMasterGain() const { return m_masterGainTarget; }
    void setHeadroom(float db) { m_headroomLinear = std::pow(10.0f, db / 20.0f); }
    void setSafetyProcessingEnabled(bool enabled) { m_safetyProcessingEnabled = enabled; }
    bool isSafetyProcessingEnabled() const { return m_safetyProcessingEnabled; }

    // Metering (read on UI thread)
    float getPeakL() const { return m_peakL.load(std::memory_order_relaxed); }
    float getPeakR() const { return m_peakR.load(std::memory_order_relaxed); }

private:
    static constexpr size_t kMaxTracks = 64;

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

    uint32_t m_sampleRate{48000};
    uint32_t m_maxBufferFrames{4096};  // Larger default for safety
    uint32_t m_outputChannels{2};
    bool m_transportPlaying{false};
    uint64_t m_globalSamplePos{0};
    
    // Pre-allocated buffers - DOUBLE PRECISION for internal mixing
    std::vector<std::vector<double>> m_trackBuffersD;  // Double precision track buffers
    std::vector<double> m_masterBufferD;               // Double precision master
    std::vector<TrackRTState> m_trackState;
    
    // Interpolation quality
    Interpolators::InterpolationQuality m_interpQuality{Interpolators::InterpolationQuality::Cubic};
    
    // Master output processing (double precision)
    float m_masterGainTarget{1.0f};
    float m_headroomLinear{0.5f};  // -6dB headroom
    SmoothedParamD m_smoothedMasterGain;
    DCBlockerD m_dcBlockerL;
    DCBlockerD m_dcBlockerR;
    bool m_safetyProcessingEnabled{false};
    
    // Peak detection
    std::atomic<float> m_peakL{0.0f};
    std::atomic<float> m_peakR{0.0f};
    
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
};

} // namespace Audio
} // namespace Nomad
