// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PlaylistClip.h"
#include "ClipSource.h"
#include "TimeTypes.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace Nomad {
namespace Audio {

// Forward declarations
class PlaylistRuntimeSnapshot;

// =============================================================================
// PlaylistLaneID - Unique track/lane identity
// =============================================================================

/**
 * @brief Unique identifier for a playlist lane
 */
struct PlaylistLaneID {
    uint64_t high = 0;
    uint64_t low = 0;
    
    bool isValid() const { return high != 0 || low != 0; }
    bool operator==(const PlaylistLaneID& other) const { 
        return high == other.high && low == other.low; 
    }
    bool operator!=(const PlaylistLaneID& other) const { return !(*this == other); }
    bool operator<(const PlaylistLaneID& other) const {
        return high < other.high || (high == other.high && low < other.low);
    }
    
    std::string toString() const {
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 static_cast<uint32_t>(high >> 32),
                 static_cast<uint16_t>(high >> 16),
                 static_cast<uint16_t>(high),
                 static_cast<uint16_t>(low >> 48),
                 static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));
        return std::string(buf);
    }
    
    static PlaylistLaneID generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        
        PlaylistLaneID id;
        id.high = dis(gen);
        id.low = dis(gen);
        id.high = (id.high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        id.low = (id.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        return id;
    }
    
    struct Hash {
        size_t operator()(const PlaylistLaneID& id) const {
            return std::hash<uint64_t>()(id.high) ^ 
                   (std::hash<uint64_t>()(id.low) << 1);
        }
    };
};

// =============================================================================
// PlaylistLane - A horizontal lane that contains clips
// =============================================================================

/**
 * @brief A single lane in the playlist view
 * 
 * Represents a horizontal "track" or "lane" in the DAW's playlist.
 * Contains multiple clips, each at their own timeline position.
 * 
 * Invariant: clips vector is ALWAYS sorted by startTime.
 */
struct PlaylistLane {
    PlaylistLaneID id;
    std::string name;
    
    // Clips (always sorted by startTime)
    std::vector<PlaylistClip> clips;
    
    // Lane properties
    float volume = 1.0f;                       ///< Master volume for lane
    float pan = 0.0f;                          ///< Pan position
    bool muted = false;
    bool solo = false;
    bool armed = false;                        ///< Armed for recording
    
    // UI properties
    uint32_t colorRGBA = 0xFF4A90D9;
    float height = 100.0f;                     ///< Lane height in pixels
    bool collapsed = false;
    
    // Constructors
    PlaylistLane() : id(PlaylistLaneID::generate()) {}
    explicit PlaylistLane(const std::string& laneName) 
        : id(PlaylistLaneID::generate()), name(laneName) {}
    
    // === Clip Sorting ===
    void sortClips() {
        std::sort(clips.begin(), clips.end(), compareByStartTime);
    }
    
    // === Queries ===
    
    /// Find clip index by ID, returns -1 if not found
    int findClipIndex(const PlaylistClipID& clipId) const {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].id == clipId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    
    /// Get clip by ID
    PlaylistClip* getClip(const PlaylistClipID& clipId) {
        int idx = findClipIndex(clipId);
        return idx >= 0 ? &clips[idx] : nullptr;
    }
    
    const PlaylistClip* getClip(const PlaylistClipID& clipId) const {
        int idx = findClipIndex(clipId);
        return idx >= 0 ? &clips[idx] : nullptr;
    }
    
    /// Get clip at timeline position
    PlaylistClip* getClipAtPosition(SampleIndex position) {
        for (auto& clip : clips) {
            if (clip.containsTime(position)) {
                return &clip;
            }
        }
        return nullptr;
    }
    
    /// Get all clips overlapping a range
    std::vector<const PlaylistClip*> getClipsInRange(SampleIndex rangeStart, SampleIndex rangeEnd) const {
        std::vector<const PlaylistClip*> result;
        
        // Binary search for first potentially overlapping clip
        auto it = std::lower_bound(clips.begin(), clips.end(), rangeStart,
            [](const PlaylistClip& clip, SampleIndex pos) {
                return clip.getEndTime() <= pos;
            });
        
        // Walk forward until clips start after range ends
        while (it != clips.end() && it->startTime < rangeEnd) {
            if (it->overlapsRange(rangeStart, rangeEnd)) {
                result.push_back(&(*it));
            }
            ++it;
        }
        
        return result;
    }
    
    /// Get total duration (end of last clip)
    SampleIndex getTotalDuration() const {
        if (clips.empty()) return 0;
        SampleIndex maxEnd = 0;
        for (const auto& clip : clips) {
            maxEnd = std::max(maxEnd, clip.getEndTime());
        }
        return maxEnd;
    }
    
    /// Check if lane is empty
    bool isEmpty() const { return clips.empty(); }
};

// =============================================================================
// PlaylistModel - Central playlist controller
// =============================================================================

/**
 * @brief Central model for all playlist data and operations
 * 
 * This class manages:
 * - All playlist lanes and their clips
 * - CRUD operations for clips (add, remove, move, trim, split)
 * - Range queries for efficient playback
 * - Snapshot generation for RT thread
 * 
 * IMPORTANT: This class runs on the UI/Engine thread only.
 * The RT thread receives data through PlaylistRuntimeSnapshot.
 * 
 * Thread safety:
 * - All public methods are thread-safe (internal mutex)
 * - Observers are called with mutex held; keep callbacks fast
 */
class PlaylistModel {
public:
    /// Observer callback types
    using ChangeCallback = std::function<void()>;
    
    PlaylistModel();
    ~PlaylistModel();
    
    // Non-copyable
    PlaylistModel(const PlaylistModel&) = delete;
    PlaylistModel& operator=(const PlaylistModel&) = delete;
    
    // === Project Settings ===
    void setProjectSampleRate(double sampleRate);
    double getProjectSampleRate() const;
    
    void setBPM(double bpm);
    double getBPM() const;
    
    void setGridSubdivision(GridSubdivision grid);
    GridSubdivision getGridSubdivision() const;
    
    void setSnapEnabled(bool enabled);
    bool isSnapEnabled() const;
    
    // === Lane Management ===
    
    /// Create a new lane, returns its ID
    PlaylistLaneID createLane(const std::string& name = "");
    
    /// Delete a lane and all its clips
    bool deleteLane(PlaylistLaneID laneId);
    
    /// Get lane by ID
    PlaylistLane* getLane(PlaylistLaneID laneId);
    const PlaylistLane* getLane(PlaylistLaneID laneId) const;
    
    /// Get lane by index
    PlaylistLane* getLaneByIndex(size_t index);
    const PlaylistLane* getLaneByIndex(size_t index) const;
    
    /// Get number of lanes
    size_t getLaneCount() const;
    
    /// Get all lane IDs in order
    std::vector<PlaylistLaneID> getLaneIDs() const;
    
    /// Move lane to new position
    bool moveLane(PlaylistLaneID laneId, size_t newIndex);
    
    // === Clip Operations ===
    
    /// Add a clip to a lane, returns the clip ID
    PlaylistClipID addClip(PlaylistLaneID laneId, const PlaylistClip& clip);
    
    /// Add a clip from a source at a specific position
    PlaylistClipID addClipFromSource(PlaylistLaneID laneId, ClipSourceID sourceId,
                                      SampleIndex startTime, SampleIndex length,
                                      SampleIndex sourceStart = 0);
    
    /// Remove a clip
    bool removeClip(PlaylistClipID clipId);
    
    /// Get clip by ID (searches all lanes)
    PlaylistClip* getClip(PlaylistClipID clipId);
    const PlaylistClip* getClip(PlaylistClipID clipId) const;
    
    /// Find which lane contains a clip
    PlaylistLaneID findClipLane(PlaylistClipID clipId) const;
    
    /// Move clip to new position (same or different lane)
    bool moveClip(PlaylistClipID clipId, PlaylistLaneID targetLaneId, SampleIndex newStartTime);
    
    /// Move clip within same lane
    bool moveClip(PlaylistClipID clipId, SampleIndex newStartTime);
    
    // === Trim Operations ===
    
    /// Trim clip start (positive = shrink, negative = expand)
    bool trimClipStart(PlaylistClipID clipId, SampleIndex deltaSamples);
    
    /// Trim clip end (positive = shrink, negative = expand)
    bool trimClipEnd(PlaylistClipID clipId, SampleIndex deltaSamples);
    
    /// Set clip length directly
    bool setClipLength(PlaylistClipID clipId, SampleIndex newLength);
    
    // === Split & Duplicate ===
    
    /// Split a clip at timeline position, returns new clip ID (second half)
    PlaylistClipID splitClip(PlaylistClipID clipId, SampleIndex splitTime);
    
    /// Duplicate a clip
    PlaylistClipID duplicateClip(PlaylistClipID clipId);
    
    // === Queries ===
    
    /// Get all clips overlapping a time range on a lane
    std::vector<const PlaylistClip*> getClipsInRange(PlaylistLaneID laneId,
                                                       SampleIndex rangeStart,
                                                       SampleIndex rangeEnd) const;
    
    /// Get all clips overlapping a time range on all lanes
    std::vector<std::pair<PlaylistLaneID, const PlaylistClip*>> 
        getAllClipsInRange(SampleIndex rangeStart, SampleIndex rangeEnd) const;
    
    /// Get the total timeline duration (end of last clip across all lanes)
    SampleIndex getTotalDuration() const;
    
    // === Snapping ===
    
    /// Snap a position to grid
    SampleIndex snapToGrid(SampleIndex position) const;
    
    // === Observer Pattern ===
    
    /// Register callback for model changes
    void addChangeObserver(ChangeCallback callback);
    
    /// Clear all observers
    void clearChangeObservers();
    
    // === Snapshot Generation ===
    
    /// Build a runtime snapshot for the audio thread
    std::unique_ptr<PlaylistRuntimeSnapshot> buildRuntimeSnapshot(
        const SourceManager& sourceManager) const;
    
    // === Serialization ===
    
    /// Clear all data
    void clear();
    
    /// Get modification counter (increments on each change)
    uint64_t getModificationCounter() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    // Internal helpers
    void notifyChange();
    int findLaneIndex(PlaylistLaneID laneId) const;
    std::pair<int, int> findClipLocation(PlaylistClipID clipId) const;
};

} // namespace Audio
} // namespace Nomad
