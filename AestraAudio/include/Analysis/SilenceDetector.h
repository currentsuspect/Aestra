// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

struct AudioBufferData;

/**
 * @brief How Delete Silence decides what is dead air.
 *
 * Founder request (2026-11-07): "In Premiere Pro, you can tap the dead air and
 * delete it... we need something of the same nature in Aestra." The defaults
 * are the ones that cut 27.7 s of pauses out of a five-minute voice-over in 40
 * places without clipping a word (the ep01 tightening pass).
 */
struct SilenceParams {
    /// Estimate the threshold from the clip's own noise floor instead of using thresholdDb.
    bool autoThreshold = true;
    /// Frame RMS at or below this counts as silent, dBFS. Used when autoThreshold is off.
    double thresholdDb = -50.0;
    /// A quiet stretch must last at least this long to be cut at all, seconds.
    double minSilenceSec = 0.4;
    /// How much of each pause survives, seconds. Split evenly either side of the cut.
    double keepPauseSec = 0.25;
    /// Fade length at each new edge the cut leaves behind, seconds.
    double fadeSec = 0.01;
};

/** A stretch to remove, in frames from the start of the analysed region. */
struct SilenceRange {
    uint64_t startFrame = 0;
    uint64_t endFrame = 0; ///< Exclusive.
    uint64_t length() const { return endFrame - startFrame; }
};

/**
 * @brief Finds measured silence in audio and returns the ranges to delete.
 *
 * Pure analysis, no project state: a buffer, a frame window and parameters in;
 * ranges out. Two safety rules, both from the prototype that proved the idea on
 * real speech:
 *  1. Only MEASURED silence is cut. Energy is read per 10 ms frame (the loudest
 *     channel wins), and only runs of quiet frames count; nothing is inferred.
 *  2. Each cut comes from the MIDDLE of its pause, so half of the kept pause
 *     stays on each side of the cut and no word edge is ever touched.
 * A pause at the very start or end of the window counts too; there the kept
 * half sits next to the audio and the rest is removed from the window's edge.
 */
class SilenceDetector {
public:
    /// Analysis frame length.
    static constexpr double kFrameSec = 0.010;
    /// Auto threshold = this percentile of the frame levels (the noise floor) ...
    static constexpr double kFloorPercentile = 0.10;
    /// ... plus this margin, dB, clamped to [kAutoMinDb, kAutoMaxDb].
    static constexpr double kAutoMarginDb = 8.0;
    static constexpr double kAutoMinDb = -75.0;
    static constexpr double kAutoMaxDb = -35.0;
    /// Auto mode refuses to cut when loud audio is less than this far above the floor (nothing to tell apart).
    static constexpr double kAutoMinRangeDb = 15.0;

    /**
     * @brief The ranges to remove from [startFrame, startFrame + frameCount), ascending, non-overlapping.
     * @param usedThresholdDb If non-null, receives the threshold actually applied.
     */
    static std::vector<SilenceRange> findRemovableRanges(const AudioBufferData& buffer, uint64_t startFrame,
                                                         uint64_t frameCount, const SilenceParams& params,
                                                         double* usedThresholdDb = nullptr);

    /// Frame levels (dBFS, loudest channel) of a window, one per kFrameSec. Exposed for tests and tuning.
    static std::vector<float> frameLevelsDb(const AudioBufferData& buffer, uint64_t startFrame, uint64_t frameCount);
};

} // namespace Audio
} // namespace Aestra
