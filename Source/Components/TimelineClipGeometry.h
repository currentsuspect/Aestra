// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The pure rules behind the timeline clip look (FD-50, boards 7 and 8): how tall the header band is, when a
// clip has room for one, and where a pattern's notes and end fall inside a clip. Header-only and free of the
// renderer, so TimelineClipGeometryTest runs in the headless build. TimelineClipPaint draws with these.
#pragma once

#include <algorithm>
#include <cmath>

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

/// Beat, from the clip's left edge, where the pattern's material runs out, or -1 when the clip ends first (or
/// exactly at the pattern's end). Past this point the clip is silent: a clip does not loop its pattern.
inline double patternEndInClip(double patternLengthBeats, double sourceOffsetBeats, double clipDurationBeats) {
    constexpr double kEpsilon = 1.0e-6;
    const double end = patternLengthBeats - sourceOffsetBeats;
    if (!(patternLengthBeats > 0.0) || end <= kEpsilon || end >= clipDurationBeats - kEpsilon) return -1.0;
    return end;
}

} // namespace ClipPaint
} // namespace Audio
} // namespace Aestra
