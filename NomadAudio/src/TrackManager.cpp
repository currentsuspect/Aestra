#include "TrackManager.h"
#include "NomadLog.h"
#include <algorithm>

namespace Nomad {
namespace Audio {

TrackManager::TrackManager() {
    Log::info("TrackManager created");
}

TrackManager::~TrackManager() {
    clearAllTracks();
    Log::info("TrackManager destroyed");
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
    m_isPlaying.store(true);
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
    if (!outputBuffer || numFrames == 0) {
        return;
    }

    // Output buffer should already be cleared by caller
    // Process each track - tracks will mix themselves into the output buffer
    for (const auto& track : m_tracks) {
        if (track && track->isPlaying()) {
            // Track will mix its audio into outputBuffer directly
            track->processAudio(outputBuffer, numFrames, streamTime);
        }
    }

    // Update position
    if (m_isPlaying.load()) {
        double newPosition = m_positionSeconds.load() + (numFrames / 48000.0);
        m_positionSeconds.store(newPosition);
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
