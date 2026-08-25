// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
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
constexpr int kFitBarsBeatsPerBar = 4;

struct FitToBarsResult {
    double durationBeats{0.0};
    double durationSeconds{0.0};
    float playbackRate{1.0f};
    bool rateClamped{false}; ///< True when content/span fell outside 0.25×–4×; span still applied, content will not exactly fill it.
};

/**
 * @brief Compute the clip edits for fitting N bars.
 *
 * @param sourceRegionSeconds Audible source content length in source-domain
 *                            seconds (trim/offset aware — what the render
 *                            region actually consumes).
 * @param bpm                 Project tempo (> 0).
 * @param bars                Target bar count (> 0).
 * @return nullopt on invalid input; otherwise the span + derived rate.
 */
inline std::optional<FitToBarsResult> computeFitToBars(
    double sourceRegionSeconds, double bpm, int bars) {
    if (!(sourceRegionSeconds > 0.0) || !(bpm > 0.0) || bars <= 0) {
        return std::nullopt;
    }

    FitToBarsResult result;
    result.durationBeats = static_cast<double>(bars) * kFitBarsBeatsPerBar;
    result.durationSeconds = result.durationBeats * 60.0 / bpm;

    const double rawRate = sourceRegionSeconds / result.durationSeconds;
    result.rateClamped = rawRate < 0.25 || rawRate > 4.0;
    result.playbackRate = static_cast<float>(std::clamp(rawRate, 0.25, 4.0));
    return result;
}

} // namespace Audio
} // namespace Aestra
