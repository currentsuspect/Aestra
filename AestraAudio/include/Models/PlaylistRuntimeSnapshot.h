#pragma once
#include "../Core/AutomationCurve.h"
#include "ClipSource.h"
#include "TimeTypes.h"

#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
class PatternManager;
class SourceManager;

/**
 * @brief Runtime clip information for audio rendering
 */
struct ClipRuntimeInfo {
    // Audio data pointer (owned by SourceManager)
    AudioBufferData* audioData{nullptr};

    // Timing (in project samples)
    uint64_t startTime{0};   // Absolute start position
    uint64_t duration{0};    // Duration in samples
    uint64_t sourceStart{0}; // Offset into source material

    // Source properties
    uint32_t sourceSampleRate{44100};
    uint32_t sourceChannels{2};

    // Parameters
    float gainLinear{1.0f};
    float pan{0.0f};

    // Type
    bool isAudioClip{true};

    bool isValid() const { return audioData != nullptr && audioData->isValid(); }

    bool isAudio() const { return isAudioClip; }

    uint64_t getEndTime() const { return startTime + duration; }
};

/**
 * @brief Runtime lane information
 */
struct LaneRuntimeInfo {
    std::vector<ClipRuntimeInfo> clips;
    std::vector<AutomationCurve> automationCurves;
};

/**
 * @brief Runtime snapshot of playlist for audio rendering
 */
struct PlaylistRuntimeSnapshot {
    std::vector<LaneRuntimeInfo> lanes;
    double bpm{120.0};
    uint32_t projectSampleRate{48000};

    /**
     * @brief Get the project sample rate
     */
    double getProjectSampleRate() const { return static_cast<double>(projectSampleRate); }
};

} // namespace Audio
} // namespace Aestra