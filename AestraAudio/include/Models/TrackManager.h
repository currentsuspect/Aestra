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
    /**
     * @brief Construct a track manager and wire its internal playback helpers.
     */
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

    /**
     * @brief Remove the last added channel (for undo of addChannel)
     * @return true if a channel was removed
     */
    bool removeLastChannel() {
        if (m_channels.empty()) return false;
        m_channels.pop_back();
        m_graphDirty.store(true, std::memory_order_relaxed);
        m_modified.store(true, std::memory_order_relaxed);
        if (m_channelSlotMap) {
            m_channelSlotMap->rebuild(m_channels);
        }
        return true;
    }

    bool removeChannelById(uint32_t channelId) {
        auto it = std::find_if(m_channels.begin(), m_channels.end(),
            [channelId](const auto& channel) {
                return channel && channel->getChannelId() == channelId;
            });
        if (it == m_channels.end()) {
            return false;
        }

        m_channels.erase(it);
        m_graphDirty.store(true, std::memory_order_relaxed);
        m_modified.store(true, std::memory_order_relaxed);
        if (m_channelSlotMap) {
            m_channelSlotMap->rebuild(m_channels);
        }
        return true;
    }

    size_t getTrackCount() const { return getChannelCount(); }
    /**
     * @brief Get a track by zero-based index.
     * @param index Channel index inside the current track list.
     * @return Mutable track pointer or nullptr when out of range.
     */
    MixerChannel* getTrack(size_t index) { return getChannel(index); }
    /**
     * @brief Get a track by zero-based index.
     * @param index Channel index inside the current track list.
     * @return Const track pointer or nullptr when out of range.
     */
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
     * @param rate Output device sample rate in Hz.
     */
    void setOutputSampleRate(double rate) { m_outputSampleRate = rate; }

    /**
     * @brief Set input sample rate
     * @param rate Input device sample rate in Hz.
     */
    void setInputSampleRate(double rate) { m_inputSampleRate = rate; }

    /**
     * @brief Set input channel count
     * @param count Number of hardware input channels currently available.
     */
    void setInputChannelCount(int count) { m_inputChannelCount = count; }

    /**
     * @brief Get output sample rate
     * @return Output sample rate in Hz.
     */
    double getOutputSampleRate() const { return m_outputSampleRate; }

    /**
     * @brief Get recording data snapshot (stub for Phase 2)
     * @param channelId Target channel identifier.
     * @param recordingData Output buffer for captured samples.
     * @param startBeat Output start beat for the returned capture.
     * @return Always false in the current stub implementation.
     */
    bool getRecordingDataSnapshot(uint32_t channelId, std::vector<float>& recordingData, double& startBeat) {
        (void)channelId;
        (void)recordingData;
        (void)startBeat;
        return false;
    }

    /**
     * @brief Set meter snapshots buffer
     * @param snapshots Shared meter buffer updated by the audio engine.
     */
    void setMeterSnapshots(std::shared_ptr<MeterSnapshotBuffer> snapshots) { m_meterSnapshots = snapshots; }

    /**
     * @brief Get meter snapshots
     * @return Shared pointer to the current meter snapshot buffer.
     */
    std::shared_ptr<MeterSnapshotBuffer> getMeterSnapshots() const { return m_meterSnapshots; }

    /**
     * @brief Get the continuous automation parameter buffer.
     * @return Shared buffer used for automation in later phases.
     */
    std::shared_ptr<ContinuousParamBuffer> getContinuousParams() const { return m_continuousParams; }

    /**
     * @brief Get channel slot map
     * @return Shared slot-map instance describing mixer routing.
     */
    std::shared_ptr<ChannelSlotMap> getChannelSlotMapShared() const { return m_channelSlotMap; }
    /**
     * @brief Get a raw pointer to the current channel slot map.
     * @return Slot-map pointer or nullptr when no routing map exists yet.
     */
    ChannelSlotMap* getChannelSlotMapRaw() const { return m_channelSlotMap.get(); }
    /**
     * @brief Copy the current channel slot map.
     * @return Snapshot of the slot map, or an empty map when none exists.
     */
    ChannelSlotMap getChannelSlotMapSnapshot() const { return m_channelSlotMap ? *m_channelSlotMap : ChannelSlotMap{}; }

    /**
     * @brief Set channel slot map
     * @param slotMap Shared slot-map instance to publish.
     */
    void setChannelSlotMapShared(std::shared_ptr<ChannelSlotMap> slotMap) { m_channelSlotMap = slotMap; }

    /**
     * @brief Set playhead position
     * @param position New UI playhead position in seconds.
     */
    void setPosition(double position) { m_position = position; }
    /**
     * @brief Update the UI playhead from the live audio engine.
     * @param position Current transport position in seconds.
     */
    void syncPositionFromEngine(double position) { m_position = position; }

    /**
     * @brief Get playhead position
     * @return Current transport position in seconds.
     */
    double getPosition() const { return m_position; }
    /**
     * @brief Get the playhead position used by UI views.
     * @return Current UI transport position in seconds.
     */
    double getUIPosition() const { return m_position; }

    /**
     * @brief Store the play start position used by stop/rewind.
     * @param position Play-start position in seconds.
     */
    void setPlayStartPosition(double position) { m_playStartPosition = position; }
    /**
     * @brief Get the transport start position used when stopping playback.
     * @return Stored play-start position in seconds.
     */
    double getPlayStartPosition() const { return m_playStartPosition; }

    /**
     * @brief Mark whether the user is actively scrubbing the transport.
     * @param scrubbing True while the user is dragging the playhead.
     */
    void setUserScrubbing(bool scrubbing) { m_userScrubbing.store(scrubbing, std::memory_order_relaxed); }
    /**
     * @brief Check whether the UI is currently scrubbing the playhead.
     * @return True while user scrubbing is active.
     */
    bool isUserScrubbing() const { return m_userScrubbing.load(std::memory_order_relaxed); }

    /**
     * @brief Process input audio destined for future recording support.
     * @param input Input channel buffer.
     * @param frames Number of frames available in the input buffer.
     */
    void processInput(const float* input, uint32_t frames) {
        (void)input;
        (void)frames;
    }

    /**
     * @brief Start transport playback from the current UI position.
     */
    void play() {
        m_isPlaying.store(true, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        pushTransportCommand(1.0f, m_position);
        if (m_stopPreviewCallback) {
            m_stopPreviewCallback();
        }
    }

    /**
     * @brief Pause transport playback without rewinding.
     */
    void pause() {
        m_isPlaying.store(false, std::memory_order_relaxed);
        m_isPaused.store(true, std::memory_order_relaxed);
        pushTransportCommand(0.0f, m_position);
    }

    /**
     * @brief Stop transport playback and return to the stored start position.
     */
    void stop() {
        m_isPlaying.store(false, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        m_position = m_playStartPosition;
        pushTransportCommand(0.0f, m_playStartPosition);
    }

    /**
     * @brief Check whether timeline playback is active.
     * @return True while transport playback is running.
     */
    bool isPlaying() const { return m_isPlaying.load(std::memory_order_relaxed); }

    /**
     * @brief Toggle the recording state flag.
     */
    void record() { m_isRecording.store(!m_isRecording.load(std::memory_order_relaxed), std::memory_order_relaxed); }
    /**
     * @brief Check whether recording is armed.
     * @return True while recording is active.
     */
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

    /**
     * @brief Enable or disable Arsenal pattern mode.
     * @param enabled True to enter pattern mode.
     */
    void setPatternMode(bool enabled) { m_patternMode.store(enabled, std::memory_order_relaxed); }
    /**
     * @brief Check whether pattern mode is active.
     * @return True when Arsenal pattern transport owns playback.
     */
    bool isPatternMode() const { return m_patternMode.load(std::memory_order_relaxed); }
    /**
     * @brief Stop Arsenal playback and optionally remain in pattern mode.
     * @param keepPatternMode True to preserve pattern mode after stopping.
     */
    void stopArsenalPlayback(bool keepPatternMode = false) {
        stop();
        if (!keepPatternMode) {
            m_patternMode.store(false, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Access the undo/redo command history.
     * @return Mutable command history instance.
     */
    CommandHistory& getCommandHistory() { return m_commandHistory; }
    /**
     * @brief Access the undo/redo command history.
     * @return Const command history instance.
     */
    const CommandHistory& getCommandHistory() const { return m_commandHistory; }

    /**
     * @brief Mark the project as modified.
     */
    void markModified() { m_modified.store(true, std::memory_order_relaxed); }
    /**
     * @brief Override the project modified flag.
     * @param modified New modified-state value.
     */
    void setModified(bool modified) { m_modified.store(modified, std::memory_order_relaxed); }
    /**
     * @brief Check whether the project contains unsaved changes.
     * @return True when the project has been modified.
     */
    bool isModified() const { return m_modified.load(std::memory_order_relaxed); }

    /**
     * @brief Consume and clear the graph-dirty flag.
     * @return Previous dirty state.
     */
    bool consumeGraphDirty() { return m_graphDirty.exchange(false, std::memory_order_acq_rel); }
    /**
     * @brief Clear the graph-dirty flag after rebuilding a snapshot.
     */
    void rebuildAndPushSnapshot() { m_graphDirty.store(false, std::memory_order_relaxed); }

    /**
     * @brief Install the audio-command sink used to talk to the live engine.
     * @param sink Callback that forwards commands to the audio thread.
     */
    void setCommandSink(std::function<void(const AudioQueueCommand&)> sink) {
        m_commandSink = std::move(sink);
        for (auto& channel : m_channels) {
            channel->setCommandSink(m_commandSink);
        }
    }

    /**
     * @brief Set the callback used to stop file-preview playback when transport starts.
     * @param callback Preview-stop callback.
     */
    void setStopPreviewCallback(std::function<void()> callback) { m_stopPreviewCallback = std::move(callback); }
    /**
     * @brief Push a raw audio command to the live engine.
     * @param cmd Command payload for the audio thread.
     */
    void pushAudioCommand(const AudioQueueCommand& cmd) {
        if (m_commandSink) {
            m_commandSink(cmd);
        }
    }

    /**
     * @brief Remove every track and reset the slot map.
     */
    void clearAllChannels() {
        m_channels.clear();
        m_graphDirty.store(true, std::memory_order_relaxed);
        if (m_channelSlotMap) {
            m_channelSlotMap->clear();
        }
    }

    /**
     * @brief Access the transport clock used by timeline and pattern playback.
     * @return Mutable timeline clock.
     */
    TimelineClock& getTimelineClock() { return m_timelineClock; }
    /**
     * @brief Access the transport clock used by timeline and pattern playback.
     * @return Const timeline clock.
     */
    const TimelineClock& getTimelineClock() const { return m_timelineClock; }

    /**
     * @brief Access the pattern playback scheduler.
     * @return Mutable pattern playback engine.
     */
    PatternPlaybackEngine& getPatternPlaybackEngine() { return m_patternPlaybackEngine; }
    /**
     * @brief Access the pattern playback scheduler.
     * @return Const pattern playback engine.
     */
    const PatternPlaybackEngine& getPatternPlaybackEngine() const { return m_patternPlaybackEngine; }

    /**
     * @brief Start Arsenal playback from beat zero for the supplied pattern.
     * @param pid Pattern identifier to schedule for playback.
     */
    void playPatternInArsenal(PatternID pid) {
        m_patternMode.store(true, std::memory_order_relaxed);
        m_isPlaying.store(true, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        m_position = 0.0;
        m_playStartPosition = 0.0;
        m_patternPlaybackEngine.flush();
        pushTransportCommand(1.0f, 0.0);
        m_patternPlaybackEngine.schedulePatternInstance(pid, 0.0, 1);
    }

    /**
     * @brief Flush the Arsenal scheduler before arming a pattern for playback.
     * @param pid Pattern identifier prepared for playback.
     */
    void preparePatternForArsenal(PatternID pid) {
        (void)pid;
        m_patternPlaybackEngine.flush();
    }

    /**
     * @brief Clear solo state across all mixer channels.
     */
    void clearAllSolos() {
        for (auto& channel : m_channels) {
            channel->setSolo(false);
        }
    }

    /**
     * @brief Get raw pointers to the current channel list.
     * @return Snapshot vector of channel pointers in track order.
     */
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
