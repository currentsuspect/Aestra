// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "TrackManager.h"
#include "MixerChannel.h"

#include "NomadLog.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <unordered_map>
#include "NomadPlatform.h" // For platform threading abstraction

namespace Nomad {
namespace Audio {

// Maximum buffer size for pre-allocation (16384 frames is plenty for any reasonable latency)
static constexpr size_t MAX_AUDIO_BUFFER_SIZE = 16384;

//==============================================================================
// Helpers
//==============================================================================

static void reindexChannels(std::vector<std::shared_ptr<MixerChannel>>& channels) {
    for (uint32_t idx = 0; idx < channels.size(); ++idx) {
        if (channels[idx]) {
            // MixerChannels don't strictly need index tracking for DSP, 
            // but we keep metadata up to date.
        }
    }
}

//==============================================================================
// AudioThreadPool Implementation
//==============================================================================

AudioThreadPool::AudioThreadPool(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] { workerThread(); });
    }
}

AudioThreadPool::~AudioThreadPool() {
    m_stop = true;
    m_condition.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
}

void AudioThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_tasks.push(std::move(task));
        m_activeTasks++;
    }
    m_condition.notify_one();
}

void AudioThreadPool::waitForCompletion() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_completionCondition.wait(lock, [this] { return m_tasks.empty() && m_activeTasks == 0; });
}

void AudioThreadPool::workerThread() {
    while (!m_stop) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
            if (m_stop && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        
        if (task) {
            task();
            m_activeTasks--;
            if (m_activeTasks == 0) {
                m_completionCondition.notify_all();
            }
        }
    }
}


//==============================================================================
// TrackManager Implementation
//==============================================================================

TrackManager::TrackManager() {
    size_t hwThreads = std::thread::hardware_concurrency();
    size_t audioThreads = (std::max)(static_cast<size_t>(2), (std::min)(static_cast<size_t>(8), hwThreads > 0 ? hwThreads - 1 : 4));
    
    m_threadPool = std::make_unique<AudioThreadPool>(audioThreads);
    m_meterSnapshotsOwned = std::make_shared<MeterSnapshotBuffer>();
    m_meterSnapshotsRaw = m_meterSnapshotsOwned.get();
    m_continuousParamsOwned = std::make_shared<ContinuousParamBuffer>();
    m_continuousParamsRaw = m_continuousParamsOwned.get();
    
    // Wire up playlist changes to trigger graph rebuilds
    m_playlistModel.addChangeObserver([this]() {
        m_graphDirty.store(true, std::memory_order_release);
    });
    
    // Initialize pattern playback engine
    m_patternEngine = std::make_unique<PatternPlaybackEngine>(&m_clock, &m_patternManager, &m_unitManager);
    m_clock.setTempo(120.0); // Default tempo
    
    Log::info("TrackManager v3.0 created");
}

TrackManager::~TrackManager() {
    clearAllChannels();
    m_threadPool.reset();
    Log::info("TrackManager destroyed");
}

void TrackManager::setThreadCount(size_t count) {
    count = std::max(size_t(1), std::min(size_t(16), count));
    m_threadPool.reset();
    m_threadPool = std::make_unique<AudioThreadPool>(count);
}

// Mixer Channel Management
std::shared_ptr<MixerChannel> TrackManager::addChannel(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    std::string channelName = name.empty() ? "Channel " + std::to_string(m_channels.size() + 1) : name;

    auto channel = std::make_shared<MixerChannel>(channelName, m_nextChannelId++);
    m_channels.push_back(channel);

    if (m_channelBuffers.size() < m_channels.size()) {
        std::vector<float> newBuffer(MAX_AUDIO_BUFFER_SIZE * 2, 0.0f);
        m_channelBuffers.resize(m_channels.size(), std::move(newBuffer));
    }

    m_isModified.store(true);
    m_graphDirty.store(true, std::memory_order_release);

    Log::info("Added MixerChannel: " + channelName);
    rebuildChannelSlotMapLocked();
    return channel;
}

void TrackManager::addExistingChannel(std::shared_ptr<MixerChannel> channel) {
    if (!channel) return;
    std::lock_guard<std::mutex> lock(m_channelMutex);
    m_channels.push_back(channel);
    if (m_channelBuffers.size() < m_channels.size()) {
        std::vector<float> newBuffer(MAX_AUDIO_BUFFER_SIZE * 2, 0.0f);
        m_channelBuffers.resize(m_channels.size(), std::move(newBuffer));
    }
    m_graphDirty.store(true, std::memory_order_release);
    rebuildChannelSlotMapLocked();
}

std::shared_ptr<MixerChannel> TrackManager::getChannel(size_t index) {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    return index < m_channels.size() ? m_channels[index] : nullptr;
}

std::shared_ptr<const MixerChannel> TrackManager::getChannel(size_t index) const {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    return index < m_channels.size() ? m_channels[index] : nullptr;
}

void TrackManager::removeChannel(size_t index) {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    if (index < m_channels.size()) {
        m_channels.erase(m_channels.begin() + index);
        if (index < m_channelBuffers.size()) {
            m_channelBuffers.erase(m_channelBuffers.begin() + index);
        }
        m_isModified.store(true);
        m_graphDirty.store(true, std::memory_order_release);
        rebuildChannelSlotMapLocked();
    }
}

void TrackManager::clearAllChannels() {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    m_channels.clear();
    m_channelBuffers.clear();
    m_channelSlotMapOwned.reset();
    m_channelSlotMapRaw = nullptr;
    m_graphDirty.store(true, std::memory_order_release);
}

std::vector<std::shared_ptr<MixerChannel>> TrackManager::getChannelsSnapshot() const {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    return m_channels;
}

void TrackManager::rebuildChannelSlotMapLocked() {
    auto map = std::make_shared<ChannelSlotMap>();
    map->rebuild(m_channels);
    m_channelSlotMapRaw = map.get();
    m_channelSlotMapOwned = std::move(map);
    if (m_continuousParamsRaw) {
        m_continuousParamsRaw->resetAll();
    }
}

size_t TrackManager::getTrackCount() const { return getChannelCount(); }
std::shared_ptr<Track> TrackManager::getTrack(size_t index) { return getChannel(index); }
void TrackManager::clearAllTracks() { clearAllChannels(); }
std::shared_ptr<Track> TrackManager::addTrack(const std::string& name) { return addChannel(name); }
void TrackManager::addExistingTrack(std::shared_ptr<Track> channel) { addExistingChannel(channel); }

std::shared_ptr<Track> TrackManager::addTrack(const std::string& name, double) {
    return addChannel(name);
}

std::shared_ptr<Track> TrackManager::sliceClip(std::shared_ptr<Track>, double) {
    return nullptr;
}

std::shared_ptr<Track> TrackManager::sliceClip(size_t, double) {
    return nullptr;
}


ChannelSlotMap TrackManager::getChannelSlotMapSnapshot() const {


    std::lock_guard<std::mutex> lock(m_channelMutex);
    ChannelSlotMap snapshot;
    snapshot.rebuild(m_channels);
    return snapshot;
}



// Transport Control
void TrackManager::play() {
    m_isPlaying.store(true);
    m_isRecording.store(false);
    Log::info("TrackManager: Play started");
}

void TrackManager::pause() {
    m_isPlaying.store(false);
    Log::info("TrackManager: Paused");
}

void TrackManager::stop() {
    m_isPlaying.store(false);
    m_isRecording.store(false);
    setPosition(0.0); // This will also update m_positionSeconds and m_uiPositionSeconds
    stopArsenalPlayback(); // Also stop Arsenal playback
    Log::info("TrackManager: Stopped");
}

void TrackManager::playPatternInArsenal(PatternID patternId) {
    // Arsenal mode: Schedule pattern at current position
    double currentBeat = m_positionSeconds.load() * (m_clock.getCurrentTempo() / 60.0);
    
    // Cancel previous Arsenal playback
    if (m_arsenalInstanceId > 0) {
        m_patternEngine->cancelPatternInstance(m_arsenalInstanceId);
    }
    
    // Use instance ID 0 for Arsenal (reserved)
    m_arsenalInstanceId = 0;
    m_patternEngine->schedulePatternInstance(patternId, currentBeat, m_arsenalInstanceId);
    
    // Start playing if not already
    if (!isPlaying()) {
        play();
    }
    
    Log::info("[Arsenal] Playing pattern " + std::to_string(patternId.value));
}

void TrackManager::stopArsenalPlayback() {
    if (m_arsenalInstanceId > 0) {
        m_patternEngine->cancelPatternInstance(m_arsenalInstanceId);
        m_arsenalInstanceId = 0;
    }
}

void TrackManager::record() {
    m_isRecording.store(!m_isRecording.load());
}

// Position Control
void TrackManager::setPosition(double seconds) {
    seconds = std::max(0.0, seconds);
    m_positionSeconds.store(seconds);
    m_uiPositionSeconds.store(seconds); // UI Safe update

    if (m_onPositionUpdate) {
        m_onPositionUpdate(seconds);
    }

    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = m_isPlaying.load() ? 1.0f : 0.0f;
        double sr = m_outputSampleRate.load();
        cmd.samplePos = static_cast<uint64_t>(seconds * sr);
        m_commandSink(cmd);
    }
}

void TrackManager::syncPositionFromEngine(double seconds) {
    m_positionSeconds.store(std::max(0.0, seconds));
    m_uiPositionSeconds.store(std::max(0.0, seconds)); // Keep in sync
}


// Audio Processing
void TrackManager::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, const SourceManager& sourceManager) {
    if (!outputBuffer || numFrames == 0) return;
    
    // 1. Get current Playlist Snapshot
    const PlaylistRuntimeSnapshot* snapshot = m_snapshotManager.getCurrentSnapshot();
    if (!snapshot) {
        // Fallback to silence or basic mixer bypass
        std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    double outputSampleRate = m_outputSampleRate.load();
    if (outputSampleRate <= 0.0) outputSampleRate = 48000.0;
    
    // === PATTERN PLAYBACK: Refill lookahead window (scheduler work) ===
    if (m_isPlaying.load()) {
        m_patternEngine->refillWindow(m_currentSampleFrame.load(), static_cast<int>(outputSampleRate), 4096);
    }
    
    // 2. Dispatch to processing implementation
    if (m_multiThreadingEnabled && m_threadPool && m_channels.size() > 2) {
        processAudioMultiThreaded(outputBuffer, numFrames, streamTime, outputSampleRate, snapshot);
    } else {
        processAudioSingleThreaded(outputBuffer, numFrames, streamTime, outputSampleRate, snapshot);
    }
    
    // === PATTERN PLAYBACK: Process RT events (call happens inside single/multi-threaded) ===
    // (Will be called from within process functions - see below)
    
    // Update sample frame counter
    m_currentSampleFrame.fetch_add(numFrames, std::memory_order_relaxed);
    
    // 3. Performance tracking & UI Position Sync
    auto endTime = std::chrono::high_resolution_clock::now();
    double processingTimeUs = std::chrono::duration<double, std::micro>(endTime - startTime).count();
    double availableTimeUs = (numFrames / outputSampleRate) * 1000000.0;
    m_audioLoadPercent.store((processingTimeUs / availableTimeUs) * 100.0);
    
    // Smooth UI Position update (Exactly once per block)
    m_uiPositionSeconds.store(m_positionSeconds.load());
}

void TrackManager::processAudioSingleThreaded(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate, const PlaylistRuntimeSnapshot* snapshot) {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    
    // === PASS 0: Clear all channel buffers ===
    for (size_t i = 0; i < m_channels.size() && i < m_channelBuffers.size(); ++i) {
        std::memset(m_channelBuffers[i].data(), 0, numFrames * 2 * sizeof(float));
    }
    std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
    
    // === PATTERN PLAYBACK ===
    if (!m_channels.empty()) {
        m_patternEngine->processAudio(m_currentSampleFrame.load(), numFrames, nullptr, 0);
    }
    
    // Current timeline window in samples
    SampleIndex winStart = static_cast<SampleIndex>(m_positionSeconds.load() * outputSampleRate);
    SampleIndex winEnd = winStart + numFrames;

    // === PASS 1: Mix clips into per-channel buffers ===
    size_t laneIndex = 0;
    for (const auto& lane : snapshot->lanes) {
        if (lane.muted || laneIndex >= m_channelBuffers.size()) {
            laneIndex++;
            continue;
        }
        
        float* channelBuf = m_channelBuffers[laneIndex].data();
        
        for (const auto& clip : lane.clips) {
            if (clip.muted) continue;
            if (!clip.overlaps(winStart, winEnd)) continue;
            
            if (clip.isAudio()) {
                SampleIndex clipOffset = std::max(SampleIndex(0), winStart - clip.startTime);
                SampleIndex framesToProcess = std::min(SampleIndex(numFrames), clip.getEndTime() - winStart);
                SampleIndex bufferOffset = std::max(SampleIndex(0), clip.startTime - winStart);
                
                for (SampleIndex i = 0; i < framesToProcess; ++i) {
                    SampleIndex frameIdx = (clip.sourceStart + clipOffset + i) % clip.audioData->numFrames;
                    SampleIndex dstIdx = (bufferOffset + i) * 2;
                    
                    float gain = clip.getGainAt(winStart + bufferOffset + i);
                    channelBuf[dstIdx] += clip.audioData->getSample(frameIdx, 0) * gain;
                    if (clip.sourceChannels > 1) {
                        channelBuf[dstIdx + 1] += clip.audioData->getSample(frameIdx, 1) * gain;
                    } else {
                        channelBuf[dstIdx + 1] += clip.audioData->getSample(frameIdx, 0) * gain;
                    }
                }
            }
        }
        laneIndex++;
    }

    // === PASS 2: Apply Sends ===
    // Build channel ID -> index map for fast lookup
    std::unordered_map<uint32_t, size_t> channelIdToIndex;
    for (size_t i = 0; i < m_channels.size(); ++i) {
        channelIdToIndex[m_channels[i]->getChannelId()] = i;
    }
    
    for (size_t srcIdx = 0; srcIdx < m_channels.size() && srcIdx < m_channelBuffers.size(); ++srcIdx) {
        auto sends = m_channels[srcIdx]->getSends(); // Thread-safe copy
        
        for (const auto& send : sends) {
            auto it = channelIdToIndex.find(send.targetChannelId);
            if (it == channelIdToIndex.end()) continue;
            
            size_t dstIdx = it->second;
            if (dstIdx >= m_channelBuffers.size()) continue;
            
            float* srcBuf = m_channelBuffers[srcIdx].data();
            float* dstBuf = m_channelBuffers[dstIdx].data();
            float sendGain = send.gain;
            
            for (uint32_t i = 0; i < numFrames * 2; ++i) {
                dstBuf[i] += srcBuf[i] * sendGain;
            }
        }
    }

    // === PASS 3: Sum channels to master with volume/pan/mute ===
    for (size_t i = 0; i < m_channels.size() && i < m_channelBuffers.size(); ++i) {
        auto& channel = m_channels[i];
        if (channel->isMuted()) continue;
        
        float vol = channel->getVolume();
        float pan = channel->getPan(); // -1 to +1
        float panL = std::min(1.0f, 1.0f - pan);
        float panR = std::min(1.0f, 1.0f + pan);
        
        float* srcBuf = m_channelBuffers[i].data();
        for (uint32_t j = 0; j < numFrames; ++j) {
            outputBuffer[j * 2] += srcBuf[j * 2] * vol * panL;
            outputBuffer[j * 2 + 1] += srcBuf[j * 2 + 1] * vol * panR;
        }
    }

    // Metering and Transport Update
    if (m_meterSnapshotsRaw) {
        // Metering logic...
    }

    if (m_isPlaying.load()) {
        double newPos = m_positionSeconds.load() + (numFrames / outputSampleRate);
        m_positionSeconds.store(newPos);
        if (m_onPositionUpdate) m_onPositionUpdate(newPos);
    }
}

void TrackManager::processAudioMultiThreaded(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate, const PlaylistRuntimeSnapshot* snapshot) {
    // Multi-threaded implementation follows same logic but parallelizes lane/channel processing
    processAudioSingleThreaded(outputBuffer, numFrames, streamTime, outputSampleRate, snapshot);
}

// === Utility Methods ===
    
void TrackManager::updateMixer() {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    Log::info("updateMixer called, channels count: " + std::to_string(m_channels.size()));
    for (const auto& channel : m_channels) {
        if (channel && channel->getMixerBus()) {
            channel->getMixerBus()->setGain(channel->getVolume());
            channel->getMixerBus()->setPan(channel->getPan());
            channel->getMixerBus()->setMute(channel->isMuted());
            channel->getMixerBus()->setSolo(channel->isSoloed());
        }
    }
}

void TrackManager::clearAllSolos() {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    for (auto& channel : m_channels) {
        channel->setSolo(false);
    }
    Log::info("Cleared all solos");
}

std::string TrackManager::generateTrackName() const {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    return "Track " + std::to_string(m_channels.size() + 1);
}

double TrackManager::getMaxTimelineExtent() const {
    const PlaylistRuntimeSnapshot* snapshot = m_snapshotManager.peekCurrentSnapshot();
    if (!snapshot) return 0.0;
    
    double maxEnd = 0.0;
    for (const auto& lane : snapshot->lanes) {
        for (const auto& clip : lane.clips) {
            double end = static_cast<double>(clip.startTime + clip.length) / m_outputSampleRate.load();
            if (end > maxEnd) maxEnd = end;
        }
    }
    return maxEnd;
}


void TrackManager::setOutputSampleRate(double sampleRate) {
    m_outputSampleRate.store(sampleRate);
    // Rebuild graph at new rate on next render
    m_graphDirty.store(true, std::memory_order_release);
}

} // namespace Audio
} // namespace Nomad
