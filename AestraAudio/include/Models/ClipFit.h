// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Aestra {
namespace Audio {

/**
 * @brief Varispeed tempo-fit math for #747 ("fit audio clip to N bars").
 *
 * Sets a clip's timeline span to N bars and derives the playbackRate that
 * makes its source content exactly fill that span. Pitch follows tempo —
 * this is varispeed, not time-stretch; callers must say so in the UI.
 * The shared bounds match ClipEdits::effectiveVarispeed() so a fitted clip
 * never plays outside what the render kernel supports.
 *
 * The app's meter is fixed 4/4 today (TimelineClock default; nothing
 * overrides it), so beats-per-bar is a named constant here rather than a
 * parameter pretending there is a meter source.
 */
constexpr int FIT_BARS_BEATS_PER_BAR = 4;

struct FitToBarsResult {
    double durationBeats{0.0};
    double durationSeconds{0.0};
    float playbackRate{1.0f}; ///< Stored base rate to write into ClipEdits (pitch folded out).
    bool rateClamped{false}; ///< True when the fit needs a rate outside 0.25x-4x (effective
                             ///< varispeed or pitch-decomposed base); span still applied, content
                             ///< will not exactly fill it.
};

/**
 * @brief Compute the clip edits for fitting N bars.
 *
 * @param sourceRegionSeconds Audible source content length in source-domain
 *                            seconds (trim/offset aware — what the render
 *                            region actually consumes).
 * @param bpm                 Project tempo (> 0).
 * @param bars                Target bar count (> 0).
 * @param pitchSemitones      Current clip pitch (retained by the fit). The
 *                            renderer plays playbackRate x 2^(pitch/12)
 *                            (ClipEdits::effectiveVarispeed, mirrored by
 *                            ClipRenderKernel), so the derived base rate is
 *                            the span-filling effective rate divided by the
 *                            pitch factor.
 * @return nullopt on invalid input; otherwise the span + derived base rate.
 */
inline std::optional<FitToBarsResult> computeFitToBars(
    double sourceRegionSeconds, double bpm, int bars, float pitchSemitones = 0.0f) {
    if (!(sourceRegionSeconds > 0.0) || !std::isfinite(sourceRegionSeconds) ||
        !(bpm > 0.0) || !std::isfinite(bpm) || bars <= 0) {
        return std::nullopt;
    }

    FitToBarsResult result;
    result.durationBeats = static_cast<double>(bars) * FIT_BARS_BEATS_PER_BAR;
    result.durationSeconds = result.durationBeats * 60.0 / bpm;
    if (!(result.durationSeconds > 0.0) || !std::isfinite(result.durationSeconds)) {
        return std::nullopt;
    }

    // Effective varispeed required to fill the span, then decomposed into the
    // STORED base rate. effectiveVarispeed() clamps BOTH factors to the
    // envelope, so the base must stay inside [0.25, 4] too: a base like 8.0
    // at -24 st would render at clamp(8.0) x 0.25 = 1.0, not the requested
    // 2.0. Either clamp means content cannot exactly fill the span and is
    // reported on rateClamped.
    const double effectiveRate = sourceRegionSeconds / result.durationSeconds;
    const bool effectiveClamped = effectiveRate < 0.25 || effectiveRate > 4.0;
    const double baseRate = effectiveRate /
                            static_cast<double>(ClipEdits::playbackRateFromSemitones(pitchSemitones));
    const bool baseClamped = baseRate < 0.25 || baseRate > 4.0;
    result.rateClamped = effectiveClamped || baseClamped;
    result.playbackRate = static_cast<float>(std::clamp(baseRate, 0.25, 4.0));
    return result;
}

} // namespace Audio
} // namespace Aestra
