#pragma once
#include "../Core/GraphDirtyReason.h"
#include "../Commands/AddClipCommand.h"
#include "../Commands/CommandHistory.h"
#include "../Core/AudioCommandQueue.h"
#include "../Core/AudioTelemetry.h"
#include "../Core/ChannelSlotMap.h"
#include "../Core/MixerChannel.h"
#include "../DSP/ContinuousParamBuffer.h"
#include "../DSP/PanLaw.h"
#include "../Playback/PatternPlaybackEngine.h"
#include "../Playback/TimelineClock.h"
#include "../RealtimeThreadGuard.h"
#include "AestraLog.h"
#include "ClipPrefilterService.h"
#include "MeterSnapshot.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "SourceManager.h"
#include "UnitManager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
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
    struct RecordingCapture {
        std::unique_ptr<std::atomic<float>[]> samples;
        size_t capacity{0};
        std::atomic<size_t> headIndex{0};
        std::atomic<size_t> size{0};
        std::atomic<double> startBeat{0.0};
        std::atomic<uint64_t> totalCapturedFrames{0};
        std::atomic<bool> hasStarted{false};
        int inputIndex{-1};
    };

    struct RtInputMonitorRoute {
        int inputIndex{-1};
        float volume{1.0f};
    };

    /**
     * @brief Destructor (out-of-line, TrackManager.cpp): documents that the
     * prefilter worker joins first via member-declaration order.
     */
    ~TrackManager();

    /**
     * @brief Construct a track manager and wire its internal playback helpers.
     */
    TrackManager() : m_patternPlaybackEngine(&m_timelineClock, &m_patternManager, &m_unitManager) {
        m_continuousParams = std::make_shared<ContinuousParamBuffer>();
        m_channelSlotMap = std::make_shared<ChannelSlotMap>();
        m_playlistModel.setPatternManager(&m_patternManager);
        // Wire up playlist model to trigger audio graph rebuild when clips change
        m_playlistModel.setClipChangedCallback(
            [this](const ClipInstanceID&) { requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged); });
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
        if (reportRealtimeMisuse("TrackManager::addChannel")) {
            return nullptr;
        }
        // IDs start at 1 to avoid collision with Master (ID 0).
        const uint32_t channelId = m_nextChannelId++;
        auto channel = std::make_unique<MixerChannel>(
            name.empty() ? "Track " + std::to_string(m_channels.size() + 1) : name, channelId);
        channel->setCommandSink(m_commandSink);
        channel->setInputMonitoringStateChangedCallback([this]() { publishInputMonitoringSnapshot(); });
        if (m_channelPrepareCallback) {
            m_channelPrepareCallback(*channel);
        }
        auto* raw = channel.get();
        m_channels.push_back(std::move(channel));
        requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
        m_modified.store(true, std::memory_order_relaxed);

        // Rebuild channel slot map
        if (!m_channelSlotMap) {
            m_channelSlotMap = std::make_shared<ChannelSlotMap>();
        }
        m_channelSlotMap->rebuild(m_channels);
        publishInputMonitoringSnapshot();

        return raw;
    }

    /** @brief Install the non-RT hook used to prepare newly created channels for live processing. */
    void setChannelPrepareCallback(std::function<void(MixerChannel&)> callback) {
        m_channelPrepareCallback = std::move(callback);
    }

    /**
     * @brief Remove the last added channel (for undo of addChannel)
     * @return true if a channel was removed
     */
    bool removeLastChannel() {
        if (m_channels.empty())
            return false;
        m_channels.pop_back();
        requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
        m_modified.store(true, std::memory_order_relaxed);
        if (m_channelSlotMap) {
            m_channelSlotMap->rebuild(m_channels);
        }
        publishInputMonitoringSnapshot();
        return true;
    }

    bool removeChannelById(uint32_t channelId) {
        auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const auto& channel) {
            return channel && channel->getChannelId() == channelId;
        });
        if (it == m_channels.end()) {
            return false;
        }

        m_channels.erase(it);
        requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
        m_modified.store(true, std::memory_order_relaxed);
        if (m_channelSlotMap) {
            m_channelSlotMap->rebuild(m_channels);
        }
        publishInputMonitoringSnapshot();
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
     *
     * Also forwards to the playlist's project sample rate: that value is the
     * timeline unit buildRuntimeSnapshot() uses to convert beats to engine
     * samples, so it must track the engine output rate. When they diverge,
     * clip positions and durations are wrong by the ratio on non-48 kHz
     * devices (measured: a clip in a 96 kHz engine truncated to half its
     * duration — SampleRateBufferTruthTest). Not persisted; beats remain the
     * project's source of truth.
     */
    void setOutputSampleRate(double rate) {
        m_outputSampleRate = rate;
        m_playlistModel.setProjectSampleRate(rate);
    }

    /**
     * @brief Set input sample rate
     * @param rate Input device sample rate in Hz.
     */
    void setInputSampleRate(double rate) { m_inputSampleRate = rate; }
    void setMaxRecordingSeconds(double seconds) { m_maxRecordingSeconds = std::max(1.0, seconds); }

    /**
     * @brief Set input channel count
     * @param count Number of hardware input channels currently available.
     */
    void setInputChannelCount(int count) {
        m_inputChannelCount = count;
        publishInputMonitoringSnapshot();
    }
    void setRecordingProjectPath(const std::string& projectPath) { m_recordingProjectPath = projectPath; }

    /**
     * @brief Get output sample rate
     * @return Output sample rate in Hz.
     */
    double getOutputSampleRate() const { return m_outputSampleRate; }
    double getMaxRecordingSeconds() const { return m_maxRecordingSeconds; }

    /**
     * @brief Get a copy of the currently captured waveform for one armed track.
     * @param channelId Target channel identifier.
     * @param recordingData Output buffer for captured samples.
     * @param startBeat Output start beat for the returned capture.
     * @return True when captured waveform data exists for the target track.
     */
    bool getRecordingDataSnapshot(uint32_t channelId, std::vector<float>& recordingData, double& startBeat) {
        std::lock_guard<std::mutex> lock(m_recordingMutex);
        auto it = m_recordingCaptures.find(channelId);
        if (it == m_recordingCaptures.end() || !it->second) {
            return false;
        }

        recordingData = copyCaptureSamples(*it->second);
        if (recordingData.empty()) {
            return false;
        }

        startBeat = it->second->startBeat.load(std::memory_order_acquire);
        return true;
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
     * @brief Build a channel slot map from the current channel list and share it.
     * Rebuilds the published ChannelSlotMap in place so existing audio-engine
     * consumers keep observing the current routing map.
     */
    void buildAndShareSlotMap() {
        if (!m_channelSlotMap) {
            m_channelSlotMap = std::make_shared<ChannelSlotMap>();
        }
        m_channelSlotMap->rebuild(m_channels);
    }

    /**
     * @brief Set playhead position
     * @param position New UI playhead position in seconds.
     */
    void setPosition(double position) { m_position.store(position, std::memory_order_relaxed); }
    /**
     * @brief Update the UI playhead from the live audio engine.
     * @param position Current transport position in seconds.
     */
    void syncPositionFromEngine(double position) { m_position.store(position, std::memory_order_relaxed); }

    /**
     * @brief Get playhead position
     * @return Current transport position in seconds.
     */
    double getPosition() const { return m_position.load(std::memory_order_relaxed); }
    /**
     * @brief Get the playhead position used by UI views.
     * @return Current UI transport position in seconds.
     */
    double getUIPosition() const {
        if (m_hasDisplayPositionOverride.load(std::memory_order_acquire)) {
            return m_displayPositionOverride.load(std::memory_order_relaxed);
        }
        return m_position.load(std::memory_order_relaxed);
    }

    void setDisplayPositionOverride(double position) {
        m_displayPositionOverride.store(std::max(0.0, position), std::memory_order_relaxed);
        m_hasDisplayPositionOverride.store(true, std::memory_order_release);
    }

    void clearDisplayPositionOverride() { m_hasDisplayPositionOverride.store(false, std::memory_order_release); }

    void setNextCapturePlacementStartBeat(double startBeat) {
        m_nextCapturePlacementStartBeat.store(std::max(0.0, startBeat), std::memory_order_relaxed);
        m_hasNextCapturePlacementStartBeat.store(true, std::memory_order_relaxed);
    }

    void clearNextCapturePlacementStartBeat() {
        m_hasNextCapturePlacementStartBeat.store(false, std::memory_order_relaxed);
        m_nextCapturePlacementStartBeat.store(0.0, std::memory_order_relaxed);
    }

    /**
     * @brief Store the play start position used by stop/rewind.
     * @param position Play-start position in seconds.
     */
    void setPlayStartPosition(double position) { m_playStartPosition.store(position, std::memory_order_relaxed); }
    /**
     * @brief Get the transport start position used when stopping playback.
     * @return Stored play-start position in seconds.
     */
    double getPlayStartPosition() const { return m_playStartPosition.load(std::memory_order_relaxed); }

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

    void processInput(const float* input, uint32_t frames, AudioTelemetry* telemetry = nullptr) {
        if (!m_isCapturing.load(std::memory_order_relaxed) || !input || frames == 0 || m_inputChannelCount <= 0) {
            return;
        }

        // Increment writer count BEFORE reading snapshot index so
        // finalizeCaptureSession() cannot destroy the snapshot while we read it.
        m_recordingWriters.fetch_add(1, std::memory_order_acq_rel);

        // RAII guard: ensures writer count is always decremented on any exit path.
        struct WriterGuard {
            std::atomic<uint32_t>& writers;
            ~WriterGuard() { writers.fetch_sub(1, std::memory_order_acq_rel); }
        } writerGuard{m_recordingWriters};

        // Read the immutable recording capture snapshot — avoids iterating the
        // live m_channels / m_recordingCaptures containers on the RT thread.
        const uint32_t snapIdx = m_activeRecordingCaptureSnapshot.load(std::memory_order_acquire);
        const uint32_t routeCount = m_recordingCaptureRouteCounts[snapIdx].load(std::memory_order_acquire);
        if (routeCount == 0) {
            return;
        }

        const auto& routes = m_recordingCaptureSnapshots[snapIdx];

        const double captureBeat = getCurrentTransportBeat();
        const bool hasDeferredStart = m_hasDeferredRecordingStart.load(std::memory_order_acquire);
        const double deferredStartBeat = m_deferredRecordingStartBeat.load(std::memory_order_relaxed);
        const double bpm = std::max(1.0, m_playlistModel.getBPM());
        const double sampleRate = std::max(1.0, m_inputSampleRate > 0.0 ? m_inputSampleRate : m_outputSampleRate);
        bool capturedAnyChannel = false;
        bool startedDeferredCapture = false;

        for (uint32_t r = 0; r < routeCount; ++r) {
            RecordingCapture* capture = routes[r].capture;
            if (!capture) {
                continue;
            }
            const int requestedInput = routes[r].inputIndex;
            if (requestedInput < 0 && requestedInput != -2) {
                continue;
            }

            const size_t capacity = capture->capacity;
            if (capacity == 0 || !capture->samples) {
                continue;
            }

            size_t head = capture->headIndex.load(std::memory_order_relaxed);
            size_t size = capture->size.load(std::memory_order_relaxed);
            double startBeat = capture->startBeat.load(std::memory_order_relaxed);
            uint64_t droppedFrames = 0;
            uint32_t startFrame = 0;
            double effectiveCaptureBeat = captureBeat;

            if (hasDeferredStart) {
                const double beatsUntilCapture = deferredStartBeat - captureBeat;
                if (beatsUntilCapture > 0.0) {
                    const double secondsUntilCapture = beatsUntilCapture * 60.0 / bpm;
                    const uint32_t skippedFrames = static_cast<uint32_t>(std::ceil(secondsUntilCapture * sampleRate));
                    if (skippedFrames >= frames) {
                        continue;
                    }
                    startFrame = skippedFrames;
                    effectiveCaptureBeat = deferredStartBeat;
                } else {
                    effectiveCaptureBeat = deferredStartBeat;
                }
            }

            auto appendSample = [&](float sample) {
                size_t writeIndex = (head + size) % capacity;
                capture->samples[writeIndex].store(sample, std::memory_order_relaxed);
                if (size < capacity) {
                    ++size;
                } else {
                    head = (head + 1) % capacity;
                    ++droppedFrames;
                }
            };

            if (requestedInput == -2) {
                const float channelScale = 1.0f / static_cast<float>(m_inputChannelCount);
                for (uint32_t frame = startFrame; frame < frames; ++frame) {
                    const size_t baseIndex = static_cast<size_t>(frame) * static_cast<size_t>(m_inputChannelCount);
                    float mixedSample = 0.0f;
                    for (int ch = 0; ch < m_inputChannelCount; ++ch) {
                        mixedSample += input[baseIndex + static_cast<size_t>(ch)];
                    }
                    appendSample(mixedSample * channelScale);
                }
            } else {
                const int inputIndex = requestedInput;
                if (inputIndex < 0 || inputIndex >= m_inputChannelCount) {
                    continue;
                }

                for (uint32_t frame = startFrame; frame < frames; ++frame) {
                    const size_t sampleIndex = static_cast<size_t>(frame) * static_cast<size_t>(m_inputChannelCount) +
                                               static_cast<size_t>(inputIndex);
                    appendSample(input[sampleIndex]);
                }
            }
            bool expectedStart = false;
            if (capture->hasStarted.compare_exchange_strong(expectedStart, true, std::memory_order_acq_rel)) {
                capture->startBeat.store(effectiveCaptureBeat, std::memory_order_release);
                if (hasDeferredStart) {
                    startedDeferredCapture = true;
                }
            }
            if (droppedFrames > 0) {
                startBeat += framesToBeats(static_cast<double>(droppedFrames));
                capture->startBeat.store(startBeat, std::memory_order_release);
            }
            capture->headIndex.store(head, std::memory_order_release);
            capture->size.store(size, std::memory_order_release);
            capture->totalCapturedFrames.fetch_add(frames - startFrame, std::memory_order_relaxed);
            capturedAnyChannel = true;
        }

        if (startedDeferredCapture) {
            clearDeferredRecordingStartBeat();
        }

        if (!capturedAnyChannel && !m_recordingNoArmLogged) {
            (void)telemetry;
            m_recordingNoArmLogged = true;
        }
        // writerGuard destructor handles m_recordingWriters.fetch_sub
    }

    void publishInputMonitoringSnapshot() {
        std::array<RtInputMonitorRoute, 32> routes{};
        uint32_t count = 0;
        for (const auto& channel : m_channels) {
            if (!channel || !channel->isArmed() || !channel->isMonitoringEnabled()) {
                continue;
            }
            const int requestedInput = channel->getInputChannelIndex();
            if (requestedInput == -1) {
                continue;
            }
            if (count >= routes.size()) {
                break;
            }
            routes[count++] = RtInputMonitorRoute{requestedInput, channel->getVolume()};
        }

        const uint32_t inactive = 1u - m_activeInputMonitorSnapshot.load(std::memory_order_relaxed);
        m_inputMonitorSnapshots[inactive] = routes;
        m_inputMonitorRouteCounts[inactive].store(count, std::memory_order_release);
        m_activeInputMonitorSnapshot.store(inactive, std::memory_order_release);
    }

    /**
     * @brief Update live hardware-input peak diagnostics from the audio callback.
     * @param input Interleaved hardware input buffer.
     * @param frames Number of frames in the block.
     */
    void updateInputDiagnostics(const float* input, uint32_t frames) {
        if (!input || frames == 0 || m_inputChannelCount <= 0) {
            return;
        }

        const int maxTrackedInputs = static_cast<int>(m_inputPeaks.size());
        const int trackedInputs = std::min(m_inputChannelCount, maxTrackedInputs);

        for (int ch = 0; ch < trackedInputs; ++ch) {
            float peak = 0.0f;
            for (uint32_t frame = 0; frame < frames; ++frame) {
                const size_t sampleIndex =
                    static_cast<size_t>(frame) * static_cast<size_t>(m_inputChannelCount) + static_cast<size_t>(ch);
                peak = std::max(peak, std::abs(input[sampleIndex]));
            }
            m_inputPeaks[static_cast<size_t>(ch)].store(peak, std::memory_order_relaxed);
        }

        for (int ch = trackedInputs; ch < maxTrackedInputs; ++ch) {
            m_inputPeaks[static_cast<size_t>(ch)].store(0.0f, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Get the latest live peak for a hardware input channel.
     * @param inputIndex Zero-based hardware input channel index.
     */
    float getInputPeak(int inputIndex) const {
        if (inputIndex < 0 || inputIndex >= static_cast<int>(m_inputPeaks.size())) {
            return 0.0f;
        }
        return m_inputPeaks[static_cast<size_t>(inputIndex)].load(std::memory_order_relaxed);
    }

    /**
     * @brief Get the number of currently configured hardware input channels.
     */
    int getInputChannelCount() const { return m_inputChannelCount; }

    /**
     * @brief Mix live monitored input into the realtime output buffer.
     * @param input Interleaved hardware input buffer.
     * @param output Interleaved hardware output buffer.
     * @param frames Number of frames in the block.
     * @param outputChannels Number of output channels in the destination buffer.
     */
    void mixInputMonitoring(const float* input, float* output, uint32_t frames, uint32_t outputChannels) const {
        if (!input || !output || frames == 0 || outputChannels == 0 || m_inputChannelCount <= 0) {
            return;
        }

        const uint32_t snapshotIndex = m_activeInputMonitorSnapshot.load(std::memory_order_acquire);
        const uint32_t monitoredCount = m_inputMonitorRouteCounts[snapshotIndex].load(std::memory_order_acquire);
        if (monitoredCount == 0) {
            return;
        }

        const float monitorMixScale = PanLaw::kEqualPowerCenterGain / static_cast<float>(monitoredCount);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const size_t inputBaseIndex = static_cast<size_t>(frame) * static_cast<size_t>(m_inputChannelCount);
            float monitoredSample = 0.0f;

            const auto& routes = m_inputMonitorSnapshots[snapshotIndex];
            for (uint32_t routeIndex = 0; routeIndex < monitoredCount && routeIndex < routes.size(); ++routeIndex) {
                const int requestedInput = routes[routeIndex].inputIndex;
                float sample = 0.0f;

                if (requestedInput == -2) {
                    const float channelScale = 1.0f / static_cast<float>(m_inputChannelCount);
                    for (int ch = 0; ch < m_inputChannelCount; ++ch) {
                        sample += input[inputBaseIndex + static_cast<size_t>(ch)];
                    }
                    sample *= channelScale;
                } else if (requestedInput >= 0 && requestedInput < m_inputChannelCount) {
                    sample = input[inputBaseIndex + static_cast<size_t>(requestedInput)];
                }

                monitoredSample += sample * routes[routeIndex].volume;
            }

            monitoredSample *= monitorMixScale;
            const size_t outputBaseIndex = static_cast<size_t>(frame) * static_cast<size_t>(outputChannels);
            output[outputBaseIndex] += monitoredSample;
            if (outputChannels > 1) {
                output[outputBaseIndex + 1] += monitoredSample;
            }
            for (uint32_t ch = 2; ch < outputChannels; ++ch) {
                output[outputBaseIndex + static_cast<size_t>(ch)] += monitoredSample;
            }
        }
    }

    /**
     * @brief Start transport playback from the current UI position.
     */
    void play() {
        if (!m_patternMode.load(std::memory_order_relaxed)) {
            m_patternPlaybackEngine.flush();
        }
        m_isPlaying.store(true, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        const double playStartPosition = m_position.load(std::memory_order_relaxed);
        m_playStartPosition.store(playStartPosition, std::memory_order_relaxed);
        if (!m_patternMode.load(std::memory_order_relaxed)) {
            scheduleTimelinePatternInstances(playStartPosition);
        }
        pushTransportCommand(1.0f, playStartPosition);
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
        pushTransportCommand(0.0f, m_position.load(std::memory_order_relaxed));
    }

    /**
     * @brief Stop transport playback and return to the stored cue/start position.
     */
    void stop() {
        m_isPlaying.store(false, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        const double playStartPosition = m_playStartPosition.load(std::memory_order_relaxed);
        m_position.store(playStartPosition, std::memory_order_relaxed);
        pushTransportCommand(0.0f, playStartPosition);
    }

    /**
     * @brief Check whether timeline playback is active.
     * @return True while transport playback is running.
     */
    bool isPlaying() const { return m_isPlaying.load(std::memory_order_relaxed); }
    bool isPaused() const { return m_isPaused.load(std::memory_order_relaxed); }
    bool hasArmedTracks() const { return getArmedTrackCount() > 0; }

    /**
     * @brief Toggle record-arm state and manage capture session lifetime.
     */
    void record() {
        const bool newArmedState = !m_recordArmed.load(std::memory_order_relaxed);
        m_recordArmed.store(newArmedState, std::memory_order_relaxed);
        Log::info(std::string("[TrackManager] Record arm ") + (newArmedState ? "enabled" : "disabled"));

        if (newArmedState) {
            if (hasArmedTracks() && m_transportPlayingConfirmed.load(std::memory_order_relaxed) &&
                !m_isCapturing.load(std::memory_order_relaxed)) {
                beginCaptureSession();
            }
        } else if (m_isCapturing.load(std::memory_order_relaxed)) {
            clearDeferredRecordingStartBeat();
            finalizeCaptureSession();
        } else {
            m_recordingCaptureAccepting.store(false, std::memory_order_release);
            clearNextCapturePlacementStartBeat();
        }
    }
    /**
     * @brief Check whether recording is armed.
     * @return True while recording is active.
     */
    bool isRecording() const { return m_isCapturing.load(std::memory_order_relaxed); }
    bool isRecordArmed() const { return m_recordArmed.load(std::memory_order_relaxed); }

    void setDeferredRecordingStartBeat(double startBeat) {
        if (startBeat > 0.0) {
            m_deferredRecordingStartBeat.store(startBeat, std::memory_order_relaxed);
            m_hasDeferredRecordingStart.store(true, std::memory_order_release);
            return;
        }
        clearDeferredRecordingStartBeat();
    }

    void clearDeferredRecordingStartBeat() {
        m_hasDeferredRecordingStart.store(false, std::memory_order_release);
        m_deferredRecordingStartBeat.store(0.0, std::memory_order_relaxed);
    }

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
        m_patternPlaybackEngine.flush();
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
     * Fires the optional on-modified callback for downstream dirty tracking.
     */
    void markModified() {
        m_modified.store(true, std::memory_order_relaxed);
        if (m_onModified) m_onModified();
    }
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
     * @brief Register a callback invoked every time markModified() is called.
     * Used by the application layer to forward dirty state to AutosaveManager.
     */
    void setOnModified(std::function<void()> callback) { m_onModified = std::move(callback); }

    using GraphDirtyReason = Aestra::Audio::GraphDirtyReason;

    /**
     * @brief Request a non-real-time rebuild of the live playback graph.
     * All state mutations should call this method.
     */
    void requestAudioGraphRebuild(GraphDirtyReason reason = GraphDirtyReason::TimelineChanged) {
        m_lastReason.store(reason, std::memory_order_relaxed);
        m_requestGeneration.fetch_add(1, std::memory_order_relaxed);
        m_graphDirty.store(true, std::memory_order_release);
    }

    /**
     * @brief Check if a rebuild has been requested (non-consuming).
     */
    bool hasPendingGraphRebuild() const {
        return m_graphDirty.load(std::memory_order_acquire);
    }

    /**
     * @brief Get the request generation counter (non-consuming).
     */
    uint64_t graphRebuildRequestGeneration() const {
        return m_requestGeneration.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get the last reason for a rebuild request (non-consuming).
     */
    GraphDirtyReason lastGraphDirtyReason() const {
        return m_lastReason.load(std::memory_order_relaxed);
    }

    /**
     * @brief Consume and clear pending rebuild request.
     * ONLY PlaybackGraphController should call this.
     */
    bool consumePendingGraphRebuild() {
        return m_graphDirty.exchange(false, std::memory_order_acq_rel);
    }

    /**
     * @brief Push effect chain snapshots for all channels.
     */
    void rebuildAndPushSnapshot();

    /**
     * @brief Mark the live audio graph as requiring a rebuild.
     */
    void markGraphDirty() { requestAudioGraphRebuild(); }

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
     * @brief Apply the transport state once it has been forwarded to the live engine.
     * @param playing True when the engine transport is now rolling.
     * @param positionSeconds Transport position used for the command.
     */
    void onTransportStateApplied(bool playing, double positionSeconds) {
        m_transportPlayingConfirmed.store(playing, std::memory_order_relaxed);
        if (!playing) {
            m_position.store(positionSeconds, std::memory_order_relaxed);
            clearDeferredRecordingStartBeat();
            if (m_isCapturing.load(std::memory_order_relaxed)) {
                finalizeCaptureSession();
            }
            return;
        }

        if (m_recordArmed.load(std::memory_order_relaxed) && hasArmedTracks() &&
            !m_isCapturing.load(std::memory_order_relaxed)) {
            beginCaptureSession();
        }
    }
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
        m_nextChannelId = 1;
        requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
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
     * @brief Start Arsenal playback for the supplied pattern.
     * @param pid Pattern identifier to schedule for playback.
     * @param startSeconds Transport position to start from. Negative (the
     *        default) resumes from the current position — e.g. a scrubbed
     *        piano-roll playhead — matching how timeline play() cues. Pass an
     *        explicit value (e.g. 0.0 for a from-the-top preview) to override.
     *        The engine wraps positions past the pattern length, so any cue
     *        point is safe.
     */
    void playPatternInArsenal(PatternID pid, double startSeconds = -1.0) {
        if (startSeconds < 0.0) {
            startSeconds = std::max(0.0, m_position.load(std::memory_order_relaxed));
        }
        m_patternMode.store(true, std::memory_order_relaxed);
        m_isPlaying.store(true, std::memory_order_relaxed);
        m_isPaused.store(false, std::memory_order_relaxed);
        m_position.store(startSeconds, std::memory_order_relaxed);
        m_playStartPosition.store(startSeconds, std::memory_order_relaxed);
        m_patternPlaybackEngine.flush();

        {
            auto* pattern = m_patternManager.getPattern(pid);
            if (pattern && pattern->isMidi()) {
                const double resolvedLength = std::max(8.0, pattern->lengthBeats);
                if (std::abs(resolvedLength - pattern->lengthBeats) > 0.001) {
                    m_patternManager.applyPatch(pid,
                                                [resolvedLength](PatternSource& p) { p.lengthBeats = resolvedLength; });
                }
            }
        }

        pushTransportCommand(1.0f, startSeconds);
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
    /// @brief Immutable snapshot of a recording capture route for RT path.
    struct RecordingCaptureRoute {
        RecordingCapture* capture{nullptr};
        int inputIndex{-1};
    };

    void publishRecordingCaptureSnapshot() {
        const uint32_t inactive = 1u - m_activeRecordingCaptureSnapshot.load(std::memory_order_relaxed);
        auto& snap = m_recordingCaptureSnapshots[inactive];
        uint32_t count = 0;
        for (const auto& [channelId, capture] : m_recordingCaptures) {
            if (!capture) {
                continue;
            }
            if (count >= kMaxRecordingTracks) {
                break;
            }
            snap[count].capture = capture.get();
            snap[count].inputIndex = capture->inputIndex;
            ++count;
        }
        m_recordingCaptureRouteCounts[inactive].store(count, std::memory_order_release);
        m_activeRecordingCaptureSnapshot.store(inactive, std::memory_order_release);
    }

    static double quantizePatternLengthBeats(double contentEndBeat) {
        constexpr double kBeatsPerBar = 4.0;
        constexpr double kBarsPerPatternBlock = 4.0;
        constexpr double kPatternBlockBeats = kBeatsPerBar * kBarsPerPatternBlock; // 16 beats = 4 bars

        const double safeContentEnd = std::max(0.0, contentEndBeat);
        const double blocksNeeded = std::max(1.0, std::ceil(safeContentEnd / kPatternBlockBeats));
        return blocksNeeded * kPatternBlockBeats;
    }

    void scheduleTimelinePatternInstances(double playStartPositionSeconds) {
        const double bpm = std::max(1.0, m_playlistModel.getBPM());
        const double playStartBeat = playStartPositionSeconds * bpm / 60.0;
        const auto instances = m_playlistModel.collectMidiClipInstances(m_patternManager);

        Log::info("[TimelinePattern] scheduling " + std::to_string(instances.size()) +
                  " MIDI clip instances from beat " + std::to_string(playStartBeat));

        uint32_t instanceId = 2; // Reserve 1 for Arsenal-focused single-pattern playback.
        for (const auto& instance : instances) {
            if (!instance.isValid()) {
                continue;
            }
            if (instance.endBeat() <= playStartBeat) {
                continue;
            }
            if (instanceId >= 256) {
                Log::warning("[TrackManager] Too many timeline MIDI clip instances to schedule; truncating at 254");
                break;
            }

            std::string routeSummary = "no-pattern";
            if (auto* pattern = m_patternManager.getPattern(instance.patternId); pattern && pattern->isMidi()) {
                const auto& midi = std::get<MidiPayload>(pattern->payload);
                size_t noteCount = midi.notes.size();
                UnitID firstUnitId = noteCount > 0 ? midi.notes.front().unitId : 0;
                int firstRoute = -999;
                if (firstUnitId != 0) {
                    if (auto* unit = m_unitManager.getUnit(firstUnitId)) {
                        firstRoute = unit->targetMixerRoute;
                    } else {
                        firstRoute = -998;
                    }
                }
                routeSummary = "notes=" + std::to_string(noteCount) + " firstUnit=" + std::to_string(firstUnitId) +
                               " firstRoute=" + std::to_string(firstRoute);
            }

            Log::info("[TimelinePattern] pattern=" + std::to_string(instance.patternId.value) +
                      " clipStart=" + std::to_string(instance.startBeat) +
                      " sourceOffset=" + std::to_string(instance.sourceOffsetBeats) +
                      " schedStart=" + std::to_string(instance.patternStartBeat()) + " " + routeSummary);
            m_patternPlaybackEngine.schedulePatternInstance(instance.patternId, instance.patternStartBeat(),
                                                            instanceId++, instance.sourceOffsetBeats,
                                                            instance.durationBeats);
        }
    }

    double getCurrentTransportBeat() const {
        const double bpm = std::max(1.0, m_playlistModel.getBPM());
        return m_position.load(std::memory_order_relaxed) * bpm / 60.0;
    }

    double framesToBeats(double frames) const {
        const double sampleRate = std::max(1.0, m_inputSampleRate > 0.0 ? m_inputSampleRate : m_outputSampleRate);
        const double bpm = std::max(1.0, m_playlistModel.getBPM());
        return (frames / sampleRate) * (bpm / 60.0);
    }

    void beginCaptureSession() {
        std::lock_guard<std::mutex> lock(m_recordingMutex);
        m_recordingCaptureAccepting.store(false, std::memory_order_release);
        m_recordingCaptures.clear();
        const size_t maxSamplesPerCapture = maxRecordingSamplesPerCapture();
        for (const auto& channel : m_channels) {
            if (!channel || !channel->isArmed()) {
                continue;
            }
            const int requestedInput = channel->getInputChannelIndex();
            if (requestedInput == -1) {
                continue;
            }
            auto capture = std::make_unique<RecordingCapture>();
            capture->samples = std::make_unique<std::atomic<float>[]>(maxSamplesPerCapture);
            capture->capacity = maxSamplesPerCapture;
            capture->headIndex.store(0, std::memory_order_relaxed);
            capture->size.store(0, std::memory_order_relaxed);
            capture->startBeat.store(0.0, std::memory_order_relaxed);
            capture->totalCapturedFrames.store(0, std::memory_order_relaxed);
            capture->hasStarted.store(false, std::memory_order_relaxed);
            capture->inputIndex = requestedInput;
            m_recordingCaptures[channel->getChannelId()] = std::move(capture);
        }
        m_recordingSessionStartBeat = getCurrentTransportBeat();
        m_recordingSessionUsesPlacementOverride = m_hasNextCapturePlacementStartBeat.load(std::memory_order_relaxed);
        if (m_recordingSessionUsesPlacementOverride) {
            m_recordingSessionStartBeat = m_nextCapturePlacementStartBeat.load(std::memory_order_relaxed);
        }
        clearNextCapturePlacementStartBeat();
        m_recordingNoArmLogged = false;
        const size_t armedCount = m_recordingCaptures.size();
        if (armedCount > kMaxRecordingTracks) {
            Log::warning("[TrackManager] Armed track count (" + std::to_string(armedCount) +
                         ") exceeds kMaxRecordingTracks (" + std::to_string(kMaxRecordingTracks) +
                         "). Only the first " + std::to_string(kMaxRecordingTracks) +
                         " will be captured in the snapshot.");
        }
        m_recordingCaptureAccepting.store(true, std::memory_order_release);
        m_isCapturing.store(true, std::memory_order_relaxed);
        publishRecordingCaptureSnapshot();
        Log::info("[TrackManager] Recording session started. Armed tracks: " + std::to_string(getArmedTrackCount()) +
                  ", input channels: " + std::to_string(m_inputChannelCount));
    }

    void finalizeCaptureSession() {
        m_recordingCaptureAccepting.store(false, std::memory_order_release);
        const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        bool writersDrained = m_recordingWriters.load(std::memory_order_acquire) == 0;
        while (!writersDrained && std::chrono::steady_clock::now() < waitDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            writersDrained = m_recordingWriters.load(std::memory_order_acquire) == 0;
        }
        if (!writersDrained) {
            Log::warning("[TrackManager] Timed out waiting for recording writers to drain before finalizing capture. "
                         "Deferring teardown — retry on next disarm.");
            // Leave m_isCapturing, snapshots, captures, and session fields intact.
            // processInput() callers still in-flight continue to hold valid pointers.
            m_recordingCaptureAccepting.store(true, std::memory_order_release);
            return;
        }

        m_isCapturing.store(false, std::memory_order_relaxed);

        // Publish empty snapshot so RT thread stops referencing captures.
        m_recordingCaptureRouteCounts[0].store(0, std::memory_order_release);
        m_recordingCaptureRouteCounts[1].store(0, std::memory_order_release);
        m_activeRecordingCaptureSnapshot.store(0, std::memory_order_release);

        std::unordered_map<uint32_t, std::unique_ptr<RecordingCapture>> captures;
        double sessionStartBeat = 0.0;
        bool sessionUsesPlacementOverride = false;
        {
            std::lock_guard<std::mutex> lock(m_recordingMutex);
            captures = std::move(m_recordingCaptures);
            sessionStartBeat = m_recordingSessionStartBeat;
            sessionUsesPlacementOverride = m_recordingSessionUsesPlacementOverride;
            m_recordingCaptures.clear();
            m_recordingSessionStartBeat = 0.0;
            m_recordingSessionUsesPlacementOverride = false;
            m_recordingNoArmLogged = false;
        }
        clearDeferredRecordingStartBeat();
        clearNextCapturePlacementStartBeat();

        Log::info("[TrackManager] Recording session stopped. Captured tracks: " + std::to_string(captures.size()));

        for (const auto& [channelId, capture] : captures) {
            if (capture) {
                commitRecordingTake(channelId, *capture, sessionStartBeat, sessionUsesPlacementOverride);
            }
        }
    }

    void commitRecordingTake(uint32_t channelId, const RecordingCapture& capture, double fallbackStartBeat,
                             bool forcePlacementStartBeat) {
        std::vector<float> capturedSamples = copyCaptureSamples(capture);
        if (capturedSamples.empty()) {
            return;
        }

        const size_t channelIndex = findChannelIndexById(channelId);
        if (channelIndex == static_cast<size_t>(-1)) {
            Log::warning("[TrackManager] Could not resolve lane for recorded track " + std::to_string(channelId));
            return;
        }

        PlaylistLaneID laneId = m_playlistModel.getLaneId(channelIndex);
        if (!laneId.isValid()) {
            Log::warning("[TrackManager] Invalid lane target for recorded track " + std::to_string(channelId));
            return;
        }

        std::vector<float> conditionedSamples = std::move(capturedSamples);
        const float rawPeak = analyzePeak(conditionedSamples);
        conditionRecordedTakeSamples(conditionedSamples);
        const float conditionedPeak = analyzePeak(conditionedSamples);

        const double captureStartBeat = capture.startBeat.load(std::memory_order_acquire);
        const double startBeat = forcePlacementStartBeat
                                     ? fallbackStartBeat
                                     : (captureStartBeat > 0.0 ? captureStartBeat : fallbackStartBeat);
        const double durationBeats = framesToBeats(static_cast<double>(conditionedSamples.size()));
        if (durationBeats <= 0.0) {
            return;
        }
        const double durationSeconds =
            static_cast<double>(conditionedSamples.size()) / std::max(1.0, m_inputSampleRate);
        const float playbackGain = computeRecordedTakeGain(conditionedSamples);

        auto buffer = std::make_shared<AudioBufferData>();
        buffer->sampleRate = static_cast<uint32_t>(std::max(1.0, m_inputSampleRate));
        buffer->numChannels = 2;
        buffer->numFrames = conditionedSamples.size();
        buffer->interleavedData.resize(conditionedSamples.size() * 2);
        for (size_t i = 0; i < conditionedSamples.size(); ++i) {
            const float sample = conditionedSamples[i];
            buffer->interleavedData[i * 2] = sample;
            buffer->interleavedData[i * 2 + 1] = sample;
        }

        const std::string takePath = buildRecordingTakePath(channelId);
        if (!writeRecordedTakeWav(takePath, *buffer)) {
            Log::error("[TrackManager] Failed to write recorded take: " + takePath);
            return;
        }

        const std::string takeName = std::filesystem::path(takePath).stem().string();
        ClipSourceID sourceId = m_sourceManager.createRecordedSource(takePath, takeName, buffer);
        if (!sourceId.isValid()) {
            Log::error("[TrackManager] Failed to register recorded source for: " + takePath);
            return;
        }

        AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = durationSeconds;
        // Populate the sample-domain fields the serializer persists
        // (startSamples/lengthSamples). The old {0.0, numFrames} form set
        // startOffset/duration instead, so the slice saved as start:0 length:0.
        AudioSlice fullSlice;
        fullSlice.startSamples = 0.0;
        fullSlice.lengthSamples = static_cast<double>(buffer->numFrames);
        payload.slices.push_back(fullSlice);

        PatternID patternId = m_patternManager.createAudioPattern(takeName, durationBeats, payload);
        if (!patternId.isValid()) {
            Log::error("[TrackManager] Failed to create audio pattern for recorded take.");
            return;
        }

        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.name = takeName;
        clip.startBeat = startBeat;
        clip.durationBeats = durationBeats;
        clip.durationSeconds = durationSeconds;
        clip.patternId = patternId;
        clip.sourceId = patternId.value;
        clip.edits.gain = playbackGain;
        clip.edits.gainLinear = playbackGain;

        auto cmd = std::make_shared<AddClipCommand>(m_playlistModel, laneId, clip);
        m_commandHistory.pushAndExecute(cmd);
        requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
        m_modified.store(true, std::memory_order_relaxed);

        Log::info("[TrackManager] Recorded take committed: " + takePath + " on track " + std::to_string(channelId) +
                  " at beat " + std::to_string(startBeat) + " with raw peak " + std::to_string(rawPeak) +
                  ", conditioned peak " + std::to_string(conditionedPeak) + ", clip gain " +
                  std::to_string(playbackGain));
    }

    std::string buildRecordingTakePath(uint32_t channelId) const {
        namespace fs = std::filesystem;
        fs::path root = recordingRootDirectory();
        std::error_code ec;
        fs::create_directories(root, ec);

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        static std::atomic<uint64_t> s_uniqCounter{0};
        const uint64_t uniq = s_uniqCounter.fetch_add(1, std::memory_order_relaxed);
        std::ostringstream oss;
        oss << "track_" << channelId << "_take_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << std::setfill('0')
            << std::setw(3) << ms.count() << "_" << uniq << ".wav";
        return (root / oss.str()).string();
    }

    std::filesystem::path recordingRootDirectory() const {
        namespace fs = std::filesystem;
        if (!m_recordingProjectPath.empty()) {
            fs::path projectPath(m_recordingProjectPath);
            if (projectPath.has_extension()) {
                return projectPath.parent_path() / "Recordings";
            }
            return projectPath / "Recordings";
        }
        if (const char* home = std::getenv("HOME")) {
            return fs::path(home) / "Documents" / "Aestra" / "Recordings";
        }
        return fs::current_path() / "Recordings";
    }

    bool writeRecordedTakeWav(const std::string& path, const AudioBufferData& buffer) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }

        const uint16_t audioFormat = 3; // IEEE float
        if (buffer.numChannels == 0 || buffer.numChannels > std::numeric_limits<uint16_t>::max() ||
            buffer.sampleRate == 0 ||
            buffer.interleavedData.size() > (std::numeric_limits<uint32_t>::max() / sizeof(float)) ||
            buffer.numFrames > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        const uint16_t numChannels = static_cast<uint16_t>(buffer.numChannels);
        const uint32_t sampleRate = buffer.sampleRate;
        const uint16_t bitsPerSample = 32;
        const uint16_t blockAlign = static_cast<uint16_t>(numChannels * (bitsPerSample / 8));
        if (sampleRate > std::numeric_limits<uint32_t>::max() / blockAlign) {
            return false;
        }
        const uint32_t byteRate = sampleRate * blockAlign;
        const uint32_t dataSize = static_cast<uint32_t>(buffer.interleavedData.size() * sizeof(float));
        const uint32_t sampleCount = static_cast<uint32_t>(buffer.numFrames);
        const uint32_t factChunkSize = 4;
        if (dataSize > std::numeric_limits<uint32_t>::max() - 48u) {
            return false;
        }
        const uint32_t riffChunkSize = 48u + dataSize;

        file.write("RIFF", 4);
        file.write(reinterpret_cast<const char*>(&riffChunkSize), sizeof(riffChunkSize));
        file.write("WAVE", 4);
        file.write("fmt ", 4);

        const uint32_t fmtChunkSize = 16;
        file.write(reinterpret_cast<const char*>(&fmtChunkSize), sizeof(fmtChunkSize));
        file.write(reinterpret_cast<const char*>(&audioFormat), sizeof(audioFormat));
        file.write(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));
        file.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
        file.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
        file.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
        file.write(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));

        file.write("fact", 4);
        file.write(reinterpret_cast<const char*>(&factChunkSize), sizeof(factChunkSize));
        file.write(reinterpret_cast<const char*>(&sampleCount), sizeof(sampleCount));

        file.write("data", 4);
        file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
        file.write(reinterpret_cast<const char*>(buffer.interleavedData.data()), dataSize);
        return file.good();
    }

    float computeRecordedTakeGain(const std::vector<float>& samples) const {
        float peak = analyzePeak(samples);
        if (peak <= 0.0f) {
            return 1.0f;
        }

        constexpr float targetPeak = 0.35f;
        return std::clamp(targetPeak / peak, 0.18f, 1.0f);
    }

    float analyzePeak(const std::vector<float>& samples) const {
        float peak = 0.0f;
        for (float sample : samples) {
            peak = std::max(peak, std::abs(sample));
        }
        return peak;
    }

    void conditionRecordedTakeSamples(std::vector<float>& samples) const {
        if (samples.empty()) {
            return;
        }

        // Remove DC bias first so the recorded waveform sits more naturally around zero.
        double mean = 0.0;
        for (float sample : samples) {
            mean += sample;
        }
        mean /= static_cast<double>(samples.size());
        for (float& sample : samples) {
            sample = static_cast<float>(sample - mean);
        }

        // If the capture came in too hot, scale it before it ever hits clip playback.
        float peak = analyzePeak(samples);
        constexpr float targetPeak = 0.85f;
        if (peak > targetPeak && peak > 0.0f) {
            const float scale = targetPeak / peak;
            for (float& sample : samples) {
                sample *= scale;
            }
        }
    }

    size_t getArmedTrackCount() const {
        size_t count = 0;
        for (const auto& channel : m_channels) {
            if (channel && channel->isArmed()) {
                ++count;
            }
        }
        return count;
    }

    size_t findChannelIndexById(uint32_t channelId) const {
        for (size_t i = 0; i < m_channels.size(); ++i) {
            if (m_channels[i] && m_channels[i]->getChannelId() == channelId) {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }

    size_t maxRecordingSamplesPerCapture() const {
        return static_cast<size_t>(std::max(1.0, m_outputSampleRate * std::max(1.0, m_maxRecordingSeconds)));
    }

    std::vector<float> copyCaptureSamples(const RecordingCapture& capture) const {
        const size_t capacity = capture.capacity;
        if (capacity == 0 || !capture.samples) {
            return {};
        }

        const size_t head = capture.headIndex.load(std::memory_order_acquire);
        const size_t size = capture.size.load(std::memory_order_acquire);
        if (size == 0) {
            return {};
        }

        std::vector<float> result(size);
        for (size_t i = 0; i < size; ++i) {
            const size_t index = (head + i) % capacity;
            result[i] = capture.samples[index].load(std::memory_order_relaxed);
        }
        return result;
    }

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
    uint32_t m_nextChannelId{1};
    PlaylistModel m_playlistModel;
    PatternManager m_patternManager;
    SourceManager m_sourceManager;
    TimelineClock m_timelineClock;
    PatternPlaybackEngine m_patternPlaybackEngine;
    CommandHistory m_commandHistory;

    double m_outputSampleRate{48000.0};
    double m_inputSampleRate{48000.0};
    int m_inputChannelCount{0};
    std::atomic<double> m_position{0.0};
    std::atomic<double> m_playStartPosition{0.0};
    std::atomic<bool> m_hasDisplayPositionOverride{false};
    std::atomic<double> m_displayPositionOverride{0.0};
    std::atomic<bool> m_hasNextCapturePlacementStartBeat{false};
    std::atomic<double> m_nextCapturePlacementStartBeat{0.0};
    std::shared_ptr<MeterSnapshotBuffer> m_meterSnapshots;
    std::shared_ptr<ContinuousParamBuffer> m_continuousParams; // STUB: Phase 2
    std::shared_ptr<ChannelSlotMap> m_channelSlotMap;
    UnitManager m_unitManager;
    std::function<void(const AudioQueueCommand&)> m_commandSink;
    std::function<void(MixerChannel&)> m_channelPrepareCallback;
    std::function<void()> m_stopPreviewCallback;
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_recordArmed{false};
    std::atomic<bool> m_isCapturing{false};
    std::atomic<bool> m_transportPlayingConfirmed{false};
    std::atomic<bool> m_hasDeferredRecordingStart{false};
    std::atomic<double> m_deferredRecordingStartBeat{0.0};
    std::atomic<bool> m_metronomeEnabled{false};
    std::atomic<bool> m_patternMode{false};
    std::atomic<bool> m_userScrubbing{false};
    std::atomic<bool> m_modified{false};
    std::function<void()> m_onModified;
    std::atomic<bool> m_graphDirty{true}; // Owned by TrackManager, consumed by PlaybackGraphController only
    std::atomic<uint64_t> m_requestGeneration{0};
    std::atomic<GraphDirtyReason> m_lastReason{GraphDirtyReason::TimelineChanged};
    mutable std::mutex m_recordingMutex;
    std::unordered_map<uint32_t, std::unique_ptr<RecordingCapture>> m_recordingCaptures;
    std::atomic<uint32_t> m_recordingWriters{0};
    std::atomic<bool> m_recordingCaptureAccepting{false};
    std::array<std::atomic<float>, 8> m_inputPeaks{};
    std::array<RtInputMonitorRoute, 32> m_inputMonitorSnapshots[2]{};
    std::array<std::atomic<uint32_t>, 2> m_inputMonitorRouteCounts{};
    std::atomic<uint32_t> m_activeInputMonitorSnapshot{0};

    static constexpr size_t kMaxRecordingTracks = 64;
    std::array<RecordingCaptureRoute, kMaxRecordingTracks> m_recordingCaptureSnapshots[2]{};
    std::array<std::atomic<uint32_t>, 2> m_recordingCaptureRouteCounts{};
    std::atomic<uint32_t> m_activeRecordingCaptureSnapshot{0};
    double m_maxRecordingSeconds{15.0};
    double m_recordingSessionStartBeat{0.0};
    bool m_recordingSessionUsesPlacementOverride{false};
    bool m_recordingNoArmLogged{false};
    std::string m_recordingProjectPath;

    // Anti-aliased clip prefiltering (Phase 4, F1; AestraDocs/clip-prefilter-lifecycle.md).
    // Declared LAST so it is destroyed FIRST: the worker joins while every member its
    // completion callback touches (the graph-dirty atomics above) is still alive.
    std::unique_ptr<ClipPrefilterService> m_clipPrefilterService;

public:
    /**
     * @brief Drain finished anti-alias prefilter results into their sources, clear
     * stale filtered variants, and queue missing work for downsampled clips.
     * Called by AudioGraphBuilder before every runtime snapshot (graph-build thread).
     */
    void ensureClipPrefilters();

    /**
     * @brief Block the calling NON-AUDIO thread until all queued prefilter jobs are
     * done, then apply them. Deterministic completion for offline render and tests.
     * A graph rebuild is still required for the engine to pick the copies up.
     */
    void waitForClipPrefilters();
};

} // namespace Audio
} // namespace Aestra
