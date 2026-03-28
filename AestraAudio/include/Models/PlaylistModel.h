// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUUID.h"
#include "ClipInstance.h"
#include "PatternManager.h"
#include "PlaylistRuntimeSnapshot.h"
#include "SourceManager.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief A single lane (row) in the playlist
 */
struct PlaylistLane {
    PlaylistLaneID id;
    std::string name;
    int index = 0;
    std::vector<ClipInstance> clips;

    // Lane properties
    float volume{1.0f};
    float pan{0.0f};
    bool muted{false};
    bool solo{false};
    uint32_t colorRGBA{0xFFFFFFFF};
    std::vector<AutomationCurve> automationCurves;

    PlaylistLane() = default;
    explicit PlaylistLane(int idx) : index(idx) { id = PlaylistLaneID::generate(); }
};

/**
 * @brief Multi-lane playlist model with undo/redo support
 *
 * This is the canonical data model for the playlist (arrangement view).
 * All mutations should go through public methods to enable command tracking.
 */
class PlaylistModel {
public:
    using ClipChangedCallback = std::function<void(const ClipInstanceID&)>;

    PlaylistModel() = default;

    // === Lane Management ===

    PlaylistLaneID createLane(const std::string& name = "") {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        PlaylistLane lane(static_cast<int>(m_lanes.size()));
        lane.name = name.empty() ? "Track " + std::to_string(lane.index + 1) : name;

        PlaylistLaneID id = lane.id;
        m_lanes.push_back(std::move(lane));
        m_laneMap[id] = m_lanes.size() - 1;

        return id;
    }

    PlaylistLane* getLane(const PlaylistLaneID& id) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_laneMap.find(id);
        if (it == m_laneMap.end())
            return nullptr;
        return &m_lanes[it->second];
    }

    size_t getLaneCount() const {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        return m_lanes.size();
    }

    // === Clip Management ===

    /**
     * @brief Add a clip to a lane
     * @param laneId Target lane
     * @param clip Clip to add (ID will be generated if invalid)
     * @return The clip's ID
     */
    ClipInstanceID addClip(const PlaylistLaneID& laneId, const ClipInstance& clip) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_laneMap.find(laneId);
        if (it == m_laneMap.end())
            return ClipInstanceID();

        ClipInstance newClip = clip;
        if (!newClip.id.isValid()) {
            newClip.id = ClipInstanceID::generate();
        }

        m_lanes[it->second].clips.push_back(newClip);
        m_clipLaneMap[newClip.id] = laneId;

        notifyClipChanged(newClip.id);
        return newClip.id;
    }

    /**
     * @brief Remove a clip by ID
     */
    void removeClip(const ClipInstanceID& clipId) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end())
            return;

        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end())
            return;

        auto& clips = m_lanes[laneIdxIt->second].clips;
        clips.erase(
            std::remove_if(clips.begin(), clips.end(), [&clipId](const ClipInstance& c) { return c.id == clipId; }),
            clips.end());

        m_clipLaneMap.erase(clipId);
        notifyClipChanged(clipId);
    }

    /**
     * @brief Get a clip by ID
     * @return Pointer to clip, or nullptr if not found
     */
    ClipInstance* getClip(const ClipInstanceID& clipId) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end())
            return nullptr;

        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end())
            return nullptr;

        for (auto& clip : m_lanes[laneIdxIt->second].clips) {
            if (clip.id == clipId)
                return &clip;
        }

        return nullptr;
    }

    /**
     * @brief Find which lane a clip belongs to
     */
    PlaylistLaneID findClipLane(const ClipInstanceID& clipId) const {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_clipLaneMap.find(clipId);
        if (it == m_clipLaneMap.end())
            return PlaylistLaneID();
        return it->second;
    }

    /**
     * @brief Check if a pattern ID is referenced by any clip in the playlist
     */
    bool isPatternUsed(PatternID patternId) const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& lane : m_lanes) {
            for (const auto& clip : lane.clips) {
                if (clip.sourceId == patternId.value)
                    return true;
            }
        }
        return false;
    }

    /**
     * @brief Set the duration of a clip
     */
    void setClipDuration(const ClipInstanceID& clipId, double duration) {
        auto* clip = getClip(clipId);
        if (clip) {
            clip->durationBeats = duration;
            notifyClipChanged(clipId);
        }
    }

    /**
     * @brief Set the start beat of a clip
     */
    void setClipStartBeat(const ClipInstanceID& clipId, double startBeat) {
        auto* clip = getClip(clipId);
        if (clip) {
            clip->startBeat = startBeat;
            notifyClipChanged(clipId);
        }
    }

    /**
     * @brief Move a clip to a new position (and optionally new lane)
     * @param clipId Clip to move
     * @param newStartBeat New start position
     * @param newLaneId New lane (optional, stays in current lane if invalid)
     */
    void moveClip(const ClipInstanceID& clipId, double newStartBeat,
                  const PlaylistLaneID& newLaneId = PlaylistLaneID()) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto* clip = getClipInternal(clipId);
        if (!clip)
            return;

        // Store clip data
        ClipInstance clipCopy = *clip;
        clipCopy.startBeat = newStartBeat;

        // Check if we need to move to a different lane
        if (newLaneId.isValid()) {
            auto oldLaneIt = m_clipLaneMap.find(clipId);
            if (oldLaneIt != m_clipLaneMap.end() && oldLaneIt->second != newLaneId) {
                // Remove from old lane
                auto oldLaneIdxIt = m_laneMap.find(oldLaneIt->second);
                if (oldLaneIdxIt != m_laneMap.end()) {
                    auto& oldClips = m_lanes[oldLaneIdxIt->second].clips;
                    oldClips.erase(std::remove_if(oldClips.begin(), oldClips.end(),
                                                  [&clipId](const ClipInstance& c) { return c.id == clipId; }),
                                   oldClips.end());
                }

                // Add to new lane
                auto newLaneIdxIt = m_laneMap.find(newLaneId);
                if (newLaneIdxIt != m_laneMap.end()) {
                    m_lanes[newLaneIdxIt->second].clips.push_back(clipCopy);
                    m_clipLaneMap[clipId] = newLaneId;
                }

                notifyClipChanged(clipId);
                return;
            }
        }

        // Just update position
        clip->startBeat = newStartBeat;
        notifyClipChanged(clipId);
    }

    /**
     * @brief Split a clip at the given beat position
     * @param clipId Clip to split
     * @param splitBeat Beat position to split at
     * @return ID of the newly created clip (second part)
     */
    ClipInstanceID splitClip(const ClipInstanceID& clipId, double splitBeat) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto* clip = getClipInternal(clipId);
        if (!clip)
            return ClipInstanceID();

        if (splitBeat <= clip->startBeat || splitBeat >= clip->endBeat()) {
            return ClipInstanceID(); // Invalid split position
        }

        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end())
            return ClipInstanceID();

        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end())
            return ClipInstanceID();

        // Create new clip for second part
        ClipInstance newClip;
        newClip.id = ClipInstanceID::generate();
        newClip.startBeat = splitBeat;
        newClip.durationBeats = clip->endBeat() - splitBeat;
        newClip.sourceId = clip->sourceId;
        newClip.sourceOffset = clip->sourceOffset + (splitBeat - clip->startBeat);
        newClip.edits = clip->edits;
        newClip.edits.fadeInBeats = 0.0f; // Clear fades at split point

        // Trim original clip
        clip->durationBeats = splitBeat - clip->startBeat;
        clip->edits.fadeOutBeats = 0.0f;

        // Add new clip
        m_lanes[laneIdxIt->second].clips.push_back(newClip);
        m_clipLaneMap[newClip.id] = laneIt->second;

        notifyClipChanged(clipId);
        notifyClipChanged(newClip.id);

        return newClip.id;
    }

    // === Callbacks ===

    void setClipChangedCallback(ClipChangedCallback callback) { m_clipChangedCallback = std::move(callback); }

    // === Runtime Snapshot ===

    /**
     * @brief Build a runtime snapshot for audio rendering
     * @param patterns Pattern manager for MIDI patterns
     * @param sources Source manager for audio data
     * @return Runtime snapshot (nullptr if empty)
     */
    std::unique_ptr<PlaylistRuntimeSnapshot> buildRuntimeSnapshot(const PatternManager& patterns,
                                                                  const SourceManager& sources) const {
        // Use shared_lock for readers (audio thread) - allows concurrent reads
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (m_lanes.empty()) {
            return nullptr;
        }

        auto snapshot = std::make_unique<PlaylistRuntimeSnapshot>();
        snapshot->lanes.reserve(m_lanes.size());
        snapshot->bpm = 120.0;               // Default BPM
        snapshot->projectSampleRate = 48000; // Default sample rate

        const double samplesPerBeat = (48000.0 * 60.0) / snapshot->bpm;

        for (const auto& lane : m_lanes) {
            LaneRuntimeInfo laneInfo;
            laneInfo.clips.reserve(lane.clips.size());

            for (const auto& clip : lane.clips) {
                ClipRuntimeInfo clipInfo;
                clipInfo.startTime = static_cast<uint64_t>(clip.startBeat * samplesPerBeat);
                clipInfo.duration = static_cast<uint64_t>(clip.durationBeats * samplesPerBeat);
                clipInfo.sourceStart = static_cast<uint64_t>(clip.sourceOffset * samplesPerBeat);
                clipInfo.gainLinear = clip.edits.gain;
                clipInfo.isAudioClip = true;

                // Resolve audio data through pattern → AudioSlicePayload → source
                if (clip.patternId.isValid()) {
                    auto* pattern = patterns.getPattern(clip.patternId);
                    if (pattern && pattern->isAudio()) {
                        auto& audioPayload = std::get<AudioSlicePayload>(pattern->payload);
                        if (auto* source = sources.getSource(audioPayload.audioSourceId)) {
                            clipInfo.audioData = const_cast<AudioBufferData*>(source->getRawBuffer());
                            if (clipInfo.audioData) {
                                clipInfo.sourceSampleRate = clipInfo.audioData->sampleRate;
                                clipInfo.sourceChannels = clipInfo.audioData->numChannels;
                            }
                        }
                    } else if (pattern && pattern->isMidi()) {
                        clipInfo.isAudioClip = false;
                    }
                }

                laneInfo.clips.push_back(clipInfo);
            }

            snapshot->lanes.push_back(std::move(laneInfo));
        }

        return snapshot;
    }

    /**
     * @brief Get the project sample rate
     */
    double getProjectSampleRate() const {
        return 48000.0; // Default for now
    }

    /**
     * @brief Set BPM
     */
    void setBPM(double bpm) { m_bpm = bpm; }

    /**
     * @brief Get BPM
     */
    double getBPM() const { return m_bpm; }

    double beatToSeconds(double beats) const {
        if (m_bpm <= 0.0) {
            return 0.0;
        }
        return beats * 60.0 / m_bpm;
    }

    double secondsToBeats(double seconds) const {
        return seconds * m_bpm / 60.0;
    }

    double getTotalDurationBeats() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        double maxBeat = 0.0;
        for (const auto& lane : m_lanes) {
            for (const auto& clip : lane.clips) {
                maxBeat = std::max(maxBeat, clip.endBeat());
            }
        }
        return maxBeat;
    }

    /**
     * @brief Set pattern manager reference
     */
    void setPatternManager(PatternManager* pm) { m_patternManager = pm; }

    /**
     * @brief Get lane ID by index
     */
    PlaylistLaneID getLaneId(size_t index) const {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (index < m_lanes.size()) {
            return m_lanes[index].id;
        }
        return PlaylistLaneID();
    }

    /**
     * @brief Add a clip from a pattern
     */
    ClipInstanceID addClipFromPattern(const PlaylistLaneID& laneId, PatternID patternId, double startBeat,
                                      double durationBeats) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_laneMap.find(laneId);
        if (it == m_laneMap.end())
            return ClipInstanceID();

        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.startBeat = startBeat;
        clip.durationBeats = durationBeats;
        clip.patternId = patternId;
        clip.sourceId = patternId.value; // Store pattern ID in sourceId for now

        m_lanes[it->second].clips.push_back(clip);
        m_clipLaneMap[clip.id] = laneId;

        notifyClipChanged(clip.id);
        return clip.id;
    }

    /**
     * @brief Get all lane IDs
     */
    std::vector<PlaylistLaneID> getLaneIDs() const {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        std::vector<PlaylistLaneID> ids;
        ids.reserve(m_lanes.size());
        for (const auto& lane : m_lanes) {
            ids.push_back(lane.id);
        }
        return ids;
    }

    /**
     * @brief Scoped batch update (no-op for now)
     */
    struct BatchUpdateScope {
        ~BatchUpdateScope() {}
    };

    BatchUpdateScope scopedBatchUpdate() { return BatchUpdateScope{}; }

    /**
     * @brief Remove a lane by ID
     * @param laneId Lane to remove
     * @return true if removed, false if not found
     */
    bool removeLane(const PlaylistLaneID& laneId) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_laneMap.find(laneId);
        if (it == m_laneMap.end())
            return false;

        size_t laneIdx = it->second;

        // Remove all clips in this lane from the clip lane map
        for (const auto& clip : m_lanes[laneIdx].clips) {
            m_clipLaneMap.erase(clip.id);
        }

        // Remove the lane
        m_lanes.erase(m_lanes.begin() + laneIdx);

        // Rebuild lane map with correct indices
        m_laneMap.clear();
        for (size_t i = 0; i < m_lanes.size(); ++i) {
            m_laneMap[m_lanes[i].id] = i;
        }

        return true;
    }

    /**
     * @brief Clear all lanes and clips
     */
    void clear() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_lanes.clear();
        m_laneMap.clear();
        m_clipLaneMap.clear();
    }

private:
    std::vector<PlaylistLane> m_lanes;
    std::unordered_map<PlaylistLaneID, size_t> m_laneMap;
    std::unordered_map<ClipInstanceID, PlaylistLaneID> m_clipLaneMap;
    mutable std::shared_mutex m_mutex;
    ClipChangedCallback m_clipChangedCallback;
    double m_bpm{120.0};
    PatternManager* m_patternManager{nullptr};

    ClipInstance* getClipInternal(const ClipInstanceID& clipId) {
        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end())
            return nullptr;

        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end())
            return nullptr;

        for (auto& clip : m_lanes[laneIdxIt->second].clips) {
            if (clip.id == clipId)
                return &clip;
        }

        return nullptr;
    }

    void notifyClipChanged(const ClipInstanceID& clipId) {
        if (m_clipChangedCallback) {
            m_clipChangedCallback(clipId);
        }
    }
};

} // namespace Audio
} // namespace Aestra
