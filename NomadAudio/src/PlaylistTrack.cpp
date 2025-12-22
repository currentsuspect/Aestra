// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PlaylistTrack.h"
#include "NomadLog.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Nomad {
namespace Audio {

PlaylistTrack::PlaylistTrack(const std::string& name, uint32_t trackIndex)
    : m_uuid(PlaylistTrackUUID::generate())
    , m_name(name)
    , m_color(0xFF4A90D9)
    , m_trackIndex(trackIndex)
    , m_volume(1.0f)
    , m_pan(0.0f)
    , m_muted(false)
    , m_soloed(false)
    , m_isSystemTrack(false)
{
    Log::info("PlaylistTrack created: " + m_name + " (index: " + std::to_string(trackIndex) + ")");
}

void PlaylistTrack::addClip(std::shared_ptr<AudioClip> clip) {
    if (!clip) return;
    
    std::lock_guard<std::mutex> lock(m_clipMutex);
    m_clips.push_back(clip);
    sortClips();
    
    Log::info("Clip '" + clip->getName() + "' added to track '" + m_name + 
              "' at " + std::to_string(clip->getStartTime()) + "s");
}

bool PlaylistTrack::removeClip(std::shared_ptr<AudioClip> clip) {
    if (!clip) return false;
    return removeClip(clip->getUUID());
}

bool PlaylistTrack::removeClip(const ClipUUID& uuid) {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    auto it = std::find_if(m_clips.begin(), m_clips.end(),
        [&uuid](const std::shared_ptr<AudioClip>& clip) {
            return clip && clip->getUUID() == uuid;
        });
    
    if (it != m_clips.end()) {
        std::string clipName = (*it)->getName();
        m_clips.erase(it);
        Log::info("Clip '" + clipName + "' removed from track '" + m_name + "'");
        return true;
    }
    return false;
}

std::shared_ptr<AudioClip> PlaylistTrack::getClipAtPosition(double timelinePosition) const {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    for (const auto& clip : m_clips) {
        if (clip && clip->containsTimelinePosition(timelinePosition)) {
            return clip;
        }
    }
    return nullptr;
}

std::shared_ptr<AudioClip> PlaylistTrack::getClipByUUID(const ClipUUID& uuid) const {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    for (const auto& clip : m_clips) {
        if (clip && clip->getUUID() == uuid) {
            return clip;
        }
    }
    return nullptr;
}

std::shared_ptr<AudioClip> PlaylistTrack::getClip(size_t index) const {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    if (index < m_clips.size()) {
        return m_clips[index];
    }
    return nullptr;
}

std::shared_ptr<AudioClip> PlaylistTrack::splitClipAt(double timelinePosition) {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    // Find clip at this position
    std::shared_ptr<AudioClip> targetClip = nullptr;
    for (const auto& clip : m_clips) {
        if (clip && clip->containsTimelinePosition(timelinePosition)) {
            targetClip = clip;
            break;
        }
    }
    
    if (!targetClip) {
        Log::warning("PlaylistTrack::splitClipAt: No clip at position " + std::to_string(timelinePosition));
        return nullptr;
    }
    
    // Calculate position within the clip
    double positionInClip = timelinePosition - targetClip->getStartTime();
    
    // Perform the split
    auto newClip = targetClip->splitAt(positionInClip);
    
    if (newClip) {
        // Add the new clip to this track (no mutex - already locked)
        m_clips.push_back(newClip);
        sortClips();
        
        Log::info("Clip split at " + std::to_string(timelinePosition) + 
                  "s in track '" + m_name + "'");
    }
    
    return newClip;
}

void PlaylistTrack::clearClips() {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    m_clips.clear();
    Log::info("All clips cleared from track '" + m_name + "'");
}

double PlaylistTrack::getTotalDuration() const {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    double maxEnd = 0.0;
    for (const auto& clip : m_clips) {
        if (clip) {
            maxEnd = std::max(maxEnd, clip->getEndTime());
        }
    }
    return maxEnd;
}

double PlaylistTrack::getEarliestStartTime() const {
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    if (m_clips.empty()) return 0.0;
    
    double minStart = std::numeric_limits<double>::max();
    for (const auto& clip : m_clips) {
        if (clip) {
            minStart = std::min(minStart, clip->getStartTime());
        }
    }
    return minStart == std::numeric_limits<double>::max() ? 0.0 : minStart;
}

void PlaylistTrack::processAudio(float* outputBuffer, uint32_t numFrames, double timelinePosition, double outputSampleRate) {
    if (!outputBuffer || numFrames == 0) return;
    
    // Initialize output to silence
    std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
    
    // Skip if muted
    if (m_muted.load()) return;
    
    // Ensure scratch buffer is large enough
    size_t bufferSize = numFrames * 2;
    if (m_scratchBuffer.size() < bufferSize) {
        m_scratchBuffer.resize(bufferSize);
    }
    
    // Get track parameters
    float volume = m_volume.load();
    float pan = m_pan.load();
    
    // Calculate pan gains (constant power)
    float panAngle = (pan + 1.0f) * 0.25f * 3.14159265f;  // 0 to PI/2
    float leftGain = std::cos(panAngle) * volume;
    float rightGain = std::sin(panAngle) * volume;
    
    // Process each clip
    std::lock_guard<std::mutex> lock(m_clipMutex);
    
    for (const auto& clip : m_clips) {
        if (!clip) continue;
        
        // Check if clip is playing in this time range
        double clipStart = clip->getStartTime();
        double clipEnd = clip->getEndTime();
        double bufferEnd = timelinePosition + (numFrames / outputSampleRate);
        
        if (bufferEnd < clipStart || timelinePosition >= clipEnd) {
            continue;  // Clip not active in this buffer
        }
        
        // Clear scratch buffer
        std::memset(m_scratchBuffer.data(), 0, bufferSize * sizeof(float));
        
        // Process clip audio
        clip->processAudio(m_scratchBuffer.data(), numFrames, timelinePosition, outputSampleRate);
        
        // Mix into output with pan
        for (uint32_t i = 0; i < numFrames; ++i) {
            float left = m_scratchBuffer[i * 2];
            float right = m_scratchBuffer[i * 2 + 1];
            
            outputBuffer[i * 2] += left * leftGain;
            outputBuffer[i * 2 + 1] += right * rightGain;
        }
    }
}

void PlaylistTrack::sortClips() {
    // Sort clips by start time (must be called with mutex held)
    std::sort(m_clips.begin(), m_clips.end(),
        [](const std::shared_ptr<AudioClip>& a, const std::shared_ptr<AudioClip>& b) {
            if (!a) return false;
            if (!b) return true;
            return a->getStartTime() < b->getStartTime();
        });
}

} // namespace Audio
} // namespace Nomad
