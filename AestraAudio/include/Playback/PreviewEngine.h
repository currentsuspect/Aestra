// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../IO/SamplePool.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Aestra {
namespace Audio {

enum class PreviewResult {
    Success, // Playback started immediately (cache hit)
    Pending, // Decode in progress, playback will start when ready
    Failed   // Decode error or invalid file
};

/**
 * @brief Handles file auditioning and previewing.
 *
 * Manages asynchronous decoding, sample rate conversion, and playback
 * of audio files for browser preview and scrubbing.
 */
class PreviewEngine {
public:
    PreviewEngine();
    ~PreviewEngine();

    // Non-copyable
    PreviewEngine(const PreviewEngine&) = delete;
    PreviewEngine& operator=(const PreviewEngine&) = delete;

    PreviewResult play(const std::string& path, float gainDb = -6.0f, double maxSeconds = 30.0,
                       float playbackRate = 1.0f);
    void stop();
    void seek(double seconds); // New seek method
    void setOutputSampleRate(double sr);
    void process(float* interleavedOutput, uint32_t numFrames);

    /**
     * @brief RT-safe preview mix into the engine's interleaved output buffer.
     *
     * Called from the audio callback. Mixes decoded preview samples directly
     * into the provided output buffer with no allocation or locking.
     *
     * @param interleavedOutput Destination buffer (interleaved, already sized).
     * @param numFrames         Number of frames to render.
     * @param outputChannels    Channel count of the destination buffer.
     */
    void processRealtime(float* interleavedOutput, uint32_t numFrames, uint32_t outputChannels);
    bool isPlaying() const;
    bool isBufferReady() const; // True when buffer is decoded and ready for playback
    void setOnComplete(std::function<void(const std::string& path)> callback);
    void setGlobalPreviewVolume(float gainDb);
    float getGlobalPreviewVolume() const;
    double getPlaybackPosition() const; // New method
    double getDuration() const;
    void handleDeferredCompletion();
    float getCurrentPlaybackRate() const { return m_playbackRate.load(std::memory_order_relaxed); }

private:
    struct PreviewVoice {
        std::shared_ptr<AudioBuffer> buffer;
        std::string path;
        double phaseFrames{0.0};
        double sampleRate{48000.0};
        uint32_t channels{2};
        float gain{0.5f};
        double durationSeconds{0.0};
        double maxPlaySeconds{0.0};
        double elapsedSeconds{0.0};
        double fadeInPos{0.0};
        double fadeOutPos{0.0};
        std::atomic<bool> stopRequested{false};
        std::atomic<double> seekRequestSeconds{-1.0}; // -1.0 = no seek
        std::atomic<bool> fadeOutActive{false};
        std::atomic<bool> playing{false};
        std::atomic<bool> bufferReady{false}; // True when buffer is decoded and ready
    };

    std::shared_ptr<AudioBuffer> loadBuffer(const std::string& path, uint32_t& sampleRate, uint32_t& channels);
    void downmixToStereo(std::vector<float>& data, uint32_t inChannels);
    float dbToLinear(float db) const;

    // Async decode support
    void decodeAsync(const std::string& path, std::shared_ptr<PreviewVoice> voice);
    PreviewResult startVoiceWithBuffer(std::shared_ptr<AudioBuffer> buffer, const std::string& path, float gainDb,
                                       double maxSeconds);

    std::shared_ptr<PreviewVoice> m_activeVoice;
    std::atomic<double> m_outputSampleRate;
    std::atomic<float> m_globalGainDb;
    std::atomic<float> m_playbackRate{1.0f};
    std::function<void(const std::string&)> m_onComplete;

    // Deferred completion (audio thread -> main thread)
    std::atomic<bool> m_completionPending{false};
    std::atomic<PreviewVoice*> m_completedVoice{nullptr};
    std::string m_completedPathStr;
    std::mutex m_completedPathMutex;

    // Decode Worker Thread
    // Persistent thread to handle decode requests without spawn overhead
    struct DecodeJob {
        std::string path;
        std::shared_ptr<PreviewVoice> voice;
        uint64_t generation;
    };

    std::thread m_workerThread;
    std::mutex m_workerMutex;
    std::condition_variable m_workerCV;
    std::optional<DecodeJob> m_pendingJob;
    std::atomic<uint64_t> m_decodeGeneration{0};
    bool m_workerRunning{true};

    void workerLoop();
};

} // namespace Audio
} // namespace Aestra
