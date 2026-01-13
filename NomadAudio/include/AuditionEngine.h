// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file AuditionEngine.h
 * @brief Dedicated audio engine for Audition Mode
 * 
 * Separate from the main AudioEngine to provide a pure listening experience.
 * Handles file playback, queue management, and DSP chain processing.
 * 
 * Key philosophy: "Psychologically out of the DAW" - simple, focused playback.
 */

#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>
#include <cstdint>

namespace Nomad {
namespace Audio {

class ClipSource;

/**
 * @brief Represents a single item in the audition queue
 */
struct AuditionQueueItem {
    std::string id;                    // Unique identifier
    std::string filePath;              // Path to audio file
    std::string title;                 // Display title
    std::string artist;                // Display artist
    std::string album;                 // Album name
    
    // Metadata
    double durationSeconds{0.0};       // Total duration
    std::vector<uint8_t> coverArtData; // Raw image data (JPEG/PNG)
    std::string coverArtMimeType;      // "image/jpeg", "image/png"
    
    bool isFromTimeline{false};        // True if this is from project tracks
    uint32_t sourceTrackId{0};         // If from timeline, which track
    bool isReference{false};           // True if external reference track
    
    // Playback state
    double lastPosition{0.0};          // Resume position
};

/**
 * @brief DSP simulation preset for audition playback
 */
struct AuditionDSPPreset {
    std::string name;                  // "Spotify", "Apple Music", etc.
    float targetLufs{-14.0f};          // Loudness normalization target
    float highCutHz{20000.0f};         // High frequency cutoff
    float lowCutHz{20.0f};             // Low frequency cutoff
    
    // Equalization
    float bassBoostDb{0.0f};           // Low shelf boost/cut at 100Hz
    float trebleShelfDb{0.0f};         // High shelf boost/cut at 10kHz
    
    // Dynamics
    float compressionRatio{1.0f};      // 1.0 = no compression
    float limiterCeilingDb{-1.0f};     // True peak ceiling (Spotify = -1dB)
    float limiterAttackMs{5.0f};       // Limiter attack time
    float limiterReleaseMs{100.0f};    // Limiter release time
    
    // Lo-fi
    float lofiAmount{0.0f};            // 0.0 = clean, 1.0 = max degradation
    bool enabled{true};
    
    // Research-based Presets
    
    static AuditionDSPPreset Spotify() {
        // -14 LUFS, -1dB True Peak, Limiter on "Loud" setting (simulated here with slight compression)
        // 5ms attack, 100ms release limiter
        return {"Spotify", -14.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 2.0f, -1.0f, 5.0f, 100.0f, 0.0f, true};
    }
    
    static AuditionDSPPreset AppleMusic() {
        // -16 LUFS (Sound Check), cleaner signal path, ALAC (lossless)
        return {"Apple Music", -16.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 1.0f, -0.1f, 10.0f, 100.0f, 0.0f, true};
    }
    
    static AuditionDSPPreset YouTube() {
        // -14 LUFS, Opus compression artifacts (simulated via slight lofi/treble cut)
        // Often slightly compressed dynamics
        return {"YouTube", -14.0f, 16000.0f, 30.0f, 0.0f, -0.5f, 1.5f, -1.0f, 10.0f, 200.0f, 0.05f, true};
    }
    
    static AuditionDSPPreset SoundCloud() {
        // -14 LUFS, often 128kbps MP3 legacy / 64kbps Opus (more artifacts)
        // Lofi set to 0.12 to give mild bit reduction (approx 14.8 bits) without decimating sample rate
        return {"SoundCloud", -14.0f, 15000.0f, 40.0f, 1.0f, -1.0f, 2.0f, -1.0f, 10.0f, 100.0f, 0.12f, true};
    }
    
    static AuditionDSPPreset CarSpeakers() {
        // Harman curve-ish: Bass boost (+4dB), treble roll-off (-3dB), high noise floor (compression)
        // Bass boost centered around 50-60Hz (simulated with shelf for now)
        return {"Car Speakers", -12.0f, 14000.0f, 45.0f, 4.0f, -3.0f, 3.0f, -6.0f, 20.0f, 300.0f, 0.0f, true};
    }
    
    static AuditionDSPPreset AirPods() {
        // Adaptive EQ simulation: Mild bass lift (+2dB), mild treble roll-off (-1dB), clean dynamics
        return {"AirPods Pro", -16.0f, 18000.0f, 20.0f, 2.0f, -1.5f, 1.2f, -0.5f, 10.0f, 50.0f, 0.0f, true};
    }
    
    static AuditionDSPPreset Bypass() {
        return {"Studio Reference", 0.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false};
    }
};

/**
 * @brief Audition Mode playback engine
 * 
 * Provides a simple, focused playback experience separate from the main DAW.
 * Designed for album listening, reference track comparison, and DSP preview.
 */
class AuditionEngine {
public:
    AuditionEngine();
    ~AuditionEngine();
    
    // Non-copyable
    AuditionEngine(const AuditionEngine&) = delete;
    AuditionEngine& operator=(const AuditionEngine&) = delete;
    
    // === Queue Management ===
    
    /// Add a file to the queue
    void addToQueue(const std::string& filePath, bool isReference = false);
    
    /// Add a timeline track to the queue (renders to temp file)
    void addTimelineTrack(uint32_t trackId, const std::string& trackName);
    
    /// Clear the entire queue
    void clearQueue();
    
    /// Get current queue
    const std::vector<AuditionQueueItem>& getQueue() const { return m_queue; }
    
    /// Get currently playing item (copy for thread safety)
    std::optional<AuditionQueueItem> getCurrentItem() const;
    
    /// Move to next/previous track
    void nextTrack();
    void previousTrack();
    
    /// Jump to specific queue index
    void jumpToTrack(size_t index);
    
    // === Transport Control ===
    
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    
    bool isPlaying() const { return m_isPlaying.load(); }
    
    /// Seek to position (0.0 - 1.0 normalized)
    void seekNormalized(double position);
    
    /// Seek to position in seconds
    void seekSeconds(double seconds);
    
    /// Get current position (0.0 - 1.0 normalized)
    double getPositionNormalized() const;
    
    /// Get current position in seconds
    double getPositionSeconds() const;
    
    /// Get current track duration
    double getDurationSeconds() const;

    /// Get current source (for visualization)
    std::shared_ptr<ClipSource> getCurrentSource() const { return m_currentSource; }
    
    // === DSP Chain ===
    
    /// Set active DSP preset
    void setDSPPreset(const AuditionDSPPreset& preset);
    
    /// Get current DSP preset
    const AuditionDSPPreset& getDSPPreset() const { return m_currentPreset; }
    
    /// Toggle A/B comparison (wet/dry)
    void setABMode(bool wetMode) { m_abWetMode.store(wetMode); }
    bool isABWetMode() const { return m_abWetMode.load(); }
    
    // === Playback Options ===
    
    void setShuffle(bool enabled) { m_shuffle.store(enabled); }
    bool isShuffle() const { return m_shuffle.load(); }
    
    enum class RepeatMode { Off, One, All };
    void setRepeatMode(RepeatMode mode) { m_repeatMode = mode; }
    RepeatMode getRepeatMode() const { return m_repeatMode; }
    
    void setCrossfadeMs(uint32_t ms) { m_crossfadeMs.store(ms); }
    uint32_t getCrossfadeMs() const { return m_crossfadeMs.load(); }
    
    // Volume Control (0.0 - 1.0)
    void setVolume(float volume) { m_volume.store(volume); }
    float getVolume() const { return m_volume.load(); }
    
    // === Callbacks ===
    
    void setOnTrackChanged(std::function<void(const AuditionQueueItem&)> cb) {
        m_onTrackChanged = std::move(cb);
    }
    
    void setOnPlaybackStateChanged(std::function<void(bool isPlaying)> cb) {
        m_onPlaybackStateChanged = std::move(cb);
    }
    
    void setOnPositionChanged(std::function<void(double seconds)> cb) {
        m_onPositionChanged = std::move(cb);
    }

    void setOnQueueUpdated(std::function<void()> cb) {
        m_onQueueUpdated = std::move(cb);
    }
    
    // === Audio Processing ===
    
    /// Process audio block (called from audio thread)
    void processBlock(float* output, uint32_t numFrames, uint32_t numChannels);
    
    /// Set output sample rate
    void setSampleRate(double sampleRate) { m_sampleRate.store(sampleRate); }

private:
    // Queue
    std::vector<AuditionQueueItem> m_queue;
    int32_t m_currentIndex{-1};
    mutable std::mutex m_queueMutex;
    
    // Playback state
    std::atomic<bool> m_isPlaying{false};
    std::atomic<double> m_positionSeconds{0.0};
    std::atomic<double> m_sampleRate{48000.0};
    
    // DSP
    AuditionDSPPreset m_currentPreset{AuditionDSPPreset::Bypass()};
    std::atomic<bool> m_abWetMode{false};  // Default to DRY (no effects)
    
    // Internal DSP State (for filters/dynamics)
    struct DSPState {
        // Shelving Filters (Biquad Direct Form 1)
        struct Biquad {
            float z1_L{0}, z2_L{0};
            float z1_R{0}, z2_R{0};
            void reset() { z1_L = z2_L = z1_R = z2_R = 0; }
        } lowShelf, highShelf;
        
        // 1-pole Filters
        float lpStateL{0}, lpStateR{0};
        float hpStateL{0}, hpStateR{0};
        
        // Dynamics
        float envFollower{0};
        
        // Lofi
        float lofiHoldL{0}, lofiHoldR{0};
        int lofiCounter{0};
        
        void reset() {
            lowShelf.reset();
            highShelf.reset();
            lpStateL = lpStateR = 0;
            hpStateL = hpStateR = 0;
            envFollower = 0;
            lofiHoldL = lofiHoldR = 0;
            lofiCounter = 0;
        }
    } m_dspState;
    
    // Options
    std::atomic<bool> m_shuffle{false};
    RepeatMode m_repeatMode{RepeatMode::Off};
    std::atomic<uint32_t> m_crossfadeMs{500};
    std::atomic<float> m_volume{1.0f};
    
    // Audio source
    std::shared_ptr<ClipSource> m_currentSource;
    
    // Callbacks
    std::function<void(const AuditionQueueItem&)> m_onTrackChanged;
    std::function<void(bool)> m_onPlaybackStateChanged;
    std::function<void(double)> m_onPositionChanged;
    std::function<void()> m_onQueueUpdated;
    
    // Internal helpers
    void loadCurrentTrack();
    void applyDSPChain(float* buffer, uint32_t numFrames, uint32_t numChannels);
    void notifyTrackChanged();
};

} // namespace Audio
} // namespace Nomad
