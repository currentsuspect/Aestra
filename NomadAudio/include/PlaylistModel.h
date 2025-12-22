// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"
#include "PatternSource.h"
#include "TimeTypes.h"
#include "AutomationCurve.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace Nomad {
namespace Audio {

// Forward declarations
class PlaylistRuntimeSnapshot;
class SourceManager;
class PatternManager;

// =============================================================================
// PlaylistLaneID - Unique track/lane identity
// =============================================================================

/**
 * @brief Unique identifier for a playlist lane (v3.0)
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
    
    static PlaylistLaneID fromString(const std::string& str) {
        PlaylistLaneID uuid;
        if (str.length() >= 36) {
            unsigned int a, b, c, d;
            unsigned long long e;
            if (sscanf(str.c_str(), "%8x-%4x-%4x-%4x-%12llx", &a, &b, &c, &d, &e) == 5) {
                uuid.high = (static_cast<uint64_t>(a) << 32) | 
                           (static_cast<uint64_t>(b) << 16) | 
                           static_cast<uint64_t>(c);
                uuid.low = (static_cast<uint64_t>(d) << 48) | e;
            }
        }
        return uuid;
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
};

// =============================================================================
// PlaylistLane - A horizontal lane that contains clips
// =============================================================================

/**
 * @brief A single lane in the playlist view (v3.0)
 * 
 * Represents a horizontal arrange lane.
 * Invariant: clips vector is ALWAYS sorted by startBeat.
 */
struct PlaylistLane {
    PlaylistLaneID id;
    std::string name;
    
    // Clips (always sorted by startBeat)
    std::vector<ClipInstance> clips;
    
    // Lane properties
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    
    // Automation Curves
    std::vector<AutomationCurve> automationCurves;
    
    // UI properties
    uint32_t colorRGBA = 0xFF4A90D9;
    float height = 100.0f;
    bool collapsed = false;
    
    // Constructors
    PlaylistLane() : id(PlaylistLaneID::generate()) {}
    explicit PlaylistLane(const std::string& laneName) 
        : id(PlaylistLaneID::generate()), name(laneName) {}
    
    // === Clip Sorting ===
    void sortClips() {
        std::sort(clips.begin(), clips.end(), [](const ClipInstance& a, const ClipInstance& b) {
            return a.startBeat < b.startBeat;
        });
    }
    
    // === Queries ===
    int findClipIndex(const ClipInstanceID& clipId) const {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].id == clipId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    
    ClipInstance* getClip(const ClipInstanceID& clipId) {
        int idx = findClipIndex(clipId);
        return idx >= 0 ? &clips[idx] : nullptr;
    }
    
    const ClipInstance* getClip(const ClipInstanceID& clipId) const {
        int idx = findClipIndex(clipId);
        return idx >= 0 ? &clips[idx] : nullptr;
    }
};

// =============================================================================
// PlaylistModel - Central playlist controller
// =============================================================================

class PlaylistModel {
public:
    using ChangeCallback = std::function<void()>;
    
    PlaylistModel();
    ~PlaylistModel();
    
    PlaylistModel(const PlaylistModel&) = delete;
    PlaylistModel& operator=(const PlaylistModel&) = delete;
    
    // === Project Settings ===
    void setProjectSampleRate(double sampleRate);
    double getProjectSampleRate() const;
    void setBPM(double bpm);
    double getBPM() const;
    
    // === Temporal Conversion ===
    double getBPMAtBeat(double beat) const;
    double beatToSeconds(double beat) const;
    double secondsToBeats(double seconds) const;
    
    // === Lane Management ===
    PlaylistLaneID createLane(const std::string& name = "");
    bool deleteLane(PlaylistLaneID laneId);
    PlaylistLane* getLane(PlaylistLaneID laneId);
    const PlaylistLane* getLane(PlaylistLaneID laneId) const;
    size_t getLaneCount() const;
    std::vector<PlaylistLaneID> getLaneIDs() const;
    PlaylistLaneID getLaneId(size_t index) const;

    bool moveLane(PlaylistLaneID laneId, size_t newIndex);
    
    // === Clip Operations ===
    ClipInstanceID addClip(PlaylistLaneID laneId, const ClipInstance& clip);
    
    /// Add a clip referencing a pattern
    ClipInstanceID addClipFromPattern(PlaylistLaneID laneId, PatternID patternId,
                                       double startBeat, double durationBeats);
    
    bool removeClip(ClipInstanceID clipId);
    ClipInstance* getClip(ClipInstanceID clipId);
    const ClipInstance* getClip(ClipInstanceID clipId) const;
    PlaylistLaneID findClipLane(ClipInstanceID clipId) const;
    
    // === Transformations ===
    bool moveClip(ClipInstanceID clipId, PlaylistLaneID targetLaneId, double newStartBeat);
    bool setClipDuration(ClipInstanceID clipId, double newDurationBeats);
    
    // === Split & Duplicate ===
    ClipInstanceID splitClip(ClipInstanceID clipId, double splitBeat);
    ClipInstanceID duplicateClip(ClipInstanceID clipId);
    
    // === Queries ===
    std::vector<const ClipInstance*> getClipsInRange(PlaylistLaneID laneId,
                                                      double startBeat,
                                                      double endBeat) const;
    
    double getTotalDurationBeats() const;
    
    // === Observer Pattern ===
    void addChangeObserver(ChangeCallback callback);
    void clearChangeObservers();
    
    // === Snapshot Generation ===
    std::unique_ptr<PlaylistRuntimeSnapshot> buildRuntimeSnapshot(
        const PatternManager& patternManager,
        const SourceManager& sourceManager) const;
    
    void clear();
    uint64_t getModificationCounter() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    void notifyChange();
    int findLaneIndex(PlaylistLaneID laneId) const;
    std::pair<int, int> findClipLocation(ClipInstanceID clipId) const;
};

} // namespace Audio
} // namespace Nomad
