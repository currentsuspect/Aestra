// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// How a timeline clip repeats its pattern (FD-50, V82-T3). A clip reads source beats [sourceStart, sourceEnd) of
// an endless repetition of its pattern: source beat s plays pattern beat s mod length. A clip no longer than its
// pattern plays exactly what it did before, so only clips running past their pattern change.
//
// One definition for the scheduler (PatternPlaybackEngine), the clip's mini piano roll and dashed repeat lines
// (TrackUIComponent / TimelineClipPaint) and the right-edge drag, so what is drawn, dragged and heard agree.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Aestra {
namespace Audio {
namespace PatternRepeat {

/// Inclusive range of repeat indices; empty when last < first.
struct Range {
    int64_t first = 0;
    int64_t last = -1;
    bool empty() const { return last < first; }
};

/// A repeat needs a real pattern length and a clip that ends: an endless instance (Arsenal preview) loops by
/// other means and plays its pattern once per pass.
inline bool repeats(double patternLengthBeats, double sourceEndBeat) {
    return patternLengthBeats > 0.0 && std::isfinite(patternLengthBeats) && std::isfinite(sourceEndBeat);
}

/// Every repeat a clip reading source beats [sourceStart, sourceEnd) touches.
inline Range inSource(double patternLengthBeats, double sourceStartBeat, double sourceEndBeat) {
    if (!repeats(patternLengthBeats, sourceEndBeat) || !(sourceEndBeat > sourceStartBeat)) return {0, 0};
    constexpr double kEpsilon = 1.0e-9;
    const auto first = static_cast<int64_t>(std::floor(std::max(0.0, sourceStartBeat) / patternLengthBeats));
    const auto last = static_cast<int64_t>(std::ceil(sourceEndBeat / patternLengthBeats - kEpsilon)) - 1;
    return {first, std::max(first, last)};
}

/// The repeats that can sound between absolute beats [fromBeat, toBeat): repeat r covers
/// [patternStartBeat + r * length, + reachBeats), where reachBeats is how far past its own start a repeat's
/// last note can ring. Bounds the scheduler's work to the lookahead window however long the clip is.
inline Range inWindow(double patternLengthBeats, double sourceStartBeat, double sourceEndBeat,
                      double patternStartBeat, double fromBeat, double toBeat, double reachBeats) {
    Range r = inSource(patternLengthBeats, sourceStartBeat, sourceEndBeat);
    if (!repeats(patternLengthBeats, sourceEndBeat)) return r;
    const double lo = std::floor((fromBeat - patternStartBeat - reachBeats) / patternLengthBeats);
    const double hi = std::floor((toBeat - patternStartBeat) / patternLengthBeats);
    if (lo > static_cast<double>(r.first)) r.first = static_cast<int64_t>(lo);
    if (hi < static_cast<double>(r.last)) r.last = static_cast<int64_t>(hi);
    return r;
}

/// Seed for a repeat's chance rolls: repeat 0 keeps the pass seed it always had, later repeats roll anew.
inline uint64_t passSeed(uint64_t pass, int64_t repeat) {
    constexpr uint64_t kGoldenRatio64 = 0x9E3779B97F4A7C15ULL; // spreads consecutive repeats across the hash
    return pass + (static_cast<uint64_t>(repeat) * kGoldenRatio64);
}

/// Where a right-edge drag that would end the clip at @p proposedEndBeat lands when it snaps to whole repeats.
/// Before the first repeat boundary past the clip's start (pattern start + k * length) the drag is a plain trim
/// and @p proposedEndBeat comes back unchanged, for the caller's grid snap; from that boundary on it lands on the
/// boundary nearest the pointer. Also unchanged when the pattern has no length.
inline double snapEndToRepeat(double patternLengthBeats, double patternStartBeat, double clipStartBeat,
                              double proposedEndBeat) {
    if (!(patternLengthBeats > 0.0) || !std::isfinite(patternLengthBeats)) return proposedEndBeat;
    constexpr double kEpsilon = 1.0e-6;
    const double firstK = std::floor((clipStartBeat - patternStartBeat) / patternLengthBeats + kEpsilon) + 1.0;
    const double firstBoundary = patternStartBeat + firstK * patternLengthBeats;
    if (proposedEndBeat < firstBoundary - kEpsilon) return proposedEndBeat;
    const double k = std::max(firstK, std::round((proposedEndBeat - patternStartBeat) / patternLengthBeats));
    return patternStartBeat + k * patternLengthBeats;
}

} // namespace PatternRepeat
} // namespace Audio
} // namespace Aestra
