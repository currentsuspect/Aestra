#pragma once
#include "../Commands/CommandHistory.h"
#include "../Core/AudioCommandQueue.h"
#include "../Core/ChannelSlotMap.h"
#include "../DSP/ContinuousParamBuffer.h"
#include "MeterSnapshot.h"
#include "../Core/MixerChannel.h"
#include "PatternManager.h"
#include "../Playback/PatternPlaybackEngine.h"
#include "../Playback/TimelineClock.h"
#include "PlaylistModel.h"
#include "SourceManager.h"
#include "UnitManager.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
struct MeterSnapshots;

/**
 * @brief Track/Channel manager for the audio engine
 */
class TrackManager {
public:
    TrackManager() : m_patternPlaybackEngine(&m_timelineClock, &m_patternManager, &m_unitManager) {
        // Wire up playlist model to trigger audio graph rebuild when clips change
        m_playlistModel.setClipChangedCallback([this](const ClipInstanceID&) {
            m_graphDirty.store(true, std::memory_order_relaxed);
        });
    }

    /**
     * @brief Get the number of channels
     */
    size_t getChannelCount() const { return m_channels.size(); }

    /**
     * @brief Get a channel by index
     */
    MixerChannel* getChannel(size_t index) {
        if (index < m_channels.size()) {
            return m_channels[index].get();
        }
        return nullptr;
    }

    const MixerChannel* getChannel(size_t index) const {
        if (index < m_channels.size()) {
            return m_channels[index].get();
        }
        return nullptr;
    }

    /**
     * @brief Add a new channel
     */
    MixerChannel* addChannel(const std::string& name = "") {
        // IDs start at 1 to avoid collision with Master (ID 0).
        auto channel =
            std::make_unique<MixerChannel>(name.empty() ? "Track " + std::to_string(m_channels.size() + 1) : name,
                                           static_cast<uint32_t>(m_channels.size() + 1));
        channel->setCommandSink(m_commandSink);
        auto* raw = channel.get();
        m_channels.push_back(std::move(channel));
        m_graphDirty.store(true, std::memory_order_relaxed);
        m_modified.store(true, std::memory_order_relaxed);
        
        // Rebuild channel slot map
        if (!m_channelSlotMap) {
            m_channelSlotMap = std::make_shared<ChannelSlotMap>();
        }
        m_channelSlotMap->rebuild(m_channels);
        
        return raw;
    }

    size_t getTrackCount() const { return getChannelCount(); }
    MixerChannel* getTrack(size_t index) { return getChannel(index); }
    const MixerChannel* getTrack(size_t index) const { return getChannel(index); }

    /**
     * @brief Get the playlist model
     */
    PlaylistModel& getPlaylistModel() { return m_playlistModel; }

    const PlaylistModel& getPlaylistModel() const { return m_playlistModel; }

    /**
     * @brief Get the pattern manager
     */
    PatternManager& getPatternManager() { return m_patternManager; }

    const PatternManager& getPatternManager() const { return m_patternManager; }

    /**
     * @brief Get the source manager
     */
    SourceManager& getSourceManager() { return m_sourceManager; }

    const SourceManager& getSourceManager() const { return m_sourceManager; }

    /**
     * @brief Get the unit manager (Arsenal)
     */
    UnitManager& getUnitManager() { return m_unitManager; }

    const UnitManager& getUnitManager() const { return m_unitManager; }

    /**
     * @brief Set output sample rate
     */
    void setOutputSampleRate(double rate) { m_outputSampleRate = rate; }

    /**
     * @brief Set input sample rate
     */
    void setInputSampleRate(double rate) { m_inputSampleRate = rate; }

    /**
     * @brief Set input channel count
     */
    void setInputChannelCount(int count) { m_inputChannelCount = count; }

    /**
     * @brief Get output sample rate
     */
    double getOutputSampleRate() const { return m_outputSampleRate; }

    /**
     * @brief Get recording data snapshot (stub for Phase 2)
     */
    bool getRecordingDataSnapshot(uint32_t channelId, std::vector<float>& recordingData, double& startBeat) {
        (void)channelId;
        (void)recordingData;
        (void)startBeat;
        return false;
    }

    /**
     * @brief Set meter snapshots buffer
     */
    void setMeterSnapshots(std::shared_ptr<MeterSnapshotBuffer> snapshots) { m_meterSnapshots = snapshots; }

    /**
     * @brief Get meter snapshots
     */
    std::shared_ptr<MeterSnapshotBuffer> getMeterSnapshots() const { return m_meterSnapshots; }

    // STUB: getContinuousParams — Phase 2 will return real-time automation parameter buffer
    std::shared_ptr<ContinuousParamBuffer> getContinuousParams() const { return m_continuousParams; }

    /**
     * @brief Get channel slot map
     */
    std::shared_ptr<ChannelSlotMap> getChannelSlotMapShared() const { return m_channelSlotMap; }
    ChannelSlotMap* getChannelSlotMapRaw() const { return m_channelSlotMap.get(); }
    ChannelSlotMap getChannelSlotMapSnapshot() const { return m_channelSlotMap ? *m_channelSlotMap : ChannelSlotMap{}; }

    /**
     * @brief Set channel slot map
     */
    void setChannelSlotMapShared(std::shared_ptr<ChannelSlotMap> slotMap) { m_channelSlotMap = slotMap; }

    /**
     * @brief Set playhead position
     */
    void setPosition(double position) { m_position = position; }
    void syncPositionFromEngine(double position) { m_position = position; }

    /**
     * @brief Get playhead position
     */
    double getPosition() const { return m_position; }
    double getUIPosition() const { return m_position; }

    void setPlayStartPosition(double position) { m_playStartPosition = position; }
    double getPlayStartPosition() const { return m_playStartPosition; }

    void setUserScrubbing(bool scrubbing) { m_userScrubbing.store(scrubbing, std::memory_order_relaxed); }
    bool isUserScrubbing() const { return m_userScrubbing.load(std::memory_order_relaxed); }

    void processInput(const float* input, uint32_t frames) {
        (void)input;
        (void)frames;
    }

    void play() {
        m_isPlaying.store(true, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        pushTransportCommand(1.0f, m_position);
        if (m_stopPreviewCallback) {
            m_stopPreviewCallback();
        }
    }

    void pause() {
        m_isPlaying.store(false, std::memory_order_relaxed);
        m_isPaused.store(true, std::memory_order_relaxed);
        pushTransportCommand(0.0f, m_position);
    }

    void stop() {
        m_isPlaying.store(false, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        m_position = m_playStartPosition;
        pushTransportCommand(0.0f, m_playStartPosition);
    }

    bool isPlaying() const { return m_isPlaying.load(std::memory_order_relaxed); }

    void record() { m_isRecording.store(!m_isRecording.load(std::memory_order_relaxed), std::memory_order_relaxed); }
    bool isRecording() const { return m_isRecording.load(std::memory_order_relaxed); }

    void enableMetronome(bool enabled) {
        m_metronomeEnabled.store(enabled, std::memory_order_relaxed);
        if (m_commandSink) {
            AudioQueueCommand cmd{};
            cmd.type = AudioQueueCommandType::SetMetronomeEnabled;
            cmd.value1 = enabled ? 1.0f : 0.0f;
            m_commandSink(cmd);
        }
    }

    void setPatternMode(bool enabled) { m_patternMode.store(enabled, std::memory_order_relaxed); }
    bool isPatternMode() const { return m_patternMode.load(std::memory_order_relaxed); }
    void stopArsenalPlayback(bool keepPatternMode = false) {
        stop();
        if (!keepPatternMode) {
            m_patternMode.store(false, std::memory_order_relaxed);
        }
    }

    CommandHistory& getCommandHistory() { return m_commandHistory; }
    const CommandHistory& getCommandHistory() const { return m_commandHistory; }

    void markModified() { m_modified.store(true, std::memory_order_relaxed); }
    void setModified(bool modified) { m_modified.store(modified, std::memory_order_relaxed); }
    bool isModified() const { return m_modified.load(std::memory_order_relaxed); }

    bool consumeGraphDirty() { return m_graphDirty.exchange(false, std::memory_order_acq_rel); }
    void rebuildAndPushSnapshot() { m_graphDirty.store(false, std::memory_order_relaxed); }

    void setCommandSink(std::function<void(const AudioQueueCommand&)> sink) {
        m_commandSink = std::move(sink);
        for (auto& channel : m_channels) {
            channel->setCommandSink(m_commandSink);
        }
    }

    void setStopPreviewCallback(std::function<void()> callback) { m_stopPreviewCallback = std::move(callback); }

    void clearAllChannels() {
        m_channels.clear();
        m_graphDirty.store(true, std::memory_order_relaxed);
        if (m_channelSlotMap) {
            m_channelSlotMap->clear();
        }
    }

    TimelineClock& getTimelineClock() { return m_timelineClock; }
    const TimelineClock& getTimelineClock() const { return m_timelineClock; }

    PatternPlaybackEngine& getPatternPlaybackEngine() { return m_patternPlaybackEngine; }
    const PatternPlaybackEngine& getPatternPlaybackEngine() const { return m_patternPlaybackEngine; }

    void playPatternInArsenal(PatternID pid) {
        m_patternMode.store(true, std::memory_order_relaxed);
        m_isPlaying.store(true, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        pushTransportCommand(1.0f, m_position);
        m_patternPlaybackEngine.schedulePatternInstance(pid, 0.0, 1);
    }

    void preparePatternForArsenal(PatternID pid) {
        m_patternPlaybackEngine.flush();
        m_patternPlaybackEngine.schedulePatternInstance(pid, 0.0, 1);
    }

    void clearAllSolos() {
        for (auto& channel : m_channels) {
            channel->setSolo(false);
        }
    }

    std::vector<MixerChannel*> getChannelsSnapshot() const {
        std::vector<MixerChannel*> result;
        result.reserve(m_channels.size());
        for (auto& channel : m_channels) {
            result.push_back(channel.get());
        }
        return result;
    }

private:
    void pushTransportCommand(float playing, double positionSeconds) {
        if (!m_commandSink) {
            return;
        }

        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = playing;
        cmd.samplePos = static_cast<uint64_t>(positionSeconds * m_outputSampleRate);
        m_commandSink(cmd);
    }

    std::vector<std::unique_ptr<MixerChannel>> m_channels;
    PlaylistModel m_playlistModel;
    PatternManager m_patternManager;
    SourceManager m_sourceManager;
    TimelineClock m_timelineClock;
    PatternPlaybackEngine m_patternPlaybackEngine;
    CommandHistory m_commandHistory;

    double m_outputSampleRate{48000.0};
    double m_inputSampleRate{48000.0};
    int m_inputChannelCount{0};
    double m_position{0.0};
    double m_playStartPosition{0.0};
    std::shared_ptr<MeterSnapshotBuffer> m_meterSnapshots;
    std::shared_ptr<ContinuousParamBuffer> m_continuousParams; // STUB: Phase 2
    std::shared_ptr<ChannelSlotMap> m_channelSlotMap;
    UnitManager m_unitManager;
    std::function<void(const AudioQueueCommand&)> m_commandSink;
    std::function<void()> m_stopPreviewCallback;
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isRecording{false};
    std::atomic<bool> m_metronomeEnabled{false};
    std::atomic<bool> m_patternMode{false};
    std::atomic<bool> m_userScrubbing{false};
    std::atomic<bool> m_modified{false};
    std::atomic<bool> m_graphDirty{true};
};

} // namespace Audio
} // namespace Aestra
