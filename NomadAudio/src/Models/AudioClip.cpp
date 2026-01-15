// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AudioClip.h"
#include "NomadLog.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Nomad {
namespace Audio {

AudioClip::AudioClip(const std::string& name)
    : m_uuid(ClipUUID::generate())
    , m_name(name)
    , m_color(0xFF4A90D9)
    , m_startTime(0.0)
    , m_sampleRate(44100)
    , m_numChannels(2)
    , m_trimStart(0.0)
    , m_trimEnd(0.0)
    , m_gain(1.0f)
{
    Log::info("AudioClip created: " + m_name + " (UUID: " + m_uuid.toString() + ")");
}

AudioClip::AudioClip(const float* audioData, uint32_t numSamples, uint32_t sampleRate,
                     uint32_t numChannels, const std::string& name)
    : m_uuid(ClipUUID::generate())
    , m_name(name)
    , m_color(0xFF4A90D9)
    , m_startTime(0.0)
    , m_sampleRate(sampleRate)
    , m_numChannels(numChannels)
    , m_trimStart(0.0)
    , m_trimEnd(0.0)
    , m_gain(1.0f)
{
    if (audioData && numSamples > 0 && numChannels > 0) {
        size_t totalSamples = static_cast<size_t>(numSamples) * numChannels;
        m_audioData.assign(audioData, audioData + totalSamples);
    }
    Log::info("AudioClip created with data: " + m_name + " (" + std::to_string(numSamples) + 
              " samples, " + std::to_string(sampleRate) + " Hz, " + std::to_string(numChannels) + " ch)");
}

void AudioClip::setAudioData(const float* data, uint32_t numSamples, uint32_t sampleRate, uint32_t numChannels) {
    if (!data || numSamples == 0 || numChannels == 0) {
        Log::warning("AudioClip::setAudioData: Invalid parameters");
        return;
    }
    
    m_sampleRate = sampleRate;
    m_numChannels = numChannels;
    
    size_t totalSamples = static_cast<size_t>(numSamples) * numChannels;
    m_audioData.assign(data, data + totalSamples);
    
    // Reset trim to full length
    m_trimStart = 0.0;
    m_trimEnd = 0.0;
    
    Log::info("AudioClip '" + m_name + "' data set: " + std::to_string(numSamples) + 
              " samples, " + std::to_string(getSourceDuration()) + "s");
}

void AudioClip::clearAudioData() {
    m_audioData.clear();
    m_audioData.shrink_to_fit();
    m_trimStart = 0.0;
    m_trimEnd = 0.0;
    Log::info("AudioClip '" + m_name + "' data cleared");
}

double AudioClip::getSourceDuration() const {
    if (m_audioData.empty() || m_sampleRate == 0 || m_numChannels == 0) {
        return 0.0;
    }
    return static_cast<double>(m_audioData.size() / m_numChannels) / static_cast<double>(m_sampleRate);
}

void AudioClip::setTrimStart(double seconds) {
    double sourceDuration = getSourceDuration();
    m_trimStart = std::clamp(seconds, 0.0, sourceDuration);
    
    // Ensure trim end is still valid
    if (m_trimEnd > 0.0 && m_trimStart >= m_trimEnd) {
        m_trimEnd = sourceDuration;
    }
}

void AudioClip::setTrimEnd(double seconds) {
    double sourceDuration = getSourceDuration();
    if (seconds <= 0.0) {
        m_trimEnd = 0.0;  // 0 means use full length
    } else {
        m_trimEnd = std::clamp(seconds, m_trimStart + 0.001, sourceDuration);
    }
}

double AudioClip::getTrimmedDuration() const {
    double sourceDuration = getSourceDuration();
    if (sourceDuration <= 0.0) return 0.0;
    
    double effectiveEnd = (m_trimEnd > 0.0) ? m_trimEnd : sourceDuration;
    return std::max(0.0, effectiveEnd - m_trimStart);
}

void AudioClip::resetTrim() {
    m_trimStart = 0.0;
    m_trimEnd = 0.0;
    Log::info("AudioClip '" + m_name + "' trim reset");
}

std::shared_ptr<AudioClip> AudioClip::splitAt(double positionInClip) {
    double trimmedDuration = getTrimmedDuration();
    
    // Validate split position
    if (positionInClip <= 0.0 || positionInClip >= trimmedDuration) {
        Log::warning("AudioClip::splitAt: Invalid position " + std::to_string(positionInClip) + 
                     "s (clip duration: " + std::to_string(trimmedDuration) + "s)");
        return nullptr;
    }
    
    // Calculate the absolute position in source audio
    double splitPointInSource = m_trimStart + positionInClip;
    
    // Create new clip for the second half
    auto newClip = std::make_shared<AudioClip>(m_name);
    newClip->m_color = m_color;
    newClip->m_sourcePath = m_sourcePath;
    newClip->m_gain = m_gain;
    
    // Copy audio data to new clip (same source data)
    newClip->m_audioData = m_audioData;
    newClip->m_sampleRate = m_sampleRate;
    newClip->m_numChannels = m_numChannels;
    
    // Set trim for new clip: starts at split point, ends where original ended
    newClip->m_trimStart = splitPointInSource;
    newClip->m_trimEnd = m_trimEnd;
    
    // Set timeline position for new clip: continues where first clip ends
    newClip->m_startTime = m_startTime + positionInClip;
    
    // Modify this clip: trim end at split point
    double originalEnd = m_trimEnd;
    m_trimEnd = splitPointInSource;
    
    Log::info("AudioClip '" + m_name + "' split at " + std::to_string(positionInClip) + 
              "s - first part: " + std::to_string(getTrimmedDuration()) + 
              "s, second part: " + std::to_string(newClip->getTrimmedDuration()) + "s");
    
    return newClip;
}

std::shared_ptr<AudioClip> AudioClip::duplicate() const {
    auto newClip = std::make_shared<AudioClip>(m_name);
    newClip->m_color = m_color;
    newClip->m_sourcePath = m_sourcePath;
    newClip->m_gain = m_gain;
    newClip->m_startTime = m_startTime;
    newClip->m_audioData = m_audioData;
    newClip->m_sampleRate = m_sampleRate;
    newClip->m_numChannels = m_numChannels;
    newClip->m_trimStart = m_trimStart;
    newClip->m_trimEnd = m_trimEnd;
    
    Log::info("AudioClip '" + m_name + "' duplicated (new UUID: " + newClip->m_uuid.toString() + ")");
    return newClip;
}

bool AudioClip::containsTimelinePosition(double timelinePosition) const {
    return timelinePosition >= m_startTime && timelinePosition < getEndTime();
}

double AudioClip::timelineToSourcePosition(double timelinePosition) const {
    if (!containsTimelinePosition(timelinePosition)) {
        return -1.0;
    }
    // Convert timeline position to position within trimmed clip, then add trim start
    double positionInClip = timelinePosition - m_startTime;
    return m_trimStart + positionInClip;
}

void AudioClip::processAudio(float* outputBuffer, uint32_t numFrames, double timelinePosition, double outputSampleRate) {
    if (!outputBuffer || numFrames == 0 || m_audioData.empty()) {
        return;
    }
    
    // Check if playhead is within this clip
    double clipEnd = getEndTime();
    if (timelinePosition >= clipEnd || timelinePosition + (numFrames / outputSampleRate) < m_startTime) {
        // Clip is not playing at this position - output silence
        std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
        return;
    }
    
    // Calculate position within source audio
    double sourcePosition = timelineToSourcePosition(timelinePosition);
    if (sourcePosition < 0.0) {
        sourcePosition = m_trimStart;
    }
    
    // Calculate effective end position
    double effectiveEnd = (m_trimEnd > 0.0) ? m_trimEnd : getSourceDuration();
    
    // Process frames
    double sampleRateRatio = static_cast<double>(m_sampleRate) / outputSampleRate;
    
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        double currentTimelinePos = timelinePosition + (frame / outputSampleRate);
        
        // Check if we're within the clip bounds
        if (currentTimelinePos < m_startTime || currentTimelinePos >= clipEnd) {
            outputBuffer[frame * 2] = 0.0f;
            outputBuffer[frame * 2 + 1] = 0.0f;
            continue;
        }
        
        // Calculate source sample position
        double posInClip = currentTimelinePos - m_startTime;
        double posInSource = m_trimStart + posInClip;
        
        // Check if within trimmed bounds
        if (posInSource < m_trimStart || posInSource >= effectiveEnd) {
            outputBuffer[frame * 2] = 0.0f;
            outputBuffer[frame * 2 + 1] = 0.0f;
            continue;
        }
        
        // Calculate sample index (with sample rate conversion)
        double sampleIndex = posInSource * m_sampleRate;
        size_t idx = static_cast<size_t>(sampleIndex);
        float frac = static_cast<float>(sampleIndex - idx);
        
        // Bounds check
        size_t maxIdx = m_audioData.size() / m_numChannels - 1;
        if (idx >= maxIdx) {
            outputBuffer[frame * 2] = 0.0f;
            outputBuffer[frame * 2 + 1] = 0.0f;
            continue;
        }
        
        // Linear interpolation
        float left0, right0, left1, right1;
        if (m_numChannels == 2) {
            left0 = m_audioData[idx * 2];
            right0 = m_audioData[idx * 2 + 1];
            left1 = m_audioData[(idx + 1) * 2];
            right1 = m_audioData[(idx + 1) * 2 + 1];
        } else {
            // Mono to stereo
            left0 = right0 = m_audioData[idx];
            left1 = right1 = m_audioData[idx + 1];
        }
        
        float left = left0 + frac * (left1 - left0);
        float right = right0 + frac * (right1 - right0);
        
        // Apply gain
        outputBuffer[frame * 2] = left * m_gain;
        outputBuffer[frame * 2 + 1] = right * m_gain;
    }
}

} // namespace Audio
} // namespace Nomad
