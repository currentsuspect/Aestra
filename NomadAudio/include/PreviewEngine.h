// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioBuffer.h"
#include <atomic>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <vector>
#include <functional>
#include <chrono>
#include "NomadThreading.h" // For LockFreeRingBuffer

namespace Nomad {
namespace Audio {

// Forward declaration
class SamplePool;

// Result codes for preview requests
enum class PreviewResult {
    Success,      // Played immediately (cached)
    Pending,      // Queued for async decode
    Failed        // Failed to load
};

// Represents an active preview voice (one-shot playback)
struct PreviewVoice {
    std::shared_ptr<AudioBuffer> buffer; // The audio data (shared ownership)
    std::string path;                    // Source file path

    // Playback parameters
    double sampleRate{48000.0};
    uint32_t channels{2};
    float gain{1.0f};
    double durationSeconds{0.0};
    double maxPlaySeconds{0.0}; // 0.0 = full duration

    // Runtime state (accessed by audio thread)
    double phaseFrames{0.0};    // Current playback position in frames
    double elapsedSeconds{0.0}; // Wall-clock time elapsed

    // Atomic flags for RT-safe control
    std::atomic<bool> stopRequested{false};
    std::atomic<double> seekRequestSeconds{-1.0}; // -1.0 = no seek, >= 0.0 = seek target
    std::atomic<bool> playing{false};
    std::atomic<bool> bufferReady{false}; // Set to true once async decode finishes

    // Fading state (for smooth start/stop)
    bool fadeOutActive{false};
    double fadeInPos{0.0};
    double fadeOutPos{0.0};
};

// Job for background worker thread
struct DecodeJob {
    std::string path;
    std::shared_ptr<PreviewVoice> voice;
    uint64_t generation{0}; // For cancellation
};

// Zombie voice (old voice waiting to be destroyed)
struct ZombieVoice {
    std::shared_ptr<PreviewVoice> voice;
    std::chrono::steady_clock::time_point deathTime;
};

/**
 * @brief Handles one-shot audio preview playback (browser, samples).
 *
 * Features:
 * - Async decoding (non-blocking for UI)
 * - Lock-free audio processing callback (using atomic raw pointer swap + deferred garbage collection)
 * - Automatic sample rate conversion (linear/cubic)
 * - Cancellation of stale requests
 * - Smooth fade-in/out to prevent clicks
 */
class PreviewEngine {
public:
    PreviewEngine();
    ~PreviewEngine();

    /**
     * @brief Request playback of a file.
     *
     * If file is cached, returns Success and plays immediately.
     * If not cached, returns Pending and starts async decode.
     *
     * @param path Path to audio file
     * @param gainDb Playback volume in dB (default -6dB)
     * @param maxSeconds Max duration to play (0.0 = full file)
     */
    PreviewResult play(const std::string& path, float gainDb = -6.0f, double maxSeconds = 0.0);

    /**
     * @brief Stop current playback with fade-out.
     */
    void stop();

    /**
     * @brief Seek to absolute position in seconds.
     */
    void seek(double seconds);

    /**
     * @brief Process audio block (call from audio thread).
     * Adds preview audio to the buffer (mixing).
     */
    void process(float* interleavedOutput, uint32_t numFrames);

    /**
     * @brief Set output sample rate (call before processing).
     */
    void setOutputSampleRate(double sr);

    /**
     * @brief Check if currently playing (or loading).
     */
    bool isPlaying() const;

    /**
     * @brief Check if buffer is ready for playback.
     */
    bool isBufferReady() const;

    /**
     * @brief Set callback for when playback finishes naturally.
     */
    void setOnComplete(std::function<void(const std::string& path)> callback);

    /**
     * @brief Set global volume for all previews.
     */
    void setGlobalPreviewVolume(float gainDb);
    float getGlobalPreviewVolume() const;

    /**
     * @brief Get current playback position in seconds.
     */
    double getPlaybackPosition() const;

private:
    // Internal helper to start playback once buffer is available
    PreviewResult startVoiceWithBuffer(std::shared_ptr<AudioBuffer> buffer,
                                       const std::string& path,
                                       float gainDb, double maxSeconds);

    // Helper to queue async decode
    void decodeAsync(const std::string& path, std::shared_ptr<PreviewVoice> voice);

    // Helper to load buffer (synchronous)
    std::shared_ptr<AudioBuffer> loadBuffer(const std::string& path, uint32_t& sr, uint32_t& ch);

    // Worker thread function
    void workerLoop();

    // Helper: db to linear
    float dbToLinear(float db) const;

    // --- State ---

    // The active voice currently playing.
    // In audio thread: accessed via raw pointer from atomic load for lock-free access.
    std::atomic<PreviewVoice*> m_activeVoiceRaw{nullptr};
    
    // Owner of the active voice (keeps it alive).
    // Modified only on UI thread (or worker thread when decoding finishes).
    std::shared_ptr<PreviewVoice> m_activeVoiceOwner;

    // List of old voices waiting to be destroyed with timestamp.
    // Cleared by worker thread.
    std::vector<ZombieVoice> m_zombies;

    // Output configuration
    std::atomic<double> m_outputSampleRate;
    std::atomic<float> m_globalGainDb;
    
    // Async worker state
    std::thread m_workerThread;
    std::mutex m_workerMutex;
    std::condition_variable m_workerCV;
    std::optional<DecodeJob> m_pendingJob;
    std::atomic<uint64_t> m_decodeGeneration;
    bool m_workerRunning;
    
    // Callback
    std::function<void(const std::string&)> m_onComplete;
};

} // namespace Audio
} // namespace Nomad
