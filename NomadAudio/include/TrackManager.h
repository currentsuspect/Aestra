// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Track.h"
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <unordered_map>

namespace Nomad {
namespace Audio {

/**
 * @brief Thread pool for parallel audio processing
 * 
 * Distributes track processing across multiple CPU cores for reduced latency
 * Uses lock-free design and thread affinity for real-time performance
 */
class AudioThreadPool {
public:
    AudioThreadPool(size_t numThreads);
    ~AudioThreadPool();
    
    // Submit a task to the thread pool
    void enqueue(std::function<void()> task);
    
    // Wait for all tasks to complete
    void waitForCompletion();
    
    // Get number of worker threads
    size_t getThreadCount() const { return m_workers.size(); }
    
private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::condition_variable m_completionCondition;
    
    std::atomic<bool> m_stop{false};
    std::atomic<size_t> m_activeTasks{0};
    
    void workerThread();
};

/**
 * @brief Manages multiple audio tracks for the DAW
 *
 * Coordinates playback, recording, and mixing of multiple tracks.
 * Provides high-level DAW functionality like transport control,
 * track management, and audio routing.
 * 
 * MULTI-THREADING:
 * - Processes tracks in parallel using thread pool
 * - Distributes CPU load across all cores
 * - Lock-free audio buffer mixing
 * - Real-time thread priorities
 */
class TrackManager {
public:
    TrackManager();
    ~TrackManager();

    // Track Management
    std::shared_ptr<Track> addTrack(const std::string& name = "");
    void addExistingTrack(std::shared_ptr<Track> track);  // Add an already-created track
    std::shared_ptr<Track> getTrack(size_t index);
    std::shared_ptr<const Track> getTrack(size_t index) const;
    size_t getTrackCount() const { return m_tracks.size(); }

    void removeTrack(size_t index);
    void clearAllTracks();

    // Transport Control
    void play();
    void pause();
    void stop();
    void record();

    bool isPlaying() const { return m_isPlaying.load(); }
    bool isRecording() const { return m_isRecording.load(); }

    // Position Control
    void setPosition(double seconds);
    // RT-authoritative position sync (does not emit engine commands).
    void syncPositionFromEngine(double seconds);
    double getPosition() const { return m_positionSeconds.load(); }

    double getTotalDuration() const;
    // Max extent considering track start offsets
    double getMaxTimelineExtent() const;
    
    // Clip manipulation
    bool moveClipToTrack(size_t fromIndex, size_t toIndex);
    std::shared_ptr<Track> sliceClip(size_t trackIndex, double sliceTimeSeconds);
    bool moveClipWithinTrack(size_t trackIndex, double newStartSeconds);
    
    // Position update callback (called during playback to update transport UI)
    void setOnPositionUpdate(std::function<void(double)> callback) { m_onPositionUpdate = callback; }

    // Audio output callback (called after processing to update VU meters, visualizers, etc.)
    // Parameters: leftChannel, rightChannel, numSamples, sampleRate
    void setOnAudioOutput(std::function<void(const float*, const float*, size_t, double)> callback) { 
        m_onAudioOutput = callback; 
    }

    // Audio Processing
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime);
    void setOutputSampleRate(double sampleRate);
    double getOutputSampleRate() const { return m_outputSampleRate.load(); }
    
    // Multi-threading control
    void setMultiThreadingEnabled(bool enabled) { m_multiThreadingEnabled = enabled; }
    bool isMultiThreadingEnabled() const { return m_multiThreadingEnabled; }
    
    void setThreadCount(size_t count);
    size_t getThreadCount() const { return m_threadPool ? m_threadPool->getThreadCount() : 1; }

    // Connect a command sink for RT updates (pushed from tracks)
    void setCommandSink(std::function<void(const AudioQueueCommand&)> sink) { m_commandSink = std::move(sink); }

    // Performance monitoring
    double getAudioLoadPercent() const { return m_audioLoadPercent.load(); }

    // Mixer Integration
    void updateMixer();

    // Solo/Mute Management
    void clearAllSolos();

    // Graph rebuild hint
    void markGraphDirty() { m_graphDirty.store(true, std::memory_order_release); }
    bool consumeGraphDirty() { return m_graphDirty.exchange(false, std::memory_order_acq_rel); }

    // Track index mapping
    uint32_t getCompactIndex(uint32_t trackId) const;

private:
    // Track collection
    std::vector<std::shared_ptr<Track>> m_tracks;
    mutable std::mutex m_trackMutex; // Guards m_tracks and m_trackBuffers against concurrent mutation

    // Transport state
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isRecording{false};
    std::atomic<double> m_positionSeconds{0.0};

    // Track ID counter
    std::atomic<uint32_t> m_nextTrackId{1};
    
    // Callbacks
    std::function<void(double)> m_onPositionUpdate;
    std::function<void(const float*, const float*, size_t, double)> m_onAudioOutput;
    
    // Multi-threading
    std::unique_ptr<AudioThreadPool> m_threadPool;
    std::atomic<bool> m_multiThreadingEnabled{true};  // Enabled by default
    
    // Performance tracking
    std::atomic<double> m_audioLoadPercent{0.0};
    // Scratch buffers reused for VU extraction to avoid per-callback allocations
    std::vector<float> m_leftScratch;
    std::vector<float> m_rightScratch;
    
    // Per-track temporary buffers for parallel processing
    std::vector<std::vector<float>> m_trackBuffers;
    
    // Project modification tracking for save prompts
    std::atomic<bool> m_isModified{false};
    std::atomic<bool> m_graphDirty{true};
    std::function<void(const AudioQueueCommand&)> m_commandSink;
    std::atomic<double> m_outputSampleRate{48000.0};
    std::unordered_map<uint32_t, uint32_t> m_idToIndex;

    // Track creation helper
    std::string generateTrackName() const;
    
    // Processing helpers
    void processAudioSingleThreaded(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate);
    void processAudioMultiThreaded(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate);
    
public:
    // Modified state tracking for graceful shutdown
    bool isModified() const { return m_isModified.load(); }
    void setModified(bool modified) { m_isModified.store(modified); }
    void markModified() { m_isModified.store(true); }
};

} // namespace Audio
} // namespace Nomad
