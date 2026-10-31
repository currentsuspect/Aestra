// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The pure rules behind the timeline clip look (FD-50, boards 7 and 8): how tall the header band is, when a
// clip has room for one, and where a pattern's notes and repeats fall inside a clip. Header-only and free of the
// renderer, so TimelineClipGeometryTest runs in the headless build. TimelineClipPaint draws with these.
#pragma once

#include "Playback/PatternRepeat.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Aestra {
namespace Audio {
namespace ClipPaint {

/// Header band height for a clip of the given height: about a quarter of a 46 px row, 11 to 14 px.
inline float headerBandHeight(float clipHeight) {
    return std::clamp(std::round(clipHeight * 0.26f), 11.0f, 14.0f);
}

/// A clip shows a header band (and a name) only when it is big enough to hold one above some content.
inline bool showsHeaderBand(float clipWidth, float clipHeight) {
    return clipHeight > headerBandHeight(clipHeight) + 6.0f && clipWidth > 12.0f;
}

/// Beat, measured from the clip's left edge, at which a pattern note sounds. The scheduler plays a note at
/// clip start - source offset + note start (PlaylistClip::patternStartBeat) and only inside the clip, so the
/// mini piano roll maps notes the same way instead of stretching the pattern to the clip's width.
inline double noteBeatInClip(double noteStartBeat, double sourceOffsetBeats) {
    return noteStartBeat - sourceOffsetBeats;
}

/// Beats, from the clip's left edge, where the clip starts its pattern again (board 8's dashed lines): every
/// k * pattern length - source offset strictly inside the clip, limited to [fromBeat, toBeat) so a long clip of
/// a short pattern only yields the lines on screen. A clip no longer than its pattern has none. The scheduler
/// repeats the pattern at the same beats (PatternRepeat.h).
inline std::vector<double> repeatLinesInClip(double patternLengthBeats, double sourceOffsetBeats,
                                             double clipDurationBeats, double fromBeat = 0.0,
                                             double toBeat = std::numeric_limits<double>::infinity()) {
    std::vector<double> lines;
    if (!(patternLengthBeats > 0.0) || !std::isfinite(patternLengthBeats) || !(clipDurationBeats > 0.0)) return lines;
    constexpr double kEpsilon = 1.0e-6;
    const double end = std::min(clipDurationBeats - kEpsilon, toBeat);
    const double firstK = std::max(std::floor(sourceOffsetBeats / patternLengthBeats) + 1.0,
                                   std::floor((fromBeat + sourceOffsetBeats) / patternLengthBeats));
    for (double k = firstK;; k += 1.0) {
        const double x = (k * patternLengthBeats) - sourceOffsetBeats;
        if (x >= end) break;
        if (x > kEpsilon && x >= fromBeat) lines.push_back(x);
    }
    return lines;
}

} // namespace ClipPaint
} // namespace Audio
} // namespace Aestra
