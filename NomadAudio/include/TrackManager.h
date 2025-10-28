#pragma once

#include "Track.h"
#include <memory>
#include <vector>
#include <atomic>

namespace Nomad {
namespace Audio {

/**
 * @brief Manages multiple audio tracks for the DAW
 *
 * Coordinates playback, recording, and mixing of multiple tracks.
 * Provides high-level DAW functionality like transport control,
 * track management, and audio routing.
 */
class TrackManager {
public:
    TrackManager();
    ~TrackManager();

    // Track Management
    std::shared_ptr<Track> addTrack(const std::string& name = "");
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
    double getPosition() const { return m_positionSeconds.load(); }

    double getTotalDuration() const;
    
    // Position update callback (called during playback to update transport UI)
    void setOnPositionUpdate(std::function<void(double)> callback) { m_onPositionUpdate = callback; }

    // Audio Processing
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime);

    // Mixer Integration
    void updateMixer();

    // Solo/Mute Management
    void clearAllSolos();

private:
    // Track collection
    std::vector<std::shared_ptr<Track>> m_tracks;

    // Transport state
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isRecording{false};
    std::atomic<double> m_positionSeconds{0.0};

    // Track ID counter
    std::atomic<uint32_t> m_nextTrackId{1};
    
    // Callbacks
    std::function<void(double)> m_onPositionUpdate;

    // Track creation helper
    std::string generateTrackName() const;
};

} // namespace Audio
} // namespace Nomad
