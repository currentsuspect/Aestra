// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <vector>

namespace Aestra::Audio {

enum class TrackSelectionIntent {
    Replace,
    Add,
    Toggle,
};

constexpr TrackSelectionIntent trackSelectionIntentForModifierState(bool toggleModifier, bool shiftModifier) {
    if (toggleModifier) {
        return TrackSelectionIntent::Toggle;
    }
    if (shiftModifier) {
        return TrackSelectionIntent::Add;
    }
    return TrackSelectionIntent::Replace;
}

/** Stable, widget-independent selection authority for Playlist lanes. */
class TimelineTrackSelection {
public:
    void apply(const PlaylistLaneID& laneId, TrackSelectionIntent intent) {
        if (!laneId.isValid()) {
            return;
        }

        if (intent == TrackSelectionIntent::Replace) {
            m_laneIds.clear();
        } else if (intent == TrackSelectionIntent::Toggle) {
            if (m_laneIds.erase(laneId) != 0) {
                return;
            }
        }
        m_laneIds.insert(laneId);
    }

    void clear() { m_laneIds.clear(); }
    bool contains(const PlaylistLaneID& laneId) const { return m_laneIds.find(laneId) != m_laneIds.end(); }
    size_t size() const { return m_laneIds.size(); }
    bool empty() const { return m_laneIds.empty(); }

    void selectAll(const std::vector<PlaylistLaneID>& laneIds) {
        m_laneIds.clear();
        for (const auto& laneId : laneIds) {
            if (laneId.isValid()) {
                m_laneIds.insert(laneId);
            }
        }
    }

    void retainOnly(const std::vector<PlaylistLaneID>& validLaneIds) {
        const std::unordered_set<PlaylistLaneID> valid(validLaneIds.begin(), validLaneIds.end());
        for (auto it = m_laneIds.begin(); it != m_laneIds.end();) {
            it = valid.find(*it) == valid.end() ? m_laneIds.erase(it) : std::next(it);
        }
    }

private:
    std::unordered_set<PlaylistLaneID> m_laneIds;
};

/**
 * @brief Multi-clip selection authority for the timeline (#848, marquee select).
 *
 * Intent-based like TimelineTrackSelection: Replace clears the set first,
 * Toggle flips membership, Add keeps existing entries. The anchor clip
 * (last non-toggle selection) stays available for single-clip operations.
 */
class TimelineClipSelection {
public:
    void apply(const ClipInstanceID& clipId, TrackSelectionIntent intent) {
        if (!clipId.isValid()) {
            return;
        }

        if (intent == TrackSelectionIntent::Replace) {
            m_clipIds.clear();
            m_anchorClipId = clipId;
        } else if (intent == TrackSelectionIntent::Toggle) {
            if (m_clipIds.erase(clipId) != 0) {
                if (m_anchorClipId == clipId && !m_clipIds.empty()) {
                    m_anchorClipId = *m_clipIds.begin();
                }
                return;
            }
            m_anchorClipId = clipId;
        } else {
            m_anchorClipId = clipId; // Add updates the anchor too.
        }
        m_clipIds.insert(clipId);
    }

    void clear() {
        m_clipIds.clear();
        m_anchorClipId = ClipInstanceID{};
    }

    bool contains(const ClipInstanceID& clipId) const { return m_clipIds.find(clipId) != m_clipIds.end(); }
    size_t size() const { return m_clipIds.size(); }
    bool empty() const { return m_clipIds.empty(); }
    const ClipInstanceID& anchor() const { return m_anchorClipId; }
    const std::unordered_set<ClipInstanceID>& clips() const { return m_clipIds; }

    void selectAll(const std::vector<ClipInstanceID>& clipIds) {
        m_clipIds.clear();
        for (const auto& clipId : clipIds) {
            if (clipId.isValid()) {
                m_clipIds.insert(clipId);
            }
        }
        m_anchorClipId = m_clipIds.empty() ? ClipInstanceID{} : *m_clipIds.begin();
    }

private:
    std::unordered_set<ClipInstanceID> m_clipIds;
    ClipInstanceID m_anchorClipId;
};

enum class TimelineLoopPreset : int {
    Off = 0,
    OneBar = 1,
    TwoBars = 2,
    FourBars = 3,
    EightBars = 4,
    Selection = 5,
    Project = 6,
};

constexpr int timelineLoopPresetId(TimelineLoopPreset preset) {
    return static_cast<int>(preset);
}

constexpr TimelineLoopPreset timelineLoopPresetFromId(int preset) {
    return preset >= timelineLoopPresetId(TimelineLoopPreset::Off) &&
                   preset <= timelineLoopPresetId(TimelineLoopPreset::Project)
               ? static_cast<TimelineLoopPreset>(preset)
               : TimelineLoopPreset::Off;
}

constexpr int kDefaultEmptyProjectLoopBars = 16;

inline double resolveProjectLoopEndBeat(double arrangementEndBeat, int beatsPerBar) {
    if (arrangementEndBeat > 0.001) {
        return arrangementEndBeat;
    }
    return static_cast<double>(std::max(1, beatsPerBar) * kDefaultEmptyProjectLoopBars);
}

} // namespace Aestra::Audio
