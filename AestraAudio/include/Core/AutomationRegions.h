// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Core/AutomationCurve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @file AutomationRegions.h
 * @brief Automation clips (V8-A7): the operations, as pure functions on one curve.
 *
 * A region (see AutomationRegion) is a span of a curve that moves, copies, splits and
 * deletes as one object. The functions here only edit the curve; undo is the caller's
 * snapshot of the lane's curves (EditAutomationCurvesCommand), and the engine is not
 * involved because the points are what it plays.
 *
 * Rules, each pinned by AutomationRegionsTest:
 *  - A curve with points and no regions has ONE implicit region over all its points;
 *    the first operation turns it into a stored one (materialize()).
 *  - Moving or copying onto another region is refused. Loose points under the
 *    destination are replaced (the clip overwrites what it lands on).
 *  - A region never moves before beat 0; the move is refused rather than clamped, so
 *    the clip's shape is never squashed.
 *  - Splitting cuts the SEGMENT between two adjacent points. It inserts nothing and
 *    changes no value, so the curve sounds the same whatever its tension.
 */
namespace AutomationRegions {

constexpr double kEps = 1e-9;

inline bool inside(const AutomationRegion& r, double beat) {
    return beat >= r.startBeat - kEps && beat <= r.endBeat + kEps;
}

inline const AutomationRegion* find(const AutomationCurve& curve, uint64_t id) {
    for (const auto& r : curve.regions)
        if (r.id == id)
            return &r;
    return nullptr;
}

/// The region whose span holds @p beat, if any.
inline const AutomationRegion* regionAt(const AutomationCurve& curve, double beat) {
    for (const auto& r : curve.regions)
        if (inside(r, beat))
            return &r;
    return nullptr;
}

inline uint64_t nextId(const AutomationCurve& curve) {
    uint64_t id = 0;
    for (const auto& r : curve.regions)
        id = std::max(id, r.id);
    return id + 1;
}

/// Stores the implicit region (all points) if the curve has points and no regions. True if it did.
inline bool materialize(AutomationCurve& curve) {
    if (!curve.regions.empty() || curve.points.empty())
        return false;
    curve.regions.push_back({1, curve.points.front().beat, curve.points.back().beat});
    return true;
}

inline bool overlapsAnother(const AutomationCurve& curve, uint64_t exceptId, double start, double end) {
    for (const auto& r : curve.regions)
        if (r.id != exceptId && start <= r.endBeat + kEps && r.startBeat <= end + kEps)
            return true;
    return false;
}

inline void sortRegions(AutomationCurve& curve) {
    std::sort(curve.regions.begin(), curve.regions.end(),
              [](const AutomationRegion& a, const AutomationRegion& b) { return a.startBeat < b.startBeat; });
}

/// Removes every point inside [start, end]. Callers have already checked the span holds no other region.
inline void clearSpan(AutomationCurve& curve, double start, double end) {
    auto& pts = curve.points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
                             [&](const AutomationPoint& p) { return p.beat >= start - kEps && p.beat <= end + kEps; }),
              pts.end());
}

/// Move region @p id by @p deltaBeats. False (nothing changed) if it would start before 0 or land on another region.
inline bool move(AutomationCurve& curve, uint64_t id, double deltaBeats) {
    materialize(curve);
    auto it = std::find_if(curve.regions.begin(), curve.regions.end(), [id](const auto& r) { return r.id == id; });
    if (it == curve.regions.end())
        return false;
    if (std::abs(deltaBeats) < kEps)
        return true;
    const AutomationRegion src = *it;
    const double start = src.startBeat + deltaBeats;
    const double end = src.endBeat + deltaBeats;
    if (start < -kEps || overlapsAnother(curve, id, start, end))
        return false;

    std::vector<AutomationPoint> carried;
    for (const auto& p : curve.points)
        if (inside(src, p.beat))
            carried.push_back(p);
    auto& pts = curve.points;
    pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const AutomationPoint& p) { return inside(src, p.beat); }),
              pts.end());
    clearSpan(curve, start, end);
    for (auto p : carried) {
        p.beat += deltaBeats;
        p.sample = 0; // legacy tempo cache, never read
        pts.push_back(p);
    }
    curve.sortPoints();
    it = std::find_if(curve.regions.begin(), curve.regions.end(), [id](const auto& r) { return r.id == id; });
    it->startBeat = start;
    it->endBeat = end;
    sortRegions(curve);
    return true;
}

/// Copy region @p id to @p deltaBeats away. Returns the new region's id, or nothing if refused.
inline std::optional<uint64_t> copy(AutomationCurve& curve, uint64_t id, double deltaBeats) {
    materialize(curve);
    const AutomationRegion* found = find(curve, id);
    if (!found)
        return std::nullopt;
    const AutomationRegion src = *found;
    const double start = src.startBeat + deltaBeats;
    const double end = src.endBeat + deltaBeats;
    if (start < -kEps || overlapsAnother(curve, 0, start, end))
        return std::nullopt;

    std::vector<AutomationPoint> carried;
    for (const auto& p : curve.points)
        if (inside(src, p.beat)) {
            auto c = p;
            c.beat += deltaBeats;
            c.sample = 0;
            c.selected = false;
            carried.push_back(c);
        }
    clearSpan(curve, start, end);
    for (const auto& p : carried)
        curve.points.push_back(p);
    curve.sortPoints();
    const uint64_t newId = nextId(curve);
    curve.regions.push_back({newId, start, end});
    sortRegions(curve);
    return newId;
}

/// Create a new clip over [start, end] holding two points at @p value (a flat line). Returns the new region's id, or
/// nothing if refused: it would start before 0, be empty, or land on another clip. Loose points under the span are
/// replaced (the clip overwrites what it lands on); the points of other clips are never touched.
inline std::optional<uint64_t> create(AutomationCurve& curve, double start, double end, float value) {
    if (start < -kEps || end <= start + kEps)
        return std::nullopt;
    materialize(curve); // an implicit clip over all the points becomes a stored one, so the overlap test sees it
    if (overlapsAnother(curve, 0, start, end))
        return std::nullopt;
    clearSpan(curve, start, end);
    for (const double beat : {start, end}) {
        AutomationPoint p;
        p.beat = beat;
        p.value = value;
        curve.points.push_back(p);
    }
    curve.sortPoints();
    const uint64_t newId = nextId(curve);
    curve.regions.push_back({newId, start, end});
    sortRegions(curve);
    return newId;
}

/// Cut region @p id in two at the segment holding @p beat. Returns the new (right-hand) id, or nothing.
inline std::optional<uint64_t> split(AutomationCurve& curve, uint64_t id, double beat) {
    materialize(curve);
    auto it = std::find_if(curve.regions.begin(), curve.regions.end(), [id](const auto& r) { return r.id == id; });
    if (it == curve.regions.end() || !(beat > it->startBeat + kEps) || !(beat <= it->endBeat + kEps))
        return std::nullopt;
    // The last point strictly before the beat ends the left part; the first at or after it starts the right.
    const AutomationPoint* left = nullptr;
    const AutomationPoint* right = nullptr;
    for (const auto& p : curve.points) {
        if (!inside(*it, p.beat))
            continue;
        if (p.beat < beat - kEps)
            left = &p;
        else if (!right)
            right = &p;
    }
    if (!left || !right)
        return std::nullopt;
    const double leftEnd = left->beat;
    const double rightStart = right->beat;
    const uint64_t newId = nextId(curve);
    const double oldEnd = it->endBeat;
    it->endBeat = leftEnd;
    curve.regions.push_back({newId, rightStart, oldEnd});
    sortRegions(curve);
    return newId;
}

/// Delete region @p id and the points inside it.
inline bool remove(AutomationCurve& curve, uint64_t id) {
    materialize(curve);
    auto it = std::find_if(curve.regions.begin(), curve.regions.end(), [id](const auto& r) { return r.id == id; });
    if (it == curve.regions.end())
        return false;
    clearSpan(curve, it->startBeat, it->endBeat);
    curve.regions.erase(it);
    return true;
}

/// After point edits (drag, add, delete): re-tighten every region to its points and drop empty ones.
inline void retighten(AutomationCurve& curve) {
    for (auto& r : curve.regions) {
        double lo = 0.0, hi = 0.0;
        bool any = false;
        for (const auto& p : curve.points)
            if (inside(r, p.beat)) {
                lo = any ? std::min(lo, p.beat) : p.beat;
                hi = any ? std::max(hi, p.beat) : p.beat;
                any = true;
            }
        r.startBeat = lo;
        r.endBeat = any ? hi : -1.0;
    }
    curve.regions.erase(std::remove_if(curve.regions.begin(), curve.regions.end(),
                                       [](const AutomationRegion& r) { return r.endBeat < r.startBeat; }),
                        curve.regions.end());
}

} // namespace AutomationRegions
} // namespace Audio
} // namespace Aestra
