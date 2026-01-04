// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MixerChannel.h"
#include "PatternManager.h"
#include "UnitManager.h"
#include "PlaylistModel.h"
#include "PlaylistRuntimeSnapshot.h"
#include "PatternPlaybackEngine.h"
#include "TimelineClock.h"
#include "Commands/CommandHistory.h"

#include "ContinuousParamBuffer.h"
#include "MeterSnapshot.h"
#include "ChannelSlotMap.h"
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <functional>
#include <unordered_map>
#include <array>
#include <chrono>

namespace Nomad {
namespace Audio {

class SourceManager;

/**
 * @brief Thread pool for parallel audio processing
 */
class AudioThreadPool {
public:
    AudioThreadPool(size_t numThreads);
    ~AudioThreadPool();
    
    void enqueue(std::function<void()> task);
    void waitForCompletion();
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
 * @brief Manages MixerChannels and orchestrates real-time processing (v3.0)
 */
class TrackManager {
public:
    TrackManager();
    ~TrackManager();

    // Mixer Channel Management
    std::shared_ptr<MixerChannel> addChannel(const std::string& name = "");
    void addExistingChannel(std::shared_ptr<MixerChannel> channel);
    std::shared_ptr<MixerChannel> getChannel(size_t index);
    std::shared_ptr<const MixerChannel> getChannel(size_t index) const;
    size_t getChannelCount() const { return m_channels.size(); }
    void removeChannel(size_t index);
    void clearAllChannels();

    // Legacy aliases for UI compatibility (implemented in .cpp)
    size_t getTrackCount() const;
    std::shared_ptr<MixerChannel> getTrack(size_t index);
    void clearAllTracks();
    std::shared_ptr<MixerChannel> addTrack(const std::string& name = "");
    std::shared_ptr<MixerChannel> addTrack(const std::string& name, double); // Stub
    void addExistingTrack(std::shared_ptr<MixerChannel> channel);
    std::shared_ptr<MixerChannel> sliceClip(std::shared_ptr<MixerChannel>, double);
    std::shared_ptr<MixerChannel> sliceClip(size_t, double);





    // Snapshot of the current channelId->slot mapping (copy).
    ChannelSlotMap getChannelSlotMapSnapshot() const;


    // Access to global managers
    PatternManager& getPatternManager() { return m_patternManager; }
    const PatternManager& getPatternManager() const { return m_patternManager; }
    UnitManager& getUnitManager() { return m_unitManager; }
    const UnitManager& getUnitManager() const { return m_unitManager; }
    PlaylistModel& getPlaylistModel() { return m_playlistModel; }
    const PlaylistModel& getPlaylistModel() const { return m_playlistModel; }
    PlaylistSnapshotManager& getSnapshotManager() { return m_snapshotManager; }
    const PlaylistSnapshotManager& getSnapshotManager() const { return m_snapshotManager; }


    
    CommandHistory& getCommandHistory() { return m_commandHistory; }
    const CommandHistory& getCommandHistory() const { return m_commandHistory; }

    // Transport Control
    void play();
    void pause();
    void stop();
    void record();
    void finishRecording();
    void processInput(const float* inputBuffer, uint32_t numFrames); // Called from RT thread
    void setInputChannelCount(uint32_t count) { m_inputChannelCount = count; }
    void setLatencySamples(uint32_t samples);
    std::vector<std::string> getInputChannelNames() const;
    bool isPlaying() const { return m_isPlaying.load(); }
    bool isRecording() const { return m_isRecording.load(); }
    bool isRecordArmed() const { return m_isRecordArmed.load(); }
    void enableMetronome(bool enable);
    bool isMetronomeEnabled() const { return m_metronomeEnabled.load(); }
    
    // Pattern Playback Control
    void playPatternInArsenal(PatternID patternId); // Arsenal direct playback mode
    void stopArsenalPlayback();
    PatternPlaybackEngine& getPatternPlaybackEngine() { return *m_patternEngine; }
    TimelineClock& getTimelineClock() { return m_clock; }

    // Position Control
    void setPosition(double seconds);
    void syncPositionFromEngine(double seconds);
    double getPosition() const { return m_positionSeconds.load(); }
    double getUIPosition() const { return m_uiPositionSeconds.load(); }

    void setUserScrubbing(bool scrubbing) { m_userScrubbing.store(scrubbing, std::memory_order_release); }
    bool isUserScrubbing() const { return m_userScrubbing.load(std::memory_order_acquire); }

    // Callbacks
    void setOnPositionUpdate(std::function<void(double)> callback) { m_onPositionUpdate = callback; }
    void setOnAudioOutput(std::function<void(const float*, const float*, size_t, double)> callback) { 
        m_onAudioOutput = callback; 
    }

    // Audio Processing (Top-level entry point from AudioDeviceManager)
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, const SourceManager& sourceManager);

    void setOutputSampleRate(double rate);
    void setInputSampleRate(double rate) { m_inputSampleRate.store(rate); }
    

    double getOutputSampleRate() const { return m_outputSampleRate.load(); }
    
    // Multi-threading control
    void setMultiThreadingEnabled(bool enabled) { m_multiThreadingEnabled = enabled; }
    bool isMultiThreadingEnabled() const { return m_multiThreadingEnabled; }
    void setThreadCount(size_t count);
    size_t getThreadCount() const { return m_threadPool ? m_threadPool->getThreadCount() : 1; }

    void setCommandSink(std::function<void(const AudioQueueCommand&)> sink) { m_commandSink = std::move(sink); }
    double getAudioLoadPercent() const { return m_audioLoadPercent.load(); }

    // Mixer Integration
    void updateMixer();
    void clearAllSolos();
    void markGraphDirty() { m_graphDirty.store(true, std::memory_order_release); }
    bool consumeGraphDirty() { return m_graphDirty.exchange(false, std::memory_order_acq_rel); }
    
    /// Rebuild the playlist runtime snapshot and push to audio thread
    void rebuildAndPushSnapshot();

    std::string generateTrackName() const;
    double getMaxTimelineExtent() const;


    // Snapshot of current channels
    std::vector<std::shared_ptr<MixerChannel>> getChannelsSnapshot() const;
    std::shared_ptr<const ChannelSlotMap> getChannelSlotMapShared() const { return m_channelSlotMapOwned; }

    // Recording Access for UI
    bool getRecordingDataSnapshot(uint32_t trackId, std::vector<float>& outBuffer, double& outStartBeat);

    // Audio source management
    SourceManager& getSourceManager() { return m_sourceManager; }
    const SourceManager& getSourceManager() const { return m_sourceManager; }


    // RT-safe metering & params
    void setMeterSnapshots(std::shared_ptr<MeterSnapshotBuffer> snapshots) {
        m_meterSnapshotsOwned = snapshots;
        m_meterSnapshotsRaw = snapshots.get();
    }
    std::shared_ptr<MeterSnapshotBuffer> getMeterSnapshots() const { return m_meterSnapshotsOwned; }
    std::shared_ptr<ContinuousParamBuffer> getContinuousParams() const { return m_continuousParamsOwned; }

private:

private:
    // Core Managers
    SourceManager m_sourceManager;
    PatternManager m_patternManager;
    UnitManager m_unitManager;
    PlaylistModel m_playlistModel;
    PlaylistSnapshotManager m_snapshotManager;
    CommandHistory m_commandHistory;
    
    // Pattern Playback
    TimelineClock m_clock;
    std::unique_ptr<PatternPlaybackEngine> m_patternEngine;
    uint32_t m_arsenalInstanceId = 0; // Instance ID for Arsenal mode playback
    std::atomic<uint64_t> m_currentSampleFrame{0};


    
    // Mixer Channels
    std::vector<std::shared_ptr<MixerChannel>> m_channels;
    mutable std::mutex m_channelMutex;

    // Transport state
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isRecording{false};
    std::atomic<bool> m_isRecordArmed{false};
    std::atomic<double> m_positionSeconds{0.0};
    std::atomic<double> m_uiPositionSeconds{0.0}; // Thread-safe exchange for UI
    std::atomic<bool> m_userScrubbing{false};
    std::atomic<bool> m_metronomeEnabled{false};

    // Callbacks
    std::function<void(double)> m_onPositionUpdate;
    std::function<void(const float*, const float*, size_t, double)> m_onAudioOutput;
    
    // Multi-threading
    std::unique_ptr<AudioThreadPool> m_threadPool;
    std::atomic<bool> m_multiThreadingEnabled{true};
    
    // Performance tracking
    std::atomic<double> m_audioLoadPercent{0.0};
    std::vector<float> m_leftScratch;
    std::vector<float> m_rightScratch;
    
    // Per-channel temporary buffers for parallel processing
    std::vector<std::vector<float>> m_channelBuffers;
    
    std::atomic<bool> m_isModified{false};
    std::atomic<bool> m_graphDirty{true};
    std::function<void(const AudioQueueCommand&)> m_commandSink;
    std::atomic<double> m_outputSampleRate{48000.0};
    std::atomic<double> m_inputSampleRate{48000.0};
    
    std::shared_ptr<const ChannelSlotMap> m_channelSlotMapOwned;
    const ChannelSlotMap* m_channelSlotMapRaw{nullptr};
    
    void startRecordingProcess(); // Decoupled capture logic

public:
    const ChannelSlotMap* getChannelSlotMapRaw() const { return m_channelSlotMapRaw; }

private:
    
    // Channel ID Generation (v3.0.1)
    // 0 is reserved for master, so we start at 1.
    std::atomic<uint32_t> m_nextChannelId{1};

    // Meter snapshot buffer for RT-safe metering
    std::shared_ptr<MeterSnapshotBuffer> m_meterSnapshotsOwned;
    MeterSnapshotBuffer* m_meterSnapshotsRaw{nullptr};

    // Continuous params (ownership + RT-safe raw pointer)
    std::shared_ptr<ContinuousParamBuffer> m_continuousParamsOwned;
    ContinuousParamBuffer* m_continuousParamsRaw{nullptr};

    // Meter analysis state (audio thread)
    uint32_t m_meterAnalysisSampleRate{0};
    float m_meterLfCoeff{0.0f};
    std::array<float, MeterSnapshotBuffer::MAX_CHANNELS> m_meterLfStateL{};
    std::array<float, MeterSnapshotBuffer::MAX_CHANNELS> m_meterLfStateR{};

    // Internal helpers
    void rebuildChannelSlotMapLocked();
    void processAudioSingleThreaded(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate, const PlaylistRuntimeSnapshot* snapshot);

    void processAudioMultiThreaded(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate, const PlaylistRuntimeSnapshot* snapshot);
    
    // Recording Implementation
    struct RecordingBuffer {
        std::vector<float> data;
        double startBeat{0.0};
        uint32_t trackId{0};     // Which track is this for
        int inputChannelIndex{0}; // Which input channel (0=Left, 1=Right)
        PlaylistLaneID targetLane;
        bool active{false};
        
        void append(const float* input, uint32_t numFrames, uint32_t stride, int channelOffset) {
            size_t currentSize = data.size();
            data.resize(currentSize + numFrames);
            
            if (channelOffset < 0) {
                // Input "None" or invalid -> Record silence
                std::fill(data.begin() + currentSize, data.end(), 0.0f);
            } else {
                for (uint32_t i = 0; i < numFrames; ++i) {
                    // Interleaved input: [L R L R ...]
                    data[currentSize + i] = input[i * stride + channelOffset];
                }
            }
        }
    };
    
    std::vector<RecordingBuffer> m_recordingBuffers;
    std::mutex m_recordingMutex;
    
    // Wall-Clock Rate Detection
    std::chrono::high_resolution_clock::time_point m_recordingStartTime;
    uint64_t m_recordingFramesCaptured{0};
    
    // Monitoring
    std::mutex m_monitorMutex; // Protects access to monitor buffer
    std::vector<float> m_monitorBuffer;
    uint32_t m_inputChannelCount{0};
    uint32_t m_latencySamples{0};
    
public:
    bool isModified() const { return m_isModified.load(); }
    void setModified(bool modified) { m_isModified.store(modified); }
    void markModified() { m_isModified.store(true); }
};


} // namespace Audio
} // namespace Nomad
