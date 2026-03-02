// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"
#include "../AestraUUID.h"
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>

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
    
    PlaylistLane() = default;
    explicit PlaylistLane(int idx) : index(idx) {
        id = PlaylistLaneID::generate();
    }
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
        std::lock_guard<std::mutex> lock(m_mutex);
        
        PlaylistLane lane(static_cast<int>(m_lanes.size()));
        lane.name = name.empty() ? "Track " + std::to_string(lane.index + 1) : name;
        
        PlaylistLaneID id = lane.id;
        m_lanes.push_back(std::move(lane));
        m_laneMap[id] = m_lanes.size() - 1;
        
        return id;
    }
    
    PlaylistLane* getLane(const PlaylistLaneID& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_laneMap.find(id);
        if (it == m_laneMap.end()) return nullptr;
        return &m_lanes[it->second];
    }
    
    size_t getLaneCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
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
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_laneMap.find(laneId);
        if (it == m_laneMap.end()) return ClipInstanceID();
        
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
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end()) return;
        
        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end()) return;
        
        auto& clips = m_lanes[laneIdxIt->second].clips;
        clips.erase(std::remove_if(clips.begin(), clips.end(),
            [&clipId](const ClipInstance& c) { return c.id == clipId; }),
            clips.end());
        
        m_clipLaneMap.erase(clipId);
        notifyClipChanged(clipId);
    }
    
    /**
     * @brief Get a clip by ID
     * @return Pointer to clip, or nullptr if not found
     */
    ClipInstance* getClip(const ClipInstanceID& clipId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end()) return nullptr;
        
        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end()) return nullptr;
        
        for (auto& clip : m_lanes[laneIdxIt->second].clips) {
            if (clip.id == clipId) return &clip;
        }
        
        return nullptr;
    }
    
    /**
     * @brief Find which lane a clip belongs to
     */
    PlaylistLaneID findClipLane(const ClipInstanceID& clipId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_clipLaneMap.find(clipId);
        if (it == m_clipLaneMap.end()) return PlaylistLaneID();
        return it->second;
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
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto* clip = getClipInternal(clipId);
        if (!clip) return;
        
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
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto* clip = getClipInternal(clipId);
        if (!clip) return ClipInstanceID();
        
        if (splitBeat <= clip->startBeat || splitBeat >= clip->endBeat()) {
            return ClipInstanceID(); // Invalid split position
        }
        
        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end()) return ClipInstanceID();
        
        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end()) return ClipInstanceID();
        
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
    
    void setClipChangedCallback(ClipChangedCallback callback) {
        m_clipChangedCallback = std::move(callback);
    }
    
private:
    std::vector<PlaylistLane> m_lanes;
    std::unordered_map<PlaylistLaneID, size_t> m_laneMap;
    std::unordered_map<ClipInstanceID, PlaylistLaneID> m_clipLaneMap;
    mutable std::mutex m_mutex;
    ClipChangedCallback m_clipChangedCallback;
    
    ClipInstance* getClipInternal(const ClipInstanceID& clipId) {
        auto laneIt = m_clipLaneMap.find(clipId);
        if (laneIt == m_clipLaneMap.end()) return nullptr;
        
        auto laneIdxIt = m_laneMap.find(laneIt->second);
        if (laneIdxIt == m_laneMap.end()) return nullptr;
        
        for (auto& clip : m_lanes[laneIdxIt->second].clips) {
            if (clip.id == clipId) return &clip;
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
