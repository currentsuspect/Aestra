// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
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

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include "AestraAtomicSharedPtr.h"
#include "Core/RealtimeSharedSlot.h"
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ClipResampler.h"

namespace Aestra {
namespace Audio {

class ClipSource;
struct AudioBufferData;

/**
 * @brief Represents a single item in the audition queue
 */
struct AuditionQueueItem {
    std::string id;       // Unique identifier
    std::string filePath; // Path to audio file
    std::string title;    // Display title
    std::string artist;   // Display artist
    std::string album;    // Album name

    // Metadata
    double durationSeconds{0.0};       // Total duration
    std::vector<uint8_t> coverArtData; // Raw image data (JPEG/PNG)
    std::string coverArtMimeType;      // "image/jpeg", "image/png"

    bool isFromTimeline{false}; // True if this is from project tracks
    uint32_t sourceTrackId{0};  // If from timeline, which track
    bool isReference{false};    // True if external reference track

    // Playback state
    double lastPosition{0.0}; // Resume position
};

/**
 * @brief DSP simulation preset for audition playback
 */
struct AuditionDSPPreset {
    std::string name;          // "Streaming", "Car", etc. (descriptive names, never a real service: FD-26)
    float targetLufs{-14.0f};  // Loudness normalization target
    float highCutHz{20000.0f}; // High frequency cutoff
    float lowCutHz{20.0f};     // Low frequency cutoff

    // Equalization
    float bassBoostDb{0.0f};   // Low shelf boost/cut at 100Hz
    float trebleShelfDb{0.0f}; // High shelf boost/cut at 10kHz

    // Dynamics
    float compressionRatio{1.0f};   // 1.0 = no compression
    float limiterCeilingDb{-1.0f};  // True peak ceiling (a common streaming ceiling is -1dB)
    float limiterAttackMs{5.0f};    // Limiter attack time
    float limiterReleaseMs{100.0f}; // Limiter release time

    // Lo-fi
    float lofiAmount{0.0f}; // 0.0 = clean, 1.0 = max degradation
    bool enabled{true};

    // Research-based Presets

    static AuditionDSPPreset Streaming() {
        // Streaming: -14 LUFS, -1dB True Peak, Limiter on a "loud" setting (simulated here with slight compression)
        // 5ms attack, 100ms release limiter
        return {"Streaming", -14.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 2.0f, -1.0f, 5.0f, 100.0f, 0.0f, true};
    }

    static AuditionDSPPreset Lossless() {
        // Lossless: -16 LUFS, cleaner signal path
        return {"Lossless", -16.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 1.0f, -0.1f, 10.0f, 100.0f, 0.0f, true};
    }

    static AuditionDSPPreset VideoPlatform() {
        // Video platform: -14 LUFS, Opus compression artifacts (simulated via slight lofi/treble cut)
        // Often slightly compressed dynamics
        return {"Video platform", -14.0f, 16000.0f, 30.0f, 0.0f, -0.5f, 1.5f, -1.0f, 10.0f, 200.0f, 0.05f, true};
    }

    static AuditionDSPPreset CompressedStream() {
        // Compressed stream: -14 LUFS, low-bitrate artifacts
        // Lofi set to 0.12 to give mild bit reduction (approx 14.8 bits) without decimating sample rate
        return {"Compressed stream", -14.0f, 15000.0f, 40.0f, 1.0f, -1.0f, 2.0f, -1.0f, 10.0f, 100.0f, 0.12f, true};
    }

    static AuditionDSPPreset Car() {
        // Harman curve-ish: Bass boost (+4dB), treble roll-off (-3dB), high noise floor (compression)
        // Bass boost centered around 50-60Hz (simulated with shelf for now)
        return {"Car", -12.0f, 14000.0f, 45.0f, 4.0f, -3.0f, 3.0f, -6.0f, 20.0f, 300.0f, 0.0f, true};
    }

    static AuditionDSPPreset Earbuds() {
        // Earbuds: mild bass lift (+2dB), mild treble roll-off (-1dB), clean dynamics
        return {"Earbuds", -16.0f, 18000.0f, 20.0f, 2.0f, -1.5f, 1.2f, -0.5f, 10.0f, 50.0f, 0.0f, true};
    }

    static AuditionDSPPreset Bypass() {
        return {"Studio", 0.0f, 20000.0f, 20.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false};
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

    /// Remove one item from queue by index
    void removeFromQueue(size_t index);

    /// Reorder one item in queue (move source index to destination index)
    void moveQueueItem(size_t fromIndex, size_t toIndex);

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
    std::shared_ptr<ClipSource> getCurrentSource() const;

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
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onTrackChanged = std::move(cb);
    }

    void setOnPlaybackStateChanged(std::function<void(bool isPlaying)> cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onPlaybackStateChanged = std::move(cb);
    }

    void setOnPositionChanged(std::function<void(double seconds)> cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onPositionChanged = std::move(cb);
    }

    void setOnQueueUpdated(std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onQueueUpdated = std::move(cb);
    }

    // === Audio Processing ===

    /// Process audio block (called from audio thread)
    void processBlock(float* output, uint32_t numFrames, uint32_t numChannels);

    /// Handle deferred track transition (call from main thread)
    void handleDeferredTrackTransition();

    /// Set output sample rate
    void setSampleRate(double sampleRate) { m_sampleRate.store(sampleRate); }

    // === Synchronised dual playback (V81-E2, FD-26) ===
    //
    // A reference (side B) runs in lockstep with the mix (side A) once one is set: both are read every block at
    // the same position (plus an optional offset), so a switch is a short gain ramp between two live signals, never
    // a seek, and anything applied after the sum (the DSP chain) hears both alike. With no reference set, playback
    // is exactly what it was.

    enum class ListenSide { Mix, Reference };

    /// Play an in-memory source as the mix (a bounce, a render). Main thread; the audio thread picks it up atomically.
    void setMixSource(std::shared_ptr<ClipSource> source);
    /// Set the reference that runs alongside the mix. `offsetSeconds` is where the reference is when the mix is at 0
    /// (positive: the reference is ahead), so a switch can land in the matching section.
    void setReferenceSource(std::shared_ptr<ClipSource> source, double offsetSeconds = 0.0);
    void clearReferenceSource();
    bool hasReference() const { return m_referenceSource.load(std::memory_order_acquire) != nullptr; }
    void setReferenceOffsetSeconds(double seconds) { m_referenceOffsetSeconds.store(seconds); }
    double getReferenceOffsetSeconds() const { return m_referenceOffsetSeconds.load(); }

    /// Which side is heard. Takes effect as a ramp of getSwitchRampMs(), not a cut.
    void setListenSide(ListenSide side) { m_listenSide.store(side == ListenSide::Reference ? 1 : 0); }
    ListenSide getListenSide() const { return m_listenSide.load() == 1 ? ListenSide::Reference : ListenSide::Mix; }
    void setSwitchRampMs(float ms) { m_switchRampMs.store(ms < 1.0f ? 1.0f : ms); }
    float getSwitchRampMs() const { return m_switchRampMs.load(); }

    /// Level match: gain on each side in dB (reference minus mix from the analysis layer).
    void setMixGainDb(float db) { m_mixGain.store(dbToGain(db)); }
    void setReferenceGainDb(float db) { m_referenceGain.store(dbToGain(db)); }

private:
    void loadCurrentTrack(bool startPlayback);
    /// Internal: queue decode and publish track. Caller must NOT hold m_queueMutex.
    void loadCurrentTrackImpl(const std::string& filePath, double lastPosition, bool isTimeline,
                              const std::string& title, bool startPlayback);
    void decodeWorkerLoop();

    struct DecodeJob {
        std::string filePath;
        double lastPosition{0.0};
        bool isTimeline{false};
        std::string title;
        bool startPlayback{false};
        uint64_t generation{0};
    };

    void cancelPendingDecodes();

    /// Generation counter: incremented on each load request so stale decode results are discarded
    std::atomic<uint64_t> m_loadGeneration{0};
    std::thread m_decodeWorkerThread;
    std::mutex m_decodeWorkerMutex;
    std::condition_variable m_decodeWorkerCV;
    std::optional<DecodeJob> m_pendingDecodeJob;
    bool m_decodeWorkerRunning{true};

    // Queue (UI thread only - no RT access)
    std::vector<AuditionQueueItem> m_queue;
    int32_t m_currentIndex{-1};
    mutable std::mutex m_queueMutex;

    // Playback state
    std::atomic<bool> m_isPlaying{false};
    std::atomic<double> m_positionSeconds{0.0};
    std::atomic<double> m_sampleRate{48000.0};
    std::atomic<double> m_cachedDurationSeconds{0.0};
    std::atomic<bool> m_trackTransitionPending{false};

    // RT-safe source pointer (C++17 atomic shared_ptr via std::atomic_load/store)
    // NOTE: C++20+ has std::atomic<std::shared_ptr<T>> with member .load()/.store()
    //       C++17 requires free functions std::atomic_load_explicit/std::atomic_store_explicit
    AtomicSharedPtr<ClipSource> m_currentSource;
    // What processBlock reads. AtomicSharedPtr::load takes a pooled mutex in this C++17
    // build, and the copy it returned could become the last reference when the decoder
    // swapped the source mid-block, freeing a whole decoded file on the audio thread.
    // The slot pins instead of copying; replaced sources die on the publishing thread.
    RealtimeSharedSlot<ClipSource> m_rtCurrentSource;
    RealtimeSharedSlot<ClipSource> m_rtReferenceSource;
    void setCurrentSource(std::shared_ptr<ClipSource> source) {
        m_currentSource.store(source, std::memory_order_release);
        m_rtCurrentSource.publish(std::move(source));
    }
    void setReferenceSourcePtr(std::shared_ptr<ClipSource> source) {
        m_referenceSource.store(source, std::memory_order_release);
        m_rtReferenceSource.publish(std::move(source));
    }

    // DSP
    AuditionDSPPreset m_currentPreset{AuditionDSPPreset::Bypass()};
    std::atomic<bool> m_abWetMode{false}; // Default to DRY (no effects)

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
    ClipResampler m_resampler;

    // Options
    std::atomic<bool> m_shuffle{false};
    RepeatMode m_repeatMode{RepeatMode::Off};
    std::atomic<uint32_t> m_crossfadeMs{500};
    std::atomic<float> m_volume{1.0f};
    std::atomic<int> m_positionCallbackCounter{0};

    // Callbacks
    std::function<void(const AuditionQueueItem&)> m_onTrackChanged;
    std::function<void(bool)> m_onPlaybackStateChanged;
    std::function<void(double)> m_onPositionChanged;
    std::function<void()> m_onQueueUpdated;
    mutable std::mutex m_callbackMutex;

    // Dual playback (RT thread state is only touched in processBlock)
    static float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
    AtomicSharedPtr<ClipSource> m_referenceSource;
    std::atomic<double> m_referenceOffsetSeconds{0.0};
    std::atomic<int> m_listenSide{0};
    std::atomic<float> m_switchRampMs{10.0f};
    std::atomic<float> m_mixGain{1.0f};
    std::atomic<float> m_referenceGain{1.0f};
    float m_sideGainMix{1.0f};       // smoothed, audio thread only
    float m_sideGainReference{0.0f}; // smoothed, audio thread only
    std::vector<float> m_referenceScratch; // sized once; processBlock never allocates

    /// Read `numFrames` of one source starting at `startSeconds` into `out` (interleaved, `numChannels`).
    void renderSource(const AudioBufferData* buffer, double startSeconds, float* out, uint32_t numFrames,
                      uint32_t numChannels);

    // Internal helpers
    void loadCurrentTrack();
    void applyDSPChain(float* buffer, uint32_t numFrames, uint32_t numChannels);
    void notifyTrackChanged();
};

} // namespace Audio
} // namespace Aestra
