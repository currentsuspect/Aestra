// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Comping (SPEC-022): which take plays where. A comp is an ordered set of non-overlapping beat ranges, each naming the
// take lane whose audio plays across it. Pure data and arithmetic: no engine, no UI, no allocation on the audio thread
// (it is never read there; the engine plays the clips this turns into, see compClipsFor).

#include "ClipInstance.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Aestra {
namespace Audio {

/// Beats closer than this are the same beat.
inline constexpr double kCompEpsilon = 1e-6;

struct CompRange {
    double startBeat = 0.0;
    double endBeat = 0.0;
    PlaylistLaneID laneId;

    double length() const { return endBeat - startBeat; }
};

class CompRanges {
public:
    const std::vector<CompRange>& ranges() const { return m_ranges; }
    bool empty() const { return m_ranges.empty(); }
    void clear() { m_ranges.clear(); }

    /// Make [startBeat, endBeat) play `laneId`. Whatever it covered before is cut back or replaced; ranges that end up
    /// touching on the same lane join. A span of no length changes nothing.
    void set(double startBeat, double endBeat, PlaylistLaneID laneId) {
        if (endBeat - startBeat <= kCompEpsilon) return;
        std::vector<CompRange> next;
        next.reserve(m_ranges.size() + 2);
        for (const CompRange& r : m_ranges) {
            if (r.endBeat <= startBeat + kCompEpsilon || r.startBeat >= endBeat - kCompEpsilon) {
                next.push_back(r); // untouched
                continue;
            }
            if (r.startBeat < startBeat - kCompEpsilon) next.push_back({r.startBeat, startBeat, r.laneId}); // left remainder
            if (r.endBeat > endBeat + kCompEpsilon) next.push_back({endBeat, r.endBeat, r.laneId});        // right remainder
        }
        next.push_back({startBeat, endBeat, laneId});
        std::sort(next.begin(), next.end(), [](const CompRange& a, const CompRange& b) { return a.startBeat < b.startBeat; });
        m_ranges.clear();
        for (const CompRange& r : next) {
            if (!m_ranges.empty() && m_ranges.back().laneId == r.laneId && r.startBeat - m_ranges.back().endBeat <= kCompEpsilon) {
                m_ranges.back().endBeat = r.endBeat;
            } else {
                m_ranges.push_back(r);
            }
        }
    }

    /// Take the lane out of the comp (it was deleted): its ranges become gaps.
    void removeLane(PlaylistLaneID laneId) {
        m_ranges.erase(std::remove_if(m_ranges.begin(), m_ranges.end(), [&](const CompRange& r) { return r.laneId == laneId; }),
                       m_ranges.end());
    }

    /// The lane that plays at `beat`, or an invalid id in a gap.
    PlaylistLaneID laneAt(double beat) const {
        for (const CompRange& r : m_ranges) {
            if (beat >= r.startBeat - kCompEpsilon && beat < r.endBeat - kCompEpsilon) return r.laneId;
        }
        return PlaylistLaneID{};
    }

    bool operator==(const CompRanges& o) const {
        if (m_ranges.size() != o.m_ranges.size()) return false;
        for (size_t i = 0; i < m_ranges.size(); ++i) {
            if (m_ranges[i].laneId != o.m_ranges[i].laneId || std::abs(m_ranges[i].startBeat - o.m_ranges[i].startBeat) > kCompEpsilon ||
                std::abs(m_ranges[i].endBeat - o.m_ranges[i].endBeat) > kCompEpsilon)
                return false;
        }
        return true;
    }

private:
    std::vector<CompRange> m_ranges;
};

/// One take lane's clips, for compClipsFor.
struct CompSourceLane {
    PlaylistLaneID laneId;
    const std::vector<ClipInstance>* clips = nullptr;
};

/// The clips the comp plays: for every range, the part of the chosen lane's clips that falls inside it, as new clips
/// (fresh ids) that read the same audio at the right offset. Playback needs nothing new: these sit on one lane like any
/// other clips. `bpm` converts the beat cut into the seconds the audio offset is kept in.
inline std::vector<ClipInstance> compClipsFor(const CompRanges& comp, const std::vector<CompSourceLane>& lanes, double bpm) {
    std::vector<ClipInstance> out;
    const double secondsPerBeat = bpm > 0.0 ? 60.0 / bpm : 0.0;
    for (const CompRange& range : comp.ranges()) {
        const auto lane = std::find_if(lanes.begin(), lanes.end(), [&](const CompSourceLane& l) { return l.laneId == range.laneId; });
        if (lane == lanes.end() || !lane->clips) continue;
        for (const ClipInstance& clip : *lane->clips) {
            const double start = std::max(range.startBeat, clip.startBeat);
            const double end = std::min(range.endBeat, clip.endBeat());
            if (end - start <= kCompEpsilon) continue;
            ClipInstance piece = clip;
            piece.id = ClipInstanceID::generate();
            const double cut = start - clip.startBeat;
            piece.startBeat = start;
            piece.durationBeats = end - start;
            piece.sourceOffset = clip.sourceOffset + cut;
            piece.sourceOffsetSeconds = clip.sourceOffsetSeconds + cut * secondsPerBeat;
            piece.durationSeconds = piece.durationBeats * secondsPerBeat;
            out.push_back(std::move(piece));
        }
    }
    return out;
}

} // namespace Audio
} // namespace Aestra
