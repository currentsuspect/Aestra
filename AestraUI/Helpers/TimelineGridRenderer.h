// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

struct TimelineGridStyle {
    float barLineAlpha = 0.15f;
    float beatLineAlpha = 0.05f;
    float subdivisionLineAlpha = 0.026f;
    float zebraAlpha = 0.016f;
};

inline float timelineGridLevelFade(float spacingPixels) {
    constexpr float kLevelHidePixels = 14.0f;
    constexpr float kLevelFullPixels = 28.0f;
    return std::clamp((spacingPixels - kLevelHidePixels) / (kLevelFullPixels - kLevelHidePixels), 0.0f, 1.0f);
}

/**
 * Zoom fade for lines the snap lands on. They show from 5 px spacing (fading out below that, gone
 * under 4 px, where 1 px lines stop reading as separate lines), where decorative tiers need
 * 14-28 px. At the default timeline zoom a beat is ~10 px: the decorative fade hid the beat lines
 * even under a Beat snap, so the grid showed bars only whatever the snap said. The half-beat tier
 * of a 1/2 or 1/4 snap is 5 px there, so it shows too and a fine snap reads differently from Beat;
 * the 2.5 px quarter tier stays hidden until zoomed in (review, #973).
 */
inline float timelineSnapTierFade(float spacingPixels) {
    constexpr float SNAP_HIDE_PIXELS = 4.0f;
    constexpr float SNAP_FULL_PIXELS = 5.0f;
    return std::clamp((spacingPixels - SNAP_HIDE_PIXELS) / (SNAP_FULL_PIXELS - SNAP_HIDE_PIXELS), 0.0f, 1.0f);
}

/** True when a snap finer than a bar is active, so beat-level tiers are snap positions. */
inline bool timelineSnapIsSubBar(double subdivisionBeats) {
    return subdivisionBeats > 0.0 && subdivisionBeats <= 1.0 + 1e-9;
}

/**
 * Alpha for a sub-beat tier drawn because of the snap: half-beats, and the finer subdivision.
 *
 * Those are the positions edits land on, so they get a visible floor, three quarters of the beat
 * tier, instead of the decorative subdivision whisper. On the timeline's black bed the whisper
 * rendered at RGB 2, so a 1/4 snap looked identical to Beat: clips snapped to lines nobody could
 * see (owner report 2026-09-24). Unsnapped keeps the decorative level and fade.
 */
inline float timelineSnapTierAlpha(const TimelineGridStyle& style, double subdivisionBeats, float spacingPixels) {
    const bool snapTier = subdivisionBeats > 0.0 && subdivisionBeats < 1.0;
    if (!snapTier) {
        return style.subdivisionLineAlpha * timelineGridLevelFade(spacingPixels);
    }
    return std::max(style.subdivisionLineAlpha, style.beatLineAlpha * 0.75f) * timelineSnapTierFade(spacingPixels);
}

inline int timelineGridBarStride(float pixelsPerBeat, int beatsPerBar) {
    constexpr float kLevelFullPixels = 28.0f;
    const float pixelsPerBar = pixelsPerBeat * static_cast<float>(std::max(1, beatsPerBar));
    int stride = 1;
    while ((pixelsPerBar * static_cast<float>(stride)) < kLevelFullPixels && stride < (1 << 20)) {
        stride *= 2;
    }
    return stride;
}

/**
 * Whether a grid tier of the given duration may be drawn under the active snap.
 *
 * The grid must never advertise positions the snap does not allow: a tier of
 * duration d is only musical when the snap duration divides it evenly (e.g. at
 * snap-to-beat a half-beat tier would show 1/2 as possible, so it is hidden).
 * Zero/absent snap keeps every tier, which is the timeline grid's contract.
 */
inline bool timelineGridTierAlignedToSnap(double tierBeats, double subdivisionBeats) {
    if (subdivisionBeats <= 0.0) {
        return true;
    }
    const double ratio = tierBeats / subdivisionBeats;
    return std::abs(ratio - std::round(ratio)) < 1e-4;
}

/**
 * Draw the canonical Track Manager timeline grid inside an arbitrary grid span.
 *
 * The power-of-two hierarchy and zebra crossfade stay stable at every zoom,
 * allowing timeline-derived editors to share one visual rhythm.
 */
inline void renderTimelineGrid(NUIRenderer& renderer,
                               const NUIRect& bounds,
                               float gridStartX,
                               float gridEndX,
                               float scrollX,
                               float pixelsPerBeat,
                               int beatsPerBar,
                               const NUIColor& ink = NUIColor::white(),
                               const TimelineGridStyle& style = {},
                               double subdivisionBeats = 0.0) {
    const float gridWidth = std::max(0.0f, gridEndX - gridStartX);
    beatsPerBar = std::max(1, beatsPerBar);
    const float pixelsPerBar = pixelsPerBeat * static_cast<float>(beatsPerBar);
    if (gridWidth <= 0.0f || pixelsPerBeat <= 0.0f || pixelsPerBar <= 0.0f) {
        return;
    }

    constexpr float kLevelFullPx = 28.0f;
    // Musical hierarchy with FIXED per-type ceilings, so bar > beat > subdivision
    // holds at every zoom (a purely spacing-based alpha made beats as strong as
    // bars when zoomed in — the "spreadsheet" look). The fade only culls a tier
    // once its lines get too dense to read.
    const auto barAlpha = [&](float levelSpacing) {
        return style.barLineAlpha * timelineGridLevelFade(levelSpacing);
    };

    const int barStride = timelineGridBarStride(pixelsPerBeat, beatsPerBar);

    // Very quiet zebra — a hint of bar-group alternation, not a checkerboard.
    const auto drawZebraLevel = [&](int strideBars, float alpha) {
        if (alpha <= 0.001f) return;
        const float blockWidth = pixelsPerBar * static_cast<float>(strideBars);
        const int startBlock = static_cast<int>(std::floor(scrollX / blockWidth));
        const int endBlock = static_cast<int>(std::floor((scrollX + gridWidth) / blockWidth)) + 1;
        for (int block = startBlock; block <= endBlock; ++block) {
            if ((block & 1) == 0) continue;
            float rectX = gridStartX + (block * blockWidth) - scrollX;
            float rectWidth = blockWidth;
            if (rectX < gridStartX) {
                rectWidth -= gridStartX - rectX;
                rectX = gridStartX;
            }
            if (rectX + rectWidth > gridEndX) {
                rectWidth = gridEndX - rectX;
            }
            if (rectWidth > 0.0f) {
                renderer.fillRect(NUIRect(rectX, bounds.y, rectWidth, bounds.height),
                                  ink.withAlpha(alpha));
            }
        }
    };

    const float pixelsPerBlock = pixelsPerBar * static_cast<float>(barStride);
    const float fineT = std::clamp((pixelsPerBlock - kLevelFullPx) / kLevelFullPx, 0.0f, 1.0f);
    drawZebraLevel(barStride, style.zebraAlpha * fineT);
    drawZebraLevel(barStride * 2, style.zebraAlpha * (1.0f - fineT));

    const double startBeat = static_cast<double>(scrollX) / pixelsPerBeat;
    const double endBeat = startBeat + static_cast<double>(gridWidth / pixelsPerBeat);
    const int firstVisibleBar = static_cast<int>(std::floor(startBeat / beatsPerBar)) - 1;
    const int lastVisibleBar = static_cast<int>(std::ceil(endBeat / beatsPerBar)) + 1;
    const auto drawGridLine = [&](float x, float alpha) {
        renderer.drawLine(NUIPoint(x, bounds.y),
                          NUIPoint(x, bounds.bottom()),
                          1.0f,
                          ink.withAlpha(alpha));
    };
    // Under a beat-or-finer snap every beat is a snap position: it uses the snap fade.
    const float beatLineAlpha =
        style.beatLineAlpha * (timelineSnapIsSubBar(subdivisionBeats) ? timelineSnapTierFade(pixelsPerBeat)
                                                                         : timelineGridLevelFade(pixelsPerBeat));
    const float halfBeatLineAlpha = timelineSnapTierAlpha(style, subdivisionBeats, pixelsPerBeat * 0.5f);
    const bool showBeatTier = timelineGridTierAlignedToSnap(1.0, subdivisionBeats);
    const bool showHalfBeatTier = timelineGridTierAlignedToSnap(0.5, subdivisionBeats);

    for (int bar = firstVisibleBar; bar <= lastVisibleBar; ++bar) {
        const float barX = gridStartX + (bar * pixelsPerBar) - scrollX;
        if (barX >= gridStartX && barX <= gridEndX) {
            int level = 20;
            if (bar != 0) {
                level = 0;
                int barMagnitude = std::abs(bar);
                while ((barMagnitude & 1) == 0 && level < 20) {
                    barMagnitude >>= 1;
                    ++level;
                }
            }
            const float levelSpacing = pixelsPerBar * static_cast<float>(1u << level);
            const float alpha = barAlpha(levelSpacing);
            if (alpha > 0.001f) drawGridLine(barX, alpha);
        }

        if (showBeatTier && beatLineAlpha > 0.001f) {
            for (int beat = 1; beat < beatsPerBar; ++beat) {
                const float beatX = barX + beat * pixelsPerBeat;
                if (beatX >= gridStartX && beatX <= gridEndX) drawGridLine(beatX, beatLineAlpha);
            }
        }

        if (showHalfBeatTier && halfBeatLineAlpha > 0.001f) {
            const float halfBeatOffset = pixelsPerBeat * 0.5f;
            for (int beat = 0; beat < beatsPerBar; ++beat) {
                const float subBeatX = barX + beat * pixelsPerBeat + halfBeatOffset;
                if (subBeatX >= gridStartX && subBeatX <= gridEndX) {
                    drawGridLine(subBeatX, halfBeatLineAlpha);
                }
            }
        }

        // Optional snap-aligned subdivision tier. The caller (e.g. the piano
        // roll grid) passes the active snap duration so every position notes
        // snap to is actually drawn. Lines that coincide with beat or bar
        // lines are skipped — those tiers already own them.
        if (subdivisionBeats > 0.0 && subdivisionBeats < 0.5) {
            const float subPixels = pixelsPerBeat * static_cast<float>(subdivisionBeats);
            const float subAlpha = timelineSnapTierAlpha(style, subdivisionBeats, subPixels);
            if (subAlpha > 0.001f) {
                const double sub = subdivisionBeats;
                const double firstSub = std::ceil((static_cast<double>(bar) * beatsPerBar) / sub);
                const double lastSub =
                    std::floor((static_cast<double>(bar + 1) * beatsPerBar - 1e-9) / sub);
                for (double k = firstSub; k <= lastSub; ++k) {
                    const double lineBeat = k * sub;
                    const double nearestBeat = std::round(lineBeat);
                    const double nearestBar = std::round(lineBeat / beatsPerBar);
                    // Skip positions another tier already draws: a second 1 px line on the same
                    // pixel doubled its brightness (half-beats outshone beats).
                    const double nearestHalf = std::round(lineBeat * 2.0) * 0.5;
                    if (std::abs(lineBeat - nearestBeat) < 1e-9 ||
                        std::abs(lineBeat - nearestBar * beatsPerBar) < 1e-9 ||
                        (showHalfBeatTier && std::abs(lineBeat - nearestHalf) < 1e-9)) {
                        continue;
                    }
                    const float subX = gridStartX + static_cast<float>(lineBeat * pixelsPerBeat) - scrollX;
                    if (subX >= gridStartX && subX <= gridEndX) drawGridLine(subX, subAlpha);
                }
            }
        }
    }
}

} // namespace AestraUI
