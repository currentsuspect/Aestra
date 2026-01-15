// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "TrackManager.h"
#include "MixerChannel.h"
#include "AudioCommandQueue.h" 
#include "AudioEngine.h" // [NEW] Correct placement

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



//==============================================================================
// AudioThreadPool Implementation
//==============================================================================




//==============================================================================
// TrackManager Implementation
//==============================================================================

// Check pattern mode via Engine
bool TrackManager::isPatternMode() const {
    return AudioEngine::getInstance().isPatternPlaybackMode();
}

// Force Rebuild
TrackManager::TrackManager() {

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
}// Mixer Channel Management
std::shared_ptr<MixerChannel> TrackManager::addChannel(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    std::string channelName = name.empty() ? "Channel " + std::to_string(m_channels.size() + 1) : name;

    auto channel = std::make_shared<MixerChannel>(channelName, m_nextChannelId++);
    m_channels.push_back(channel);



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
        // if (index < m_channelBuffers.size()) {
        //     m_channelBuffers.erase(m_channelBuffers.begin() + index);
        // }
        m_isModified.store(true);
        m_graphDirty.store(true, std::memory_order_release);
        rebuildChannelSlotMapLocked();
    }
}

void TrackManager::clearAllChannels() {
    std::lock_guard<std::mutex> lock(m_channelMutex);
    m_channels.clear();
    // m_channelBuffers.clear(); // Removed
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
    // Save current position as play start (for return-on-stop behavior)
    saveCurrentAsPlayStart();
    
    m_isPlaying.store(true);
    
    // Decoupled Recording: Start capture if armed
    if (m_isRecordArmed.load()) {
        startRecordingProcess();
    }
    
    // [FIX] Re-schedule pattern if we're in pattern mode (was cancelled on stop/pause)
    auto& engine = AudioEngine::getInstance();
    bool isPatMode = engine.isPatternPlaybackMode();
    bool hasEngine = (m_patternEngine != nullptr);
    uint64_t lastPatId = m_lastScheduledPatternId.value;
    
    Log::info("[TrackManager] play() called. EnginePatMode=" + std::string(isPatMode ? "YES" : "NO") + 
              ", HasPatEngine=" + std::string(hasEngine ? "YES" : "NO") + 
              ", LastPatID=" + std::to_string(lastPatId));

    if (isPatMode && hasEngine && lastPatId != 0) {
        m_patternEngine->schedulePatternInstance(m_lastScheduledPatternId, 0.0, 0);
        Log::info("[TrackManager] Re-scheduled pattern " + std::to_string(m_lastScheduledPatternId.value) + " on play");
    }
    
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
    m_isRecordArmed.store(false); // Clear arm on stop
    
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
    if (!m_patternEngine) return;
    Log::info("[TrackManager] playPatternInArsenal: id=" + std::to_string(patternId.value));
    
    auto* pattern = m_patternManager.getPattern(patternId);
    if (!pattern) {
        Log::info("WARNING: TrackManager: Cannot play Arsenal - Pattern not found");
        return;
    }
    
    double lengthBeats = pattern->lengthBeats;
    if (lengthBeats <= 0.0) lengthBeats = 4.0;
    
    // Configure Engine to Pattern Mode (Loops position 0..Length)
    auto& engine = AudioEngine::getInstance();
    engine.setPatternPlaybackMode(true, lengthBeats);
    
    // Cancel previous instance
    // Use instance ID 0 for Arsenal
    m_patternEngine->cancelPatternInstance(0);
    
    // Request voice reset to avoid hung notes from previous pattern
    engine.requestVoiceResetOnPatternChange();
    
    m_arsenalInstanceId = 0;
    m_lastScheduledPatternId = patternId;
    
    // Schedule at Beat 0.0 (Since Engine loops 0..Length)
    m_patternEngine->schedulePatternInstance(patternId, 0.0, 0);
    
    setPosition(0.0);

    // Start Transport
    if (!isPlaying()) {
        play();
    }
    
    Log::info("[TrackManager] Arsenal Play triggered");
}

void TrackManager::stopArsenalPlayback(bool keepMode) {
    auto& engine = AudioEngine::getInstance();
    Log::info("[TrackManager] stopArsenalPlayback: keepMode=" + std::string(keepMode ? "true" : "false"));
    
    // [FIX] Reset position to 0.0 (Stop behavior vs Pause)
    m_isPlaying.store(false);
    setPosition(0.0);
    
    if (m_commandSink) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 0.0f; // Stopped
        cmd.samplePos = 0; // Reset to start
        m_commandSink(cmd);
    }
    
    // [FIX] ALWAYS flush the pattern engine when stopping to ensure a clean "Stop at 0" state.
    if (m_patternEngine) {
        m_patternEngine->cancelPatternInstance(0);
        m_patternEngine->flush(); // Clear queue and reset refill frame
    }

    // [FIX] Kill ringing voices to provide immediate "Hard Stop" feedback in Arsenal.
    engine.requestVoiceResetOnPatternChange();
    
    // Disable Pattern Mode ONLY if not keeping mode (e.g. for pause/retrigger)
    if (!keepMode) {
        engine.setPatternPlaybackMode(false, 4.0);
    }
}

void TrackManager::record() {
    if (m_isRecording.load()) {
        // Stop recording
        m_isRecording.store(false);
        m_isRecordArmed.store(false);
        finishRecording();
        Log::info("Recording stopped manually");
        return;
    }

    if (m_isPlaying.load()) {
        // Punch-in: Start immediately if already playing
        startRecordingProcess();
    } else {
        // Stop state: Toggle Arm
        bool armed = !m_isRecordArmed.load();
        m_isRecordArmed.store(armed);
        Log::info(armed ? "Recording ARMED" : "Recording UNARMED");
    }
}

void TrackManager::startRecordingProcess() {
    std::lock_guard<std::mutex> lock(m_recordingMutex);
    m_recordingBuffers.clear();
    std::lock_guard<std::mutex> chanLock(m_channelMutex);
    
    double currentBeat = m_positionSeconds.load() * (m_clock.getCurrentTempo() / 60.0);
    
    // B-006: Pre-allocate recording buffers on main thread
    // Use input sample rate if available, otherwise output rate
    uint32_t sampleRate = static_cast<uint32_t>(m_inputSampleRate.load());
    if (sampleRate == 0) {
        sampleRate = static_cast<uint32_t>(m_outputSampleRate.load());
    }
    if (sampleRate == 0) {
        sampleRate = 48000; // Safe fallback
    }
    
    for (const auto& channel : m_channels) {
        if (channel->isArmed()) {
            RecordingBuffer buf;
            buf.trackId = channel->getChannelId();
            buf.startBeat = currentBeat;
            buf.inputChannelIndex = channel->getInputChannelIndex();
            buf.active = true;
            
            // B-006: Pre-allocate buffer for MAX_RECORDING_SECONDS
            // This eliminates all allocations on the audio thread during recording
            buf.preallocate(MAX_RECORDING_SECONDS * sampleRate);
            
            auto it = std::find(m_channels.begin(), m_channels.end(), channel);
            if (it != m_channels.end()) {
                size_t idx = std::distance(m_channels.begin(), it);
                if (idx < m_playlistModel.getLaneCount()) {
                    buf.targetLane = m_playlistModel.getLaneId(idx);
                }
            }
            
            if (buf.trackId > 0)
                m_recordingBuffers.push_back(std::move(buf));
        }
    }
    
    if (!m_recordingBuffers.empty()) {
        m_isRecording.store(true);
        Log::info("Recording started - Armed tracks: " + std::to_string(m_recordingBuffers.size()) + 
                  ", Pre-allocated " + std::to_string(MAX_RECORDING_SECONDS) + "s per track");
    } else {
        Log::warning("Cannot start recording - No tracks are armed");
        m_isRecordArmed.store(false);
    }
}



void TrackManager::processInput(const float* inputBuffer, uint32_t numFrames) {
    // Copy to monitor buffer (always update for monitoring)
    // Uses lock-free spinlock with immediate fallback to avoid RT stalls
    if (inputBuffer && m_inputChannelCount > 0) {
        size_t totalSamples = numFrames * m_inputChannelCount;
        
        // Try to acquire spinlock (non-blocking)
        // If another thread holds it, just skip this update (monitoring is non-critical)
        if (!m_monitorSpinlock.test_and_set(std::memory_order_acquire)) {
            // We got the lock - buffer was pre-allocated in setInputChannelCount
            if (m_monitorBuffer.size() >= totalSamples) {
                std::memcpy(m_monitorBuffer.data(), inputBuffer, totalSamples * sizeof(float));
                m_monitorBufferSize.store(static_cast<uint32_t>(totalSamples), std::memory_order_release);
            }
            m_monitorSpinlock.clear(std::memory_order_release);
        }
        // If we couldn't acquire, skip update - UI will use stale data (acceptable)
    }

    if (!m_isRecording.load()) return;
    
    std::unique_lock<std::mutex> lock(m_recordingMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    // Initialize time on first callback
    if (m_recordingFramesCaptured == 0) {
        m_recordingStartTime = std::chrono::high_resolution_clock::now();
    }
    m_recordingFramesCaptured += numFrames;
    
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
        // B-006: Use getSampleCount() for pre-allocated buffer
        size_t sampleCount = buf.getSampleCount();
        if (sampleCount == 0) {
            Log::warning("Recording buffer empty for Track " + std::to_string(buf.trackId));
            continue;
        }

        // --- Latency Compensation ---
        // B-006: Work with actual sample count, not buffer capacity
        size_t startOffset = 0;
        if (m_latencySamples > 0 && sampleCount > m_latencySamples) {
             Log::info("Compensating latency: Removing " + std::to_string(m_latencySamples) + " samples");
             startOffset = m_latencySamples;
             sampleCount -= m_latencySamples;
        } else if (m_latencySamples > 0) {
             // Edge case: recording shorter than latency
             Log::warning("Recording shorter than latency compensation!");
             sampleCount = 0;
        }

        if (sampleCount == 0) continue;

        Log::info("Saving recording for Track " + std::to_string(buf.trackId) + ". Samples: " + std::to_string(sampleCount));
        
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
            // B-006: Write from pre-allocated buffer with latency offset
            ma_encoder_write_pcm_frames(&encoder, buf.data.data() + startOffset, sampleCount, nullptr);
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
                audioData->numFrames = sampleCount;
                // B-006: Copy only the recorded samples (with latency offset applied)
                audioData->interleavedData.assign(buf.data.begin() + startOffset, 
                                                   buf.data.begin() + startOffset + sampleCount);
                source->setBuffer(audioData);
                
                // Create Pattern (which ClipInstance uses)
                AudioSlicePayload payload;
                payload.audioSourceId = sourceId;
                payload.slices.push_back({0.0, (double)sampleCount});
                
                double durationBeats = (double)sampleCount / (double)finalRate * (m_clock.getCurrentTempo() / 60.0);
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
    if (!m_isPlaying.load()) return;
    
    m_positionSeconds.store(std::max(0.0, seconds));
    m_uiPositionSeconds.store(std::max(0.0, seconds)); // Keep in sync
}


// Audio Processing
// [LEGACY AUDIO PROCESSING REMOVED]
// TrackManager no longer processes audio directly. 
// Use AudioEngine::processBlock which consumes the AudioGraph built from TrackManager state.


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

void TrackManager::rebuildAndPushSnapshot() {
    // Build a new runtime snapshot from the current PlaylistModel state
    auto snapshot = m_playlistModel.buildRuntimeSnapshot(m_patternManager, m_sourceManager);
    if (snapshot) {
        m_snapshotManager.pushSnapshot(std::move(snapshot));
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

void TrackManager::enableMetronome(bool enable) {
    m_metronomeEnabled.store(enable);
    AudioQueueCommand cmd;
    cmd.type = AudioQueueCommandType::SetMetronomeEnabled;
    cmd.value1 = static_cast<float>(enable);
    if (m_commandSink) {
        m_commandSink(cmd);
    }
}



bool TrackManager::getRecordingDataSnapshot(uint32_t trackId, std::vector<float>& outBuffer, double& outStartBeat) {
    std::lock_guard<std::mutex> lock(m_recordingMutex);
    
    // Find active recording buffer for this track
    for (const auto& buffer : m_recordingBuffers) {
        if (buffer.active && buffer.trackId == trackId) {
            // Copy data to output buffer
            outBuffer = buffer.data; 
            outStartBeat = buffer.startBeat;
            return true;
        }
    }
    
    return false;
}

} // namespace Audio
} // namespace Nomad
