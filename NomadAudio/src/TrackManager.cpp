// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "TrackManager.h"
#include "NomadLog.h"
#include <algorithm>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Nomad {
namespace Audio {

//==============================================================================
// AudioThreadPool Implementation
//==============================================================================

AudioThreadPool::AudioThreadPool(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this, i] { workerThread(); });
        
        // Set thread priority to real-time (platform-specific)
        #ifdef _WIN32
        SetThreadPriority(m_workers.back().native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
        #endif
    }
    
    Log::info("AudioThreadPool created with " + std::to_string(numThreads) + " threads");
}

AudioThreadPool::~AudioThreadPool() {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    m_condition.notify_all();
    
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    Log::info("AudioThreadPool destroyed");
}

void AudioThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_tasks.push(std::move(task));
        m_activeTasks.fetch_add(1);
    }
    m_condition.notify_one();
}

void AudioThreadPool::waitForCompletion() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_completionCondition.wait(lock, [this] { 
        return m_tasks.empty() && m_activeTasks.load() == 0; 
    });
}

void AudioThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this] { 
                return m_stop || !m_tasks.empty(); 
            });
            
            if (m_stop && m_tasks.empty()) {
                return;
            }
            
            if (!m_tasks.empty()) {
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
        }
        
        if (task) {
            task();
            
            size_t remaining = m_activeTasks.fetch_sub(1) - 1;
            if (remaining == 0) {
                m_completionCondition.notify_all();
            }
        }
    }
}

//==============================================================================
// TrackManager Implementation
//==============================================================================

TrackManager::TrackManager() {
    // Create thread pool with optimal thread count
    // Use hardware concurrency minus 1 (leave one core for OS/UI)
    // Minimum 2 threads, maximum 8 threads for real-time audio
    size_t hwThreads = std::thread::hardware_concurrency();
    size_t audioThreads = std::max(size_t(2), std::min(size_t(8), hwThreads > 0 ? hwThreads - 1 : 4));
    
    m_threadPool = std::make_unique<AudioThreadPool>(audioThreads);
    
    Log::info("TrackManager created with " + std::to_string(audioThreads) + " audio processing threads");
}

TrackManager::~TrackManager() {
    clearAllTracks();
    m_threadPool.reset();
    Log::info("TrackManager destroyed");
}

void TrackManager::setThreadCount(size_t count) {
    // Clamp between 1 and 16 threads
    count = std::max(size_t(1), std::min(size_t(16), count));
    
    // Recreate thread pool with new count
    m_threadPool.reset();
    m_threadPool = std::make_unique<AudioThreadPool>(count);
    
    Log::info("TrackManager thread count set to: " + std::to_string(count));
}

// Track Management
std::shared_ptr<Track> TrackManager::addTrack(const std::string& name) {
    std::string trackName = name.empty() ? generateTrackName() : name;
    uint32_t trackId = m_nextTrackId.fetch_add(1);

    auto track = std::make_shared<Track>(trackName, trackId);
    m_tracks.push_back(track);

    Log::info("Added track: " + trackName + " (ID: " + std::to_string(trackId) + ")");
    return track;
}

std::shared_ptr<Track> TrackManager::getTrack(size_t index) {
    if (index < m_tracks.size()) {
        return m_tracks[index];
    }
    return nullptr;
}

std::shared_ptr<const Track> TrackManager::getTrack(size_t index) const {
    if (index < m_tracks.size()) {
        return m_tracks[index];
    }
    return nullptr;
}

void TrackManager::removeTrack(size_t index) {
    if (index < m_tracks.size()) {
        std::string trackName = m_tracks[index]->getName();
        m_tracks.erase(m_tracks.begin() + index);

        Log::info("Removed track: " + trackName);
    }
}

void TrackManager::clearAllTracks() {
    for (auto& track : m_tracks) {
        if (track->isRecording()) {
            track->stopRecording();
        }
    }

    m_tracks.clear();
    Log::info("Cleared all tracks");
}

// Transport Control
void TrackManager::play() {
    m_isPlaying.store(true);  // CRITICAL: Set to true even when resuming from pause
    m_isRecording.store(false);

    for (auto& track : m_tracks) {
        // Skip system tracks (preview, test sound) - they manage their own playback
        if (!track->isSystemTrack()) {
            track->play();
        }
    }

    Log::info("TrackManager: Play started");
}

void TrackManager::pause() {
    m_isPlaying.store(false);

    for (auto& track : m_tracks) {
        // Skip system tracks (preview, test sound) - they manage their own playback
        if (!track->isSystemTrack()) {
            track->pause();
        }
    }

    Log::info("TrackManager: Paused");
}

void TrackManager::stop() {
    m_isPlaying.store(false);
    m_isRecording.store(false);
    m_positionSeconds.store(0.0);

    for (auto& track : m_tracks) {
        // Skip system tracks (preview, test sound) - they manage their own playback
        if (!track->isSystemTrack()) {
            track->stop();
        }
    }

    Log::info("TrackManager: Stopped");
}

void TrackManager::record() {
    // Toggle recording state
    bool wasRecording = m_isRecording.load();
    if (wasRecording) {
        // Stop recording
        m_isRecording.store(false);
        for (auto& track : m_tracks) {
            if (track->isRecording()) {
                track->stopRecording();
            }
        }
        Log::info("TrackManager: Recording stopped");
    } else {
        // Start recording on empty tracks
        m_isRecording.store(true);
        for (auto& track : m_tracks) {
            if (track->getState() == TrackState::Empty) {
                track->startRecording();
            }
        }
        Log::info("TrackManager: Recording started");
    }
}

// Position Control
void TrackManager::setPosition(double seconds) {
    m_positionSeconds.store(seconds);

    for (auto& track : m_tracks) {
        track->setPosition(seconds);
    }
}

double TrackManager::getTotalDuration() const {
    double maxDuration = 0.0;
    for (const auto& track : m_tracks) {
        maxDuration = std::max(maxDuration, track->getDuration());
    }
    return maxDuration;
}

// Audio Processing
void TrackManager::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime) {
    // Start timing
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Dispatch to single-threaded or multi-threaded implementation
    if (m_multiThreadingEnabled && m_threadPool && m_tracks.size() > 2) {
        // Use multi-threading for 3+ tracks
        processAudioMultiThreaded(outputBuffer, numFrames, streamTime);
    } else {
        // Use single-threaded for 1-2 tracks or when multi-threading is disabled
        processAudioSingleThreaded(outputBuffer, numFrames, streamTime);
    }
    
    // End timing and calculate load percentage
    auto endTime = std::chrono::high_resolution_clock::now();
    double processingTimeUs = std::chrono::duration<double, std::micro>(endTime - startTime).count();
    
    // Calculate available time for this buffer (in microseconds)
    // Assuming 44.1kHz sample rate (will work for other rates too)
    double availableTimeUs = (numFrames / 44100.0) * 1000000.0;
    
    // Calculate load percentage
    double loadPercent = (processingTimeUs / availableTimeUs) * 100.0;
    m_audioLoadPercent.store(loadPercent);
}

void TrackManager::processAudioSingleThreaded(float* outputBuffer, uint32_t numFrames, double streamTime) {
    if (!outputBuffer || numFrames == 0) {
        return;
    }

    // Check if any track is soloed (for exclusive solo behavior)
    bool anySoloed = false;
    std::string soloedTrackName;
    for (const auto& track : m_tracks) {
        if (track && track->isSoloed()) {
            anySoloed = true;
            soloedTrackName = track->getName();
            break;
        }
    }
    
    // Debug: Log solo state once every ~1 second (48000 samples / 512 frames = ~94 calls/sec, log every 100 calls)
    static int debugCounter = 0;
    if (++debugCounter >= 100) {
        debugCounter = 0;
        if (anySoloed) {
            Log::info("Solo active - only processing: " + soloedTrackName);
        }
    }

    // Output buffer should already be cleared by caller
    // Process each track - tracks will mix themselves into the output buffer
    for (const auto& track : m_tracks) {
        // Only process tracks that are playing
        // System tracks (preview) always process if playing
        // Regular tracks only process if transport is playing OR if they're explicitly playing
        if (track && track->isPlaying()) {
            // System tracks (preview, test sound) always play regardless of solo state
            bool isSystemTrack = track->isSystemTrack();
            
            // If any track is soloed, only process soloed tracks (unless track is muted)
            // Exception: System tracks always play
            if (anySoloed && !track->isSoloed() && !isSystemTrack) {
                continue; // Skip non-soloed tracks when solo is active
            }
            
            track->processAudio(outputBuffer, numFrames, streamTime);
        }
    }

    // Send master output to visualizer callback (if registered)
    // Extract stereo channels for VU meter display
    if (m_onAudioOutput) {
        // Assume stereo interleaved output: [L0, R0, L1, R1, L2, R2, ...]
        // We need to deinterleave for the visualizer
        std::vector<float> leftChannel(numFrames);
        std::vector<float> rightChannel(numFrames);
        
        for (uint32_t i = 0; i < numFrames; ++i) {
            leftChannel[i] = outputBuffer[i * 2];      // Left channel
            rightChannel[i] = outputBuffer[i * 2 + 1]; // Right channel
        }
        
        // Call visualizer callback with deinterleaved channels
        m_onAudioOutput(leftChannel.data(), rightChannel.data(), numFrames, 48000.0);
    }

    // Update position
    if (m_isPlaying.load()) {
        double currentPos = m_positionSeconds.load();
        double newPosition = currentPos + (numFrames / 48000.0);
        double maxDuration = getTotalDuration();
        
        // Check for loop boundary - reset to 0 if we've exceeded duration
        if (maxDuration > 0.0 && newPosition >= maxDuration) {
            // Loop back to start
            newPosition = 0.0;
            m_positionSeconds.store(0.0);
            
            // Reset all non-system tracks to start position
            for (auto& track : m_tracks) {
                if (!track->isSystemTrack()) {
                    track->setPosition(0.0);
                }
            }
            
            // Notify UI callback if set (for timer display update)
            if (m_onPositionUpdate) {
                m_onPositionUpdate(0.0);
            }
        } else {
            m_positionSeconds.store(newPosition);
        }
    }
}

void TrackManager::processAudioMultiThreaded(float* outputBuffer, uint32_t numFrames, double streamTime) {
    if (!outputBuffer || numFrames == 0 || !m_threadPool) {
        return;
    }
    
    // Resize per-track buffer storage if needed
    size_t bufferSize = numFrames * 2; // Stereo
    if (m_trackBuffers.size() != m_tracks.size()) {
        m_trackBuffers.resize(m_tracks.size());
        for (auto& buffer : m_trackBuffers) {
            buffer.resize(bufferSize, 0.0f);
        }
    }
    
    // Clear all track buffers
    for (auto& buffer : m_trackBuffers) {
        std::memset(buffer.data(), 0, bufferSize * sizeof(float));
    }
    
    // Check if any track is soloed (for exclusive solo behavior)
    bool anySoloed = false;
    for (const auto& track : m_tracks) {
        if (track && track->isSoloed()) {
            anySoloed = true;
            break;
        }
    }
    
    // Process each track in parallel into separate buffers
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        const auto& track = m_tracks[i];
        
        // Only process playing tracks
        if (track && track->isPlaying()) {
            // System tracks (preview, test sound) always play regardless of solo state
            bool isSystemTrack = track->isSystemTrack();
            
            // If any track is soloed, only process soloed tracks
            // Exception: System tracks always play
            // Mute always takes priority over solo
            if (anySoloed && !track->isSoloed() && !isSystemTrack) {
                continue; // Skip non-soloed tracks when solo is active
            }
            
            // Submit track processing task to thread pool
            m_threadPool->enqueue([track, &buffer = m_trackBuffers[i], numFrames, streamTime]() {
                track->processAudio(buffer.data(), numFrames, streamTime);
            });
        }
    }
    
    // Wait for all tracks to finish processing
    m_threadPool->waitForCompletion();
    
    // Mix all track buffers into output buffer (lock-free summation)
    // Zero the output buffer first
    std::memset(outputBuffer, 0, bufferSize * sizeof(float));
    
    // Sum all track buffers
    for (const auto& buffer : m_trackBuffers) {
        for (size_t i = 0; i < bufferSize; ++i) {
            outputBuffer[i] += buffer[i];
        }
    }
    
    // Send master output to visualizer callback (if registered)
    if (m_onAudioOutput) {
        // Deinterleave stereo channels for visualizer
        std::vector<float> leftChannel(numFrames);
        std::vector<float> rightChannel(numFrames);
        
        for (uint32_t i = 0; i < numFrames; ++i) {
            leftChannel[i] = outputBuffer[i * 2];
            rightChannel[i] = outputBuffer[i * 2 + 1];
        }
        
        m_onAudioOutput(leftChannel.data(), rightChannel.data(), numFrames, 48000.0);
    }
    
    // Update position
    if (m_isPlaying.load()) {
        double currentPos = m_positionSeconds.load();
        double newPosition = currentPos + (numFrames / 48000.0);
        double maxDuration = getTotalDuration();
        
        if (maxDuration > 0.0 && newPosition >= maxDuration) {
            newPosition = 0.0;
            m_positionSeconds.store(0.0);
            
            for (auto& track : m_tracks) {
                if (!track->isSystemTrack()) {
                    track->setPosition(0.0);
                }
            }
            
            if (m_onPositionUpdate) {
                m_onPositionUpdate(0.0);
            }
        } else {
            m_positionSeconds.store(newPosition);
        }
    }
}

void TrackManager::updateMixer() {
    Log::info("updateMixer called, tracks count: " + std::to_string(m_tracks.size()));
    // Update mixer with current track states
    // This would integrate with the main DAW's mixer system
    for (const auto& track : m_tracks) {
        if (track && track->getMixerBus()) {
            // Update mixer bus parameters from track
            track->getMixerBus()->setGain(track->getVolume());
            track->getMixerBus()->setPan(track->getPan());
            track->getMixerBus()->setMute(track->isMuted());
            track->getMixerBus()->setSolo(track->isSoloed());
        }
    }
}

void TrackManager::clearAllSolos() {
    for (auto& track : m_tracks) {
        track->setSolo(false);
    }
    Log::info("Cleared all solos");
}

std::string TrackManager::generateTrackName() const {
    // Generate track name based on the number of non-preview tracks
    // Since we start with 1 Preview track, we need to subtract 1
    uint32_t nextId = m_nextTrackId.load();
    uint32_t displayNumber = nextId - 1; // Account for Preview track
    return "Track " + std::to_string(displayNumber);
}

} // namespace Audio
} // namespace Nomad
