// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/ClipInstance.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Crossfade resolution (V83-E10, FD-94).
 *
 * A crossfade is two linked fades plus a shape. It is stored once, on the LATER clip (ClipEdits::crossfadeFrom);
 * this turns that link into the fade lengths and shapes the renderer applies, every time the snapshot is built.
 * Because only the link is stored, the length is always the current overlap: move a clip and the crossfade follows.
 * A link that no longer means anything (the partner is gone, on another lane, or no longer overlaps) resolves to
 * no crossfade, so the clip plays with its own fades.
 */
struct ResolvedFades {
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
    FadeShape fadeInShape = FadeShape::Linear;
    FadeShape fadeOutShape = FadeShape::Linear;
};

/** @brief The overlap of two clips in beats, 0 when they only touch or do not meet. */
inline double crossfadeOverlapBeats(const ClipInstance& earlier, const ClipInstance& later) {
    if (!(earlier.startBeat < later.startBeat))
        return 0.0;
    const double overlap = std::min(earlier.endBeat(), later.endBeat()) - later.startBeat;
    return overlap > 0.0 && std::isfinite(overlap) ? overlap : 0.0;
}

/** @brief The clip's own fades, with any crossfade it takes part in laid over them. */
inline ResolvedFades resolveClipFades(const std::vector<ClipInstance>& laneClips, const ClipInstance& clip) {
    ResolvedFades out;
    out.fadeInBeats = std::isfinite(clip.edits.fadeInBeats) ? std::max(0.0, static_cast<double>(clip.edits.fadeInBeats)) : 0.0;
    out.fadeOutBeats = std::isfinite(clip.edits.fadeOutBeats) ? std::max(0.0, static_cast<double>(clip.edits.fadeOutBeats)) : 0.0;

    // As the later clip: fade in across the overlap with the clip it crossfades from.
    if (clip.edits.crossfadeFrom.isValid()) {
        for (const auto& other : laneClips) {
            if (other.id == clip.edits.crossfadeFrom) {
                const double overlap = crossfadeOverlapBeats(other, clip);
                if (overlap > 0.0) {
                    out.fadeInBeats = overlap;
                    out.fadeInShape = clip.edits.crossfadeShape;
                }
                break;
            }
        }
    }
    // As the earlier clip: fade out across the overlap with whichever clip crossfades from this one.
    for (const auto& other : laneClips) {
        if (other.edits.crossfadeFrom == clip.id && clip.id.isValid()) {
            const double overlap = crossfadeOverlapBeats(clip, other);
            if (overlap > 0.0) {
                out.fadeOutBeats = overlap;
                out.fadeOutShape = other.edits.crossfadeShape;
            }
            break;
        }
    }
    return out;
}

/** @brief True when this clip is the later half of a crossfade that currently overlaps. */
inline bool isCrossfaded(const std::vector<ClipInstance>& laneClips, const ClipInstance& later) {
    if (!later.edits.crossfadeFrom.isValid())
        return false;
    for (const auto& other : laneClips)
        if (other.id == later.edits.crossfadeFrom)
            return crossfadeOverlapBeats(other, later) > 0.0;
    return false;
}

} // namespace Audio
} // namespace Aestra
