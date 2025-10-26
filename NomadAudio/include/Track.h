#pragma once

#include "MixerBus.h"
#include <memory>
#include <vector>
#include <string>
#include <atomic>

namespace Nomad {
namespace Audio {

/**
 * @brief Audio track states
 */
enum class TrackState {
    Empty,      // No audio data
    Loaded,     // Audio file loaded
    Recording,  // Currently recording
    Playing,    // Currently playing
    Paused,     // Playback paused
    Stopped     // Playback stopped
};

/**
 * @brief Audio track class for DAW
 *
 * Manages individual audio tracks with:
 * - Track properties (name, color, volume, pan, mute, solo)
 * - Audio data management (sample buffers, file loading)
 * - Recording functionality
 * - Real-time parameter control
 */
class Track {
public:
    Track(const std::string& name = "Track", uint32_t trackId = 0);
    ~Track();

    // Track Properties
    void setName(const std::string& name);
    const std::string& getName() const { return m_name; }

    void setColor(uint32_t color);  // ARGB format
    uint32_t getColor() const { return m_color; }

    uint32_t getTrackId() const { return m_trackId; }

    // Audio Parameters (thread-safe)
    void setVolume(float volume);  // 0.0 to 1.0
    float getVolume() const { return m_volume.load(); }

    void setPan(float pan);  // -1.0 (left) to 1.0 (right)
    float getPan() const { return m_pan.load(); }

    void setMute(bool mute);
    bool isMuted() const { return m_muted.load(); }

    void setSolo(bool solo);
    bool isSoloed() const { return m_soloed.load(); }
    
    // System track flag (preview, test sound, etc - not affected by transport)
    void setSystemTrack(bool isSystem) { m_isSystemTrack = isSystem; }
    bool isSystemTrack() const { return m_isSystemTrack; }

    // Track State
    void setState(TrackState state);
    TrackState getState() const { return m_state.load(); }

    // Audio Data Management
    bool loadAudioFile(const std::string& filePath);
    bool generatePreviewTone(const std::string& filePath);
    bool generateDemoAudio(const std::string& filePath);
    void setAudioData(const float* data, uint32_t numSamples, uint32_t sampleRate, uint32_t numChannels);
    void clearAudioData();

    // Recording
    void startRecording();
    void stopRecording();
    bool isRecording() const { return m_state.load() == TrackState::Recording; }

    // Playback Control
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return m_state.load() == TrackState::Playing; }

    // Position Control
    void setPosition(double seconds);
    double getPosition() const { return m_positionSeconds.load(); }
    double getDuration() const { return m_durationSeconds.load(); }

    // Audio Processing
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime);

    // Mixer Integration
    MixerBus* getMixerBus() { return m_mixerBus.get(); }
    const MixerBus* getMixerBus() const { return m_mixerBus.get(); }

private:
    // Track identification
    std::string m_name;
    uint32_t m_trackId;
    uint32_t m_color;  // ARGB format
    bool m_isSystemTrack{false};  // System tracks (preview, test sound) aren't affected by transport

    // Audio parameters (atomic for thread safety)
    std::atomic<float> m_volume{1.0f};
    std::atomic<float> m_pan{0.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_soloed{false};

    // Track state
    std::atomic<TrackState> m_state{TrackState::Empty};
    std::atomic<double> m_positionSeconds{0.0};
    std::atomic<double> m_durationSeconds{0.0};

    // Audio data
    std::vector<float> m_audioData;  // Interleaved stereo samples
    uint32_t m_sampleRate{48000};
    uint32_t m_numChannels{2};
    std::atomic<double> m_playbackPhase{0.0};  // For sample-accurate playback

    // Mixer integration
    std::unique_ptr<MixerBus> m_mixerBus;

    // Recording state
    std::vector<float> m_recordingBuffer;
    std::atomic<bool> m_isRecording{false};

    // Internal audio processing
    void generateSilence(float* buffer, uint32_t numFrames);
    void copyAudioData(float* outputBuffer, uint32_t numFrames);
};

} // namespace Audio
} // namespace Nomad
