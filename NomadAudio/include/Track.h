// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MixerBus.h"
#include "SampleRateConverter.h"
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <fstream>
#include <random>
#include <functional>
#include "SamplePool.h"
#include "AudioCommandQueue.h"

namespace Nomad {
namespace Audio {

/**
 * @brief Simple UUID implementation for stable track identity
 */
struct TrackUUID {
    uint64_t high = 0;
    uint64_t low = 0;
    
    bool isValid() const { return high != 0 || low != 0; }
    bool operator==(const TrackUUID& other) const { return high == other.high && low == other.low; }
    bool operator!=(const TrackUUID& other) const { return !(*this == other); }
    bool operator<(const TrackUUID& other) const { 
        return high < other.high || (high == other.high && low < other.low); 
    }
    
    std::string toString() const {
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 static_cast<uint32_t>(high >> 32),
                 static_cast<uint16_t>(high >> 16),
                 static_cast<uint16_t>(high),
                 static_cast<uint16_t>(low >> 48),
                 low & 0xFFFFFFFFFFFFULL);
        return std::string(buf);
    }
    
    static TrackUUID generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        
        TrackUUID uuid;
        uuid.high = dis(gen);
        uuid.low = dis(gen);
        // Set version 4 (random) and variant bits
        uuid.high = (uuid.high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        uuid.low = (uuid.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        return uuid;
    }
    
    static TrackUUID fromString(const std::string& str) {
        TrackUUID uuid;
        if (str.length() >= 36) {
            unsigned int a, b, c, d;
            unsigned long long e;
            if (sscanf(str.c_str(), "%8x-%4x-%4x-%4x-%12llx", &a, &b, &c, &d, &e) == 5) {
                uuid.high = (static_cast<uint64_t>(a) << 32) | 
                           (static_cast<uint64_t>(b) << 16) | 
                           static_cast<uint64_t>(c);
                uuid.low = (static_cast<uint64_t>(d) << 48) | e;
            }
        }
        return uuid;
    }
};

/**
 * @brief Audio track states
 */
enum class TrackState {
    Empty,      // No audio data
    Loaded,     // Audio file loaded
    Recording,  // Currently recording
    Playing,    // Currently playing
    Paused,     // Playback paused
    Stopped     // Playback stopped
};

/**
 * @brief Interpolation quality modes
 */
enum class InterpolationQuality {
    Linear,     // Fast, basic quality (2-point linear)
    Cubic,      // Good quality (4-point cubic Hermite)
    Sinc,       // Best quality (8-point windowed sinc)
    Ultra       // Mastering grade (16-point polyphase sinc)
};

/**
 * @brief Resampling mode for sample rate conversion
 */
enum class ResamplingMode {
    Fast,       // Linear interpolation (2-point)
    Medium,     // Cubic interpolation (4-point)
    High,       // Windowed sinc (8-point)
    Ultra,      // Polyphase sinc (16-point, reference grade)
    Extreme,    // Polyphase sinc (64-point, mastering grade)
    Perfect     // Polyphase sinc (512-point, FL Studio grade - extreme quality)
};

/**
 * @brief Dithering modes
 */
enum class DitheringMode {
    None,           // No dithering
    Triangular,     // TPDF (Triangular Probability Density Function)
    HighPass,       // High-pass shaped dither
    NoiseShaped     // Psychoacoustic noise shaping (pushes noise above hearing range)
};

/**
 * @brief Internal processing precision
 */
enum class InternalPrecision {
    Float32,    // 32-bit float (realtime)
    Float64     // 64-bit double (mastering, reduces rounding errors)
};

/**
 * @brief Oversampling modes
 */
enum class OversamplingMode {
    None,       // No oversampling
    Auto,       // Automatic (enable for nonlinear effects only)
    Force2x,    // Force 2x oversampling
    Force4x     // Force 4x oversampling (mastering)
};

/**
 * @brief Nomad Mode - Sonic character toggle
 */
enum class NomadMode {
    Off,            // Disabled (bypass all Nomad Mode processing)
    Transparent,    // Clinical precision, reference-grade (default)
    Euphoric        // Analog soul: harmonic warmth, smooth transients, rich tails
};

/**
 * @brief Quality presets for easy configuration
 */
enum class QualityPreset {
    Custom,         // User-defined settings
    Economy,        // Low CPU: Linear, no oversampling, 32-bit
    Balanced,       // Recommended: Cubic, auto oversampling, 32-bit
    HighFidelity,   // High quality: Sinc, 2x oversampling, noise-shaped dither
    Mastering       // Maximum: Ultra sinc, 4x oversampling, 64-bit, full processing
};

/**
 * @brief Comprehensive audio quality settings
 */
struct AudioQualitySettings {
    // Core Quality
    ResamplingMode resampling{ResamplingMode::Medium};
    DitheringMode dithering{DitheringMode::Triangular};
    InternalPrecision precision{InternalPrecision::Float32};
    OversamplingMode oversampling{OversamplingMode::None};
    
    // Legacy compatibility (maps to resampling)
    InterpolationQuality interpolation{InterpolationQuality::Cubic};
    
    // Processing options
    bool removeDCOffset{true};          // Remove DC bias from audio
    bool enableSoftClipping{false};     // Soft-clip protection on output
    bool autoGainNormalization{false};  // LUFS-based auto gain (future)
    
    // Nomad Mode - Sonic Character
    NomadMode nomadMode{NomadMode::Off};  // Off / Transparent / Euphoric
    
    // Euphoria Engine Settings (active when nomadMode == Euphoric)
    struct EuphoriaSettings {
        bool tapeCircuit{true};         // Non-linear transient rounding + harmonic bloom
        bool airEnhancement{true};       // Psychoacoustic stereo widening (mid/side delay)
        bool driftEffect{false};         // Subtle detune & clock variance (warmth)
        float harmonicBloom{0.15f};     // Harmonic saturation amount (0.0 - 1.0)
        float transientSmoothing{0.25f}; // Transient rounding (0.0 - 1.0)
    } euphoria;
    
    // Anti-aliasing filter steepness (for resampling)
    enum class FilterSteepness { Soft, Medium, Steep } antiAliasingFilter{FilterSteepness::Medium};
    
    // Quality preset tracking
    QualityPreset preset{QualityPreset::Balanced};
    
    AudioQualitySettings() = default;
    
    // Preset constructors
    static AudioQualitySettings Economy();
    static AudioQualitySettings Balanced();
    static AudioQualitySettings HighFidelity();
    static AudioQualitySettings Mastering();
    
    // Apply preset
    void applyPreset(QualityPreset preset);
};

/**
 * @brief Audio track class for DAW
 *
 * Manages individual audio tracks with:
 * - Track properties (name, color, volume, pan, mute, solo)
 * - Audio data management (sample buffers, file loading)
 * - Recording functionality
 * - Real-time parameter control
 */
class Track {
public:
    Track(const std::string& name = "Track", uint32_t trackId = 0);
    ~Track();

    // === STABLE IDENTITY (UUID - never changes after creation) ===
    const TrackUUID& getUUID() const { return m_uuid; }
    void setUUID(const TrackUUID& uuid) { m_uuid = uuid; }  // Only for deserialization
    
    // Track Properties (display name - user editable, NOT used for identity)
    void setName(const std::string& name);
    const std::string& getName() const { return m_name; }

    void setColor(uint32_t color);  // ARGB format
    uint32_t getColor() const { return m_color; }

    uint32_t getTrackId() const { return m_trackId; }
    uint32_t getTrackIndex() const { return m_trackIndex; }
    void setTrackIndex(uint32_t idx) { m_trackIndex = idx; }

    // Audio Parameters (thread-safe)
    void setVolume(float volume);  // 0.0 to 1.0
    float getVolume() const { return m_volume.load(); }

    void setPan(float pan);  // -1.0 (left) to 1.0 (right)
    float getPan() const { return m_pan.load(); }

    void setMute(bool mute);
    bool isMuted() const { return m_muted.load(); }

    void setSolo(bool solo);
    bool isSoloed() const { return m_soloed.load(); }
    
    // System track flag (preview, test sound, etc - not affected by transport)
    void setSystemTrack(bool isSystem) { m_isSystemTrack = isSystem; }
    bool isSystemTrack() const { return m_isSystemTrack; }
    
    // === LANE INDEX (for grouping clips on the same visual row) ===
    // Clips with the same laneIndex are rendered on the same horizontal lane
    void setLaneIndex(int32_t index) { m_laneIndex = index; }
    int32_t getLaneIndex() const { return m_laneIndex; }

    // Track State
    void setState(TrackState state);
    TrackState getState() const { return m_state.load(); }

    // Audio Data Management
    bool loadAudioFile(const std::string& filePath);
    bool generatePreviewTone(const std::string& filePath);
    bool generateDemoAudio(const std::string& filePath);
    // Optional targetSampleRate allows resampling on load to match engine/device SR.
    void setAudioData(const float* data, uint32_t numSamples, uint32_t sampleRate, uint32_t numChannels, uint32_t targetSampleRate = 0);
    void clearAudioData();
    
    // Waveform data access (for UI visualization)
    const std::vector<float>& getAudioData() const;
    uint32_t getSampleRate() const { return m_sampleRate; }
    uint32_t getNumChannels() const { return m_numChannels; }

    // Recording
    void startRecording();
    void stopRecording();
    bool isRecording() const { return m_state.load() == TrackState::Recording; }

    // Playback Control
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return m_state.load() == TrackState::Playing; }

    // Position Control
    void setPosition(double seconds);
    double getPosition() const { return m_positionSeconds.load(); }
    double getDuration() const { return m_durationSeconds.load(); }
    
    // Sample Timeline Position (where sample starts in the timeline, in seconds)
    void setStartPositionInTimeline(double seconds) { m_startPositionInTimeline.store(seconds); }
    double getStartPositionInTimeline() const { return m_startPositionInTimeline.load(); }
    void setSourcePath(const std::string& path) { m_sourcePath = path; }
    const std::string& getSourcePath() const { return m_sourcePath; }
    
    // === CLIP TRIMMING (non-destructive) ===
    // Trim positions define which portion of the audio is used (in seconds from start of audio)
    void setTrimStart(double seconds);    // Where playback begins within the audio
    void setTrimEnd(double seconds);      // Where playback ends within the audio  
    double getTrimStart() const { return m_trimStart.load(); }
    double getTrimEnd() const { return m_trimEnd.load(); }
    double getTrimmedDuration() const;    // Duration of trimmed clip
    void resetTrim();                     // Reset to full audio length
    
    // Split clip at position (returns new Track with second half, modifies this track in place)
    std::shared_ptr<Track> splitAt(double positionInClip);
    
    // Create a copy of this clip (for duplicate/copy operations)
    std::shared_ptr<Track> duplicate() const;

    // Audio Processing
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate);

    // Mixer Integration
    MixerBus* getMixerBus() { return m_mixerBus.get(); }
    const MixerBus* getMixerBus() const { return m_mixerBus.get(); }
    
    // Latency Compensation
    void setLatencyCompensation(double inputLatencyMs, double outputLatencyMs);
    double getLatencyCompensationMs() const { return m_latencyCompensationMs; }
    
    // Audio Quality Settings
    void setQualitySettings(const AudioQualitySettings& settings);
    const AudioQualitySettings& getQualitySettings() const { return m_qualitySettings; }

    // Change notifications (owner can observe data changes to rebuild graphs)
    void setOnDataChanged(std::function<void()> cb) { m_onDataChanged = std::move(cb); }
    // Command sink for RT parameter updates
    void setCommandSink(std::function<void(const AudioQueueCommand&)> cb) { m_commandSink = std::move(cb); }

private:
    // Track identification
    TrackUUID m_uuid;        // Stable unique identifier (never changes after creation)
    std::string m_name;      // Display name (user-editable, NOT used for identity)
    uint32_t m_trackId;
    uint32_t m_trackIndex{0};  // zero-based index in TrackManager ordering
    uint32_t m_color;  // ARGB format
    bool m_isSystemTrack{false};  // System tracks (preview, test sound) aren't affected by transport
    int32_t m_laneIndex{-1};      // Visual lane index (-1 = auto-assign based on track order)

    // Audio parameters (atomic for thread safety)
    std::atomic<float> m_volume{1.0f};
    std::atomic<float> m_pan{0.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_soloed{false};

    // Track state
    std::atomic<TrackState> m_state{TrackState::Empty};
    std::atomic<double> m_positionSeconds{0.0};
    std::atomic<double> m_durationSeconds{0.0};
    std::atomic<double> m_startPositionInTimeline{0.0};  // Where sample starts in timeline (seconds)
    
    // Clip trim positions (non-destructive - define which portion of audio to use)
    std::atomic<double> m_trimStart{0.0};     // Start point within audio (seconds)
    std::atomic<double> m_trimEnd{-1.0};      // End point within audio (-1 = full length)

    // Audio data
    std::vector<float> m_audioData;  // Interleaved stereo samples (streaming/recording/temp)
    std::shared_ptr<AudioBuffer> m_sampleBuffer; // Shared decoded buffer from SamplePool (non-streaming)
    uint32_t m_sampleRate{48000};
    uint32_t m_numChannels{2};       // Always 2 after downmix
    uint32_t m_sourceChannels{2};    // Original channel count on load
    std::string m_sourcePath;
    std::atomic<double> m_playbackPhase{0.0};  // For sample-accurate playback
    mutable std::recursive_mutex m_audioDataMutex;  // Recursive to allow nested locking (e.g., copyAudioData -> trimStreamBuffer)
    std::atomic<bool> m_streaming{false};
    std::thread m_streamThread;
    std::atomic<bool> m_streamStop{false};
    std::condition_variable m_streamCv;
    std::mutex m_streamMutex;
    std::atomic<uint64_t> m_streamBaseFrame{0};     // absolute frame index for m_audioData[0]
    std::atomic<uint64_t> m_streamTotalFrames{0};   // total frames in source
    std::atomic<bool> m_streamEof{false};
    std::atomic<uint32_t> m_streamBytesPerSample{0};
    std::atomic<uint32_t> m_streamDataOffset{0};
    std::ifstream m_streamFile;

    // Mixer integration
    std::unique_ptr<MixerBus> m_mixerBus;

    // Recording state
    std::vector<float> m_recordingBuffer;
    std::atomic<bool> m_isRecording{false};
    
    // Latency compensation (milliseconds)
    double m_latencyCompensationMs{0.0};  // Total input + output latency for recording
    
    // Audio quality settings
    AudioQualitySettings m_qualitySettings;
    double m_dcOffset{0.0};  // Accumulated DC offset for removal
    
    // Dithering state (for noise shaping)
    float m_ditherHistory[2]{0.0f, 0.0f};  // Per-channel dither history for noise shaping

    // Temporary buffer reused per process call to avoid allocations
    mutable std::vector<float> m_tempBuffer;
    
    // Sample Rate Converter for batch processing (replaces legacy per-sample interpolation)
    SampleRateConverter m_srcConverter;
    bool m_useSRCModule{false};  // SRC module disabled - it's designed for streaming, not random-access playback
    uint32_t m_lastOutputSampleRate{0};  // Track output rate changes
    
    // Air effect state (per-track, replaces static variables in applyAir)
    float m_airDelayL[8]{};
    float m_airDelayR[8]{};
    int m_airDelayPos{0};
    
    // Drift effect state (per-track, replaces static variables in applyDrift)
    float m_driftPhase{0.0f};
    float m_driftAmount{0.0f};
    
    // Audio-quality fast PRNG state (for deterministic dithering)
    uint32_t m_ditherRngState{12345};

    std::function<void()> m_onDataChanged;
    std::function<void(const AudioQueueCommand&)> m_commandSink;

    // Internal audio processing
    void generateSilence(float* buffer, uint32_t numFrames);
    void copyAudioData(float* outputBuffer, uint32_t numFrames, double outputSampleRate);
    void trimStreamBuffer(uint64_t currentFrame);
    void stopStreaming();
    bool startWavStreaming(const std::string& filePath, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample, uint32_t dataOffset, uint64_t dataSize, uint64_t startFrame = 0);
    void streamWavThread(uint32_t channels);
    
    // Interpolation methods
    float interpolateLinear(const float* data, uint32_t totalSamples, double position, uint32_t channel) const;
    float interpolateCubic(const float* data, uint32_t totalSamples, double position, uint32_t channel) const;
    float interpolateSinc(const float* data, uint32_t totalSamples, double position, uint32_t channel) const;
    float interpolateUltra(const float* data, uint32_t totalSamples, double position, uint32_t channel) const;
    float interpolateExtreme(const float* data, uint32_t totalSamples, double position, uint32_t channel) const;
    float interpolatePerfect(const float* data, uint32_t totalSamples, double position, uint32_t channel) const;
    
    // Audio quality processing
    void applyDithering(float* buffer, uint32_t numSamples);
    void applyTriangularDither(float* buffer, uint32_t numSamples);
    void applyHighPassDither(float* buffer, uint32_t numSamples);
    void applyNoiseShapedDither(float* buffer, uint32_t numSamples);
    void removeDC(float* buffer, uint32_t numSamples);
    void applySoftClipping(float* buffer, uint32_t numSamples);
    void applyStereoWidth(float* buffer, uint32_t numFrames, float widthPercent);
    
    // Euphoria Engine (Nomad Mode signature processing)
    void applyEuphoriaEngine(float* buffer, uint32_t numFrames);
    void applyTapeCircuit(float* buffer, uint32_t numSamples, float bloomAmount, float smoothing);
    void applyAir(float* buffer, uint32_t numFrames);
    void applyDrift(float* buffer, uint32_t numFrames);
    
    // SRC quality mapping helper
    static SRCQuality mapResamplingToSRC(ResamplingMode mode);
};

} // namespace Audio
} // namespace Nomad
