// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Where "Place in timeline" puts an Arsenal pattern (V81-AR1, FD-27: the mock's "Place in timeline").
 *
 * A button press has no pointer position, so the rule decides the start beat and the lane:
 *  - start: straight after the last copy of this pattern already in the timeline (so pressing again stacks
 *    Beat A, Beat A, Beat A); with no copy yet, after the end of the song so far, rounded up to a bar, so a
 *    new pattern follows the last idea placed (build, place, try a variation, place: the song grows to the
 *    right). When the timeline cursor stands later than that, its bar wins: the person chose the spot.
 *    Layering a pattern under another is a drag, not this button;
 *  - lane: the lane of that last copy when it is free for the whole length, otherwise the first free lane in
 *    order, otherwise a new lane (-1). A clip is never placed over another.
 * Pure, so it is tested without a project.
 */
struct PlacedClip {
    int lane{0};
    double startBeat{0.0};
    double endBeat{0.0};
    uint64_t patternId{0};
};

struct PlacementTarget {
    int lane{-1}; ///< -1 = make a new lane
    double startBeat{0.0};
};

inline PlacementTarget planPlacement(const std::vector<PlacedClip>& clips, int laneCount, uint64_t patternId,
                                     double patternBeats, double cursorBeat, double beatsPerBar) {
    const double length = std::max(patternBeats, 0.25);
    const double bar = std::max(beatsPerBar, 1.0);
    const PlacedClip* last = nullptr;
    for (const auto& c : clips)
        if (c.patternId == patternId && (!last || c.endBeat > last->endBeat)) last = &c;

    PlacementTarget out;
    if (last) {
        out.startBeat = last->endBeat;
    } else {
        double songEnd = 0.0;
        for (const auto& c : clips) songEnd = std::max(songEnd, c.endBeat);
        const double endBar = std::ceil(songEnd / bar - 1e-9) * bar;
        const double cursorBar = std::floor(std::max(0.0, cursorBeat) / bar) * bar;
        out.startBeat = std::max(endBar, cursorBar);
    }

    const auto freeOn = [&](int lane) {
        for (const auto& c : clips)
            if (c.lane == lane && c.startBeat < out.startBeat + length - 1e-9 && c.endBeat > out.startBeat + 1e-9) return false;
        return true;
    };
    if (last && last->lane >= 0 && last->lane < laneCount && freeOn(last->lane)) { out.lane = last->lane; return out; }
    for (int lane = 0; lane < laneCount; ++lane)
        if (freeOn(lane)) { out.lane = lane; return out; }
    out.lane = -1;
    return out;
}

} // namespace Audio
} // namespace Aestra
