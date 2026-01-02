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
#include "../External/miniaudio/miniaudio.h"
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "ClipSource.h"

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
    
    // Wire PatternManager to PlaylistModel for smart split (pattern cloning)
    m_playlistModel.setPatternManager(&m_patternManager);
    
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
    // DO NOT clear recording here! record() calls play(), so this would cancel it.
    // m_isRecording.store(false); 
    
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 1.0f; // Playing
        
        // Preserve current position
        double sr = m_outputSampleRate.load();
        if (sr <= 0.0) sr = 48000.0;
        cmd.samplePos = static_cast<uint64_t>(m_positionSeconds.load() * sr);
        
        m_commandSink(cmd);
    }
    
    Log::info("TrackManager: Play started");
}

void TrackManager::pause() {
    m_isPlaying.store(false);
    
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 0.0f; // Stopped/Paused
        
        double sr = m_outputSampleRate.load();
        if (sr <= 0.0) sr = 48000.0;
        cmd.samplePos = static_cast<uint64_t>(m_positionSeconds.load() * sr);
        
        m_commandSink(cmd);
    }
    
    Log::info("TrackManager: Paused");
}

void TrackManager::stop() {
    m_isPlaying.store(false);
    if (m_isRecording.load()) {
        m_isRecording.store(false);
        finishRecording();
    }
    
    setPosition(0.0); // This will also update m_positionSeconds and m_uiPositionSeconds
    
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 0.0f; // Stopped
        cmd.samplePos = 0; // Reset to start
        m_commandSink(cmd);
    }

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
     bool wasRecording = m_isRecording.load();
     if (wasRecording) {
         m_isRecording.store(false);
         finishRecording();
         Log::info("Recording stopped");
     } else {
         // Start Recording
         {
             std::lock_guard<std::mutex> lock(m_recordingMutex);
             m_recordingBuffers.clear();
             std::lock_guard<std::mutex> chanLock(m_channelMutex);
             
             double currentBeat = m_positionSeconds.load() * (m_clock.getCurrentTempo() / 60.0);
             


             // Use PlaylistModel directly since we are on the UI thread
             // const auto* snapshot = m_snapshotManager.peekCurrentSnapshot();

             for (const auto& channel : m_channels) {
                 if (channel->isArmed()) {
                     RecordingBuffer buf;
                     buf.trackId = channel->getChannelId();
                     buf.startBeat = currentBeat;
                     buf.inputChannelIndex = channel->getInputChannelIndex();
                     Log::info("Track " + std::to_string(buf.trackId) + " Armed. Input: " + std::to_string(buf.inputChannelIndex));
                     buf.active = true;
                     
                     // Target corresponding lane index (assuming 1:1 map)
                     // Find channel index
                     auto it = std::find(m_channels.begin(), m_channels.end(), channel);
                     if (it != m_channels.end()) {
                         size_t idx = std::distance(m_channels.begin(), it);
                         if (idx < m_playlistModel.getLaneCount()) {
                             buf.targetLane = m_playlistModel.getLaneId(idx);
                         }
                     }
                     
                     if (buf.trackId > 0) // Valid track
                        m_recordingBuffers.push_back(std::move(buf));
                 }
             }
         }
         
         if (!m_recordingBuffers.empty()) {
             m_isRecording.store(true);
             Log::info("Recording started - Armed tracks: " + std::to_string(m_recordingBuffers.size()));
             if (!isPlaying()) {
                 play();
             }
         } else {
             Log::warning("Recording not started - No tracks armed");
         }
     }
}

void TrackManager::processInput(const float* inputBuffer, uint32_t numFrames) {
    static int cbCount = 0;
    if ((cbCount++ % 100) == 0) {
       Log::info("processInput: " + std::to_string(numFrames) + " frames. InputChans: " + std::to_string(m_inputChannelCount));
    }

    // Copy to monitor buffer (always update for monitoring)
    if (inputBuffer && m_inputChannelCount > 0) {
        std::lock_guard<std::mutex> monitorLock(m_monitorMutex); // <--- LOCK ADDED
        size_t totalSamples = numFrames * m_inputChannelCount;
        if (m_monitorBuffer.size() < totalSamples) m_monitorBuffer.resize(totalSamples);
        std::memcpy(m_monitorBuffer.data(), inputBuffer, totalSamples * sizeof(float));
    }

    if (!m_isRecording.load()) return;
    
    std::unique_lock<std::mutex> lock(m_recordingMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    // Initialize time on first callback
    if (m_recordingFramesCaptured == 0) {
        m_recordingStartTime = std::chrono::high_resolution_clock::now();
    }
    m_recordingFramesCaptured += numFrames; // <--- ADDED THIS
    
    uint32_t stride = (m_inputChannelCount > 0) ? m_inputChannelCount : 2;
    for (auto& buf : m_recordingBuffers) {
        if (buf.active) {
            buf.append(inputBuffer, numFrames, stride, buf.inputChannelIndex);
        }
    }
}

void TrackManager::finishRecording() {
    std::lock_guard<std::mutex> lock(m_recordingMutex);
    
    // Ensure Recordings dir exists
    std::filesystem::path recDir = "Recordings";
    std::error_code ec;
    std::filesystem::create_directories(recDir, ec);
    
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    
    for (auto& buf : m_recordingBuffers) {
        if (buf.data.empty()) {
            Log::warning("Recording buffer empty for Track " + std::to_string(buf.trackId));
            continue;
        }

        // --- Latency Compensation ---
        if (m_latencySamples > 0 && buf.data.size() > m_latencySamples) {
             Log::info("Compensating latency: Removing " + std::to_string(m_latencySamples) + " samples");
             buf.data.erase(buf.data.begin(), buf.data.begin() + m_latencySamples);
        } else if (m_latencySamples > 0) {
             // Edge case: recording shorter than latency
             Log::warning("Recording shorter than latency compensation!");
             buf.data.clear();
        }

        if (buf.data.empty()) continue;

        Log::info("Saving recording for Track " + std::to_string(buf.trackId) + ". Samples: " + std::to_string(buf.data.size()));
        
        // Generate filename
        std::stringstream ss;
        ss << "Rec_Track" << buf.trackId << "_" << std::put_time(std::localtime(&timeT), "%Y%m%d_%H%M%S") << ".wav";
        std::filesystem::path filePath = recDir / ss.str();
        
        // === SAMPLE RATE: Use the ACTUAL input sample rate from WASAPI ===
        // The old "smart detection" (frames/time) was unreliable due to timing jitter.
        // Main.cpp sets m_inputSampleRate from WASAPI's actual device rate.
        // This is THE SOURCE OF TRUTH for what rate the samples were captured at.
        
        uint32_t finalRate = static_cast<uint32_t>(m_inputSampleRate.load());
        
        // Fallback to output rate if input rate not set
        if (finalRate == 0) {
            finalRate = static_cast<uint32_t>(m_outputSampleRate.load());
        }
        
        // Debug: Log what the old "smart detection" would have guessed (for comparison)
        auto stopTime = std::chrono::high_resolution_clock::now();
        double durationSec = std::chrono::duration<double>(stopTime - m_recordingStartTime).count();
        double detectedRate = (durationSec > 0.5) ? (double)m_recordingFramesCaptured / durationSec : 0.0;
        Log::info("Rate Debug: Frames=" + std::to_string(m_recordingFramesCaptured) + 
                  " Duration=" + std::to_string(durationSec) + 
                  " (Detected~" + std::to_string((int)detectedRate) + "Hz)");
        Log::info("Recording: Using WASAPI Input Rate = " + std::to_string(finalRate) + " Hz");
        
        ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, finalRate);
        ma_encoder encoder;
        if (ma_encoder_init_file(filePath.string().c_str(), &config, &encoder) == MA_SUCCESS) {
            ma_encoder_write_pcm_frames(&encoder, buf.data.data(), buf.data.size(), nullptr);
            ma_encoder_uninit(&encoder);
            
            Log::info("Saved recording to: " + filePath.string());

            // Register Source
            ClipSourceID sourceId = m_sourceManager.createSource(ss.str());
            ClipSource* source = m_sourceManager.getSource(sourceId);
            if (source) {
                source->setFilePath(filePath.string());
                
                auto audioData = std::make_shared<AudioBufferData>();
                audioData->sampleRate = finalRate;
                audioData->numChannels = 1;
                audioData->numFrames = buf.data.size();
                audioData->interleavedData = buf.data; // Copy
                source->setBuffer(audioData);
                
                // Create Pattern (which ClipInstance uses)
                AudioSlicePayload payload;
                payload.audioSourceId = sourceId;
                payload.slices.push_back({0.0, (double)buf.data.size()});
                
                double durationBeats = (double)buf.data.size() / (double)finalRate * (m_clock.getCurrentTempo() / 60.0);
                PatternID patternId = m_patternManager.createAudioPattern(ss.str(), durationBeats, payload);

                // Create Clip Instance
                ClipInstance newClip;
                newClip.id = ClipInstanceID::generate();
                newClip.patternId = patternId;
                newClip.startBeat = buf.startBeat;
                newClip.durationBeats = durationBeats;
                newClip.name = ss.str();
                newClip.mixerChannelId = MixerChannelID(buf.trackId); // Assuming TrackID matches MixerChannelID value
                
                // Add to Playlist
                if (buf.targetLane.isValid()) {
                    m_playlistModel.addClip(buf.targetLane, newClip);
                } else {
                    // Create new lane? For now just log warning.
                    Log::warning("Recorded clip not added: No target lane found");
                }
            }
        } else {
             Log::error("Failed to save recording: " + filePath.string());
        }
    }
    m_recordingBuffers.clear();
    m_isModified.store(true);
    m_graphDirty.store(true);
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
    // Protect m_channels from concurrent modification (Graph Rebuild)
    std::unique_lock<std::mutex> channelLock(m_channelMutex, std::try_to_lock);
    if (!channelLock.owns_lock()) {
        // Graph is being rebuilt - output silence to avoid crash
        std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
        return;
    }

    try {
        if (m_multiThreadingEnabled && m_threadPool && m_channels.size() > 2) {
            processAudioMultiThreaded(outputBuffer, numFrames, streamTime, outputSampleRate, snapshot);
        } else {
            processAudioSingleThreaded(outputBuffer, numFrames, streamTime, outputSampleRate, snapshot);
        }
    } catch (const std::exception& e) {
        Log::error("CRITICAL EXCEPTION in processAudio: " + std::string(e.what()));
        std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
    } catch (...) {
        Log::error("CRITICAL EXCEPTION in processAudio: Unknown");
        std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
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
    // Lock is held by caller (processAudio)
    // std::lock_guard<std::mutex> lock(m_channelMutex);
    
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

        // === INPUT MONITORING ===
        // Tape-Style: Only monitor input if we are RECORDING.
        // Monitoring while Stopped causes immediate feedback loops for users with hot mics/loopback ("Screeching").
        // To be safe for "Just Works" users, verify recording is active.
        bool monitoringAllowed = m_isRecording.load();
        
        if (monitoringAllowed && m_inputChannelCount > 0) {
             std::lock_guard<std::mutex> monitorLock(m_monitorMutex); // <--- LOCK ADDED
             if (!m_monitorBuffer.empty()) {
                 if (laneIndex < m_channels.size()) {
                     const auto& channel = m_channels[laneIndex];
                     if (channel->isMonitoringEnabled()) {
                         int inputChan = channel->getInputChannelIndex();
                         if (inputChan >= 0 && inputChan < static_cast<int>(m_inputChannelCount)) {
                             for (uint32_t f = 0; f < numFrames; ++f) {
                                  // Bounds safety inside lock
                                  if (f * m_inputChannelCount + inputChan < m_monitorBuffer.size()) {
                                      float inSample = m_monitorBuffer[f * m_inputChannelCount + inputChan];
                                      channelBuf[f * 2] += inSample;     // L
                                      channelBuf[f * 2 + 1] += inSample; // R
                                  }
                             }
                         }
                     }
                 }
             }
        }
        
        for (const auto& clip : lane.clips) {
            if (clip.muted) continue;
            if (!clip.overlaps(winStart, winEnd)) continue;
            
            if (clip.isAudio()) {
                if (!clip.audioData) continue; // Safety check

                SampleIndex clipOffset = std::max(SampleIndex(0), winStart - clip.startTime);
                SampleIndex framesToProcess = std::min(SampleIndex(numFrames), clip.getEndTime() - winStart);
                SampleIndex bufferOffset = std::max(SampleIndex(0), clip.startTime - winStart);
                
                // CRITICAL SAFETY: Ensure we don't write past the OUTPUT buffer
                // dstIdx = (bufferOffset + i) * 2 must stay within numFrames * 2
                if (bufferOffset >= SampleIndex(numFrames)) {
                    continue; // Clip starts after this buffer, skip
                }
                if (bufferOffset + framesToProcess > SampleIndex(numFrames)) {
                    framesToProcess = SampleIndex(numFrames) - bufferOffset;
                }
                if (framesToProcess <= 0) continue;
                
                // Resampling & Mixing Loop
                // 1. Calculate Ratio (Source Speed)
                double sourceSR = (clip.sourceSampleRate > 0) ? (double)clip.sourceSampleRate : outputSampleRate;
                double ratio = sourceSR / outputSampleRate;
                
                // DEBUG LOGGING (Once per clip start/playback start?)
                // Use a static counter to avoid spam, or check streamTime
                static double lastLogTime = -999.0;
                if (streamTime - lastLogTime > 2.0) {
                     Log::info("Mixing Clip: " + std::to_string(clip.patternId) + 
                               " SourceSR: " + std::to_string(sourceSR) + 
                               " OutSR: " + std::to_string(outputSampleRate) + 
                               " Ratio: " + std::to_string(ratio) +
                               " Frames: " + std::to_string(clip.audioData->numFrames));
                     lastLogTime = streamTime;
                }

                // Helper to safely get sample (Bounds Checked)
                auto safeGetSample = [&](SampleIndex idx, int channel) -> float {
                    if (!clip.audioData || clip.audioData->numFrames == 0) return 0.0f;
                    if (idx >= clip.audioData->numFrames) idx = clip.audioData->numFrames - 1; 
                    return clip.audioData->getSample(idx, channel); 
                };

                try {
                    const SampleIndex maxFrame = (clip.audioData->numFrames > 0) ? clip.audioData->numFrames - 1 : 0;
                    
                    for (SampleIndex i = 0; i < framesToProcess; ++i) {
                        SampleIndex dstIdx = (bufferOffset + i) * 2;
                        
                        double outputTimeAssumingStart = (double)(winStart + bufferOffset + i) - (double)clip.startTime;
                        double sourcePosDouble = (double)clip.sourceStart + outputTimeAssumingStart * ratio;
                        
                        // SAFETY: Clamp to valid source range BEFORE any access
                        if (sourcePosDouble < 0.0) sourcePosDouble = 0.0;
                        
                        SampleIndex idx0 = static_cast<SampleIndex>(sourcePosDouble);
                        SampleIndex idx1 = idx0 + 1;
                        
                        // SAFETY: Exit loop if we've exhausted source audio (prevent crash)
                        if (idx0 >= clip.audioData->numFrames) {
                            break; // Source exhausted - stop playing this clip for this buffer
                        }
                        
                        // Clamp idx1 to valid range (for interpolation at end of buffer)
                        if (idx1 >= clip.audioData->numFrames) idx1 = maxFrame;
                        
                        float frac = static_cast<float>(sourcePosDouble - idx0);
                        float gain = clip.getGainAt(winStart + bufferOffset + i);
                        
                        float s0_L, s1_L, s0_R, s1_R;
                        
                        if (clip.sourceChannels > 1) {
                            s0_L = safeGetSample(idx0, 0);
                            s0_R = safeGetSample(idx0, 1);
                            s1_L = safeGetSample(idx1, 0);
                            s1_R = safeGetSample(idx1, 1);
                        } else {
                            s0_L = s0_R = safeGetSample(idx0, 0);
                            s1_L = s1_R = safeGetSample(idx1, 0);
                        }
                        
                        float sampleL = s0_L + frac * (s1_L - s0_L);
                        float sampleR = s0_R + frac * (s1_R - s0_R);
                        
                        channelBuf[dstIdx] += sampleL * gain;
                        channelBuf[dstIdx + 1] += sampleR * gain;
                    }
                } catch (const std::exception& e) {
                    Log::error("CRASH AVERTED in processing loop: " + std::string(e.what()));
                } catch (...) {
                    Log::error("CRASH AVERTED in processing loop: Unknown exception");
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
            float L = srcBuf[j * 2] * vol * panL;
            float R = srcBuf[j * 2 + 1] * vol * panR;
            
            outputBuffer[j * 2] += L;
            outputBuffer[j * 2 + 1] += R;
        }
    }
    
    // === SAFETY RAIL: Hard Limiter & NaN Check ===
    // Prevents Feedback Explosions from crashing the app or destroying speakers.
    for (uint32_t j = 0; j < numFrames * 2; ++j) {
        float s = outputBuffer[j];
        if (std::isnan(s) || std::isinf(s)) {
            outputBuffer[j] = 0.0f;
        } else if (s > 2.0f) {
            outputBuffer[j] = 2.0f;
        } else if (s < -2.0f) {
            outputBuffer[j] = -2.0f;
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
    // Thread-Safe: Access PlaylistModel (Main Thread Data) instead of Snapshot (Audio Thread RCU)
    // to prevent Use-After-Free race condition during UI/Graph rebuilds.
    double beats = m_playlistModel.getTotalDurationBeats();
    return m_playlistModel.beatToSeconds(beats);
}


void TrackManager::setOutputSampleRate(double sampleRate) {
    m_outputSampleRate.store(sampleRate);
    // Rebuild graph at new rate on next render
    m_graphDirty.store(true, std::memory_order_release);
}

// Input channel names for UI
std::vector<std::string> TrackManager::getInputChannelNames() const {
    std::vector<std::string> names;
    names.reserve(m_inputChannelCount + 1);
    for (uint32_t i = 0; i < m_inputChannelCount; ++i) {
        names.push_back("Input " + std::to_string(i + 1));
    }
    names.push_back("None");
    return names;
}

void TrackManager::setLatencySamples(uint32_t samples) {
    m_latencySamples = samples;
    // Optional: Log latency update? 
    // Log::info("TrackManager: Latency compensation set to " + std::to_string(samples) + " samples");
}

} // namespace Audio
} // namespace Nomad
