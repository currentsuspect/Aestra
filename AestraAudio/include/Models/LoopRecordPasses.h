// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace Aestra {
namespace Audio {

/** One pass of a loop recording: a span of the captured frames and where it is placed on the timeline. */
struct LoopRecordPass {
    size_t offsetFrames = 0;
    size_t lengthFrames = 0;
    double startBeat = 0.0;
};

/**
 * @brief Cut a loop recording into one take per pass of the loop (Loop record).
 *
 * Capture runs on across loop wraps, so the frames are one continuous performance. The first pass runs from the
 * capture start to the loop end; each later pass is one loop length and is placed at the loop start. Boundaries are
 * moved later by @p latencyBeats, because the captured audio for a backing beat arrives that much later (the same
 * sum TrackManager shifts placement by). A tail shorter than @p minTailFrames is dropped (a stop just after a wrap).
 * Returns a single pass covering everything when the capture never crosses the loop end, or the loop does not apply.
 */
inline std::vector<LoopRecordPass> loopRecordPasses(size_t totalFrames, double placedStartBeat, double captureStartBeat,
                                                    double latencyBeats, double loopStart, double loopEnd,
                                                    double framesPerBeat, size_t minTailFrames) {
    std::vector<LoopRecordPass> passes;
    const LoopRecordPass whole{0, totalFrames, placedStartBeat};
    if (totalFrames == 0 || framesPerBeat <= 0.0 || !(loopEnd > loopStart) || captureStartBeat >= loopEnd ||
        captureStartBeat < loopStart - 1e-6) {
        passes.push_back(whole);
        return passes;
    }
    const double firstBeats = loopEnd - captureStartBeat + std::max(0.0, latencyBeats);
    const auto firstFrames = static_cast<size_t>(std::llround(firstBeats * framesPerBeat));
    const auto loopFrames = static_cast<size_t>(std::llround((loopEnd - loopStart) * framesPerBeat));
    if (firstFrames >= totalFrames || loopFrames == 0) {
        passes.push_back(whole);
        return passes;
    }
    passes.push_back({0, firstFrames, placedStartBeat});
    for (size_t off = firstFrames; off < totalFrames; off += loopFrames) {
        const size_t len = std::min(loopFrames, totalFrames - off);
        if (len < loopFrames && len < minTailFrames)
            break;
        passes.push_back({off, len, loopStart});
    }
    return passes;
}

} // namespace Audio
} // namespace Aestra
