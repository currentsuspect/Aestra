// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

inline float timelineGridLevelFade(float spacingPixels) {
    constexpr float kLevelHidePixels = 14.0f;
    constexpr float kLevelFullPixels = 28.0f;
    return std::clamp((spacingPixels - kLevelHidePixels) / (kLevelFullPixels - kLevelHidePixels), 0.0f, 1.0f);
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
                               const NUIColor& ink = NUIColor::white()) {
    const float gridWidth = std::max(0.0f, gridEndX - gridStartX);
    beatsPerBar = std::max(1, beatsPerBar);
    const float pixelsPerBar = pixelsPerBeat * static_cast<float>(beatsPerBar);
    if (gridWidth <= 0.0f || pixelsPerBeat <= 0.0f || pixelsPerBar <= 0.0f) {
        return;
    }

    constexpr float kLevelFullPx = 28.0f;
    const auto lineAlpha = [&](float spacingPx) {
        const float strength =
            0.028f + (0.105f - 0.028f) *
                         std::clamp((spacingPx - kLevelFullPx) / (kLevelFullPx * 3.0f), 0.0f, 1.0f);
        return strength * timelineGridLevelFade(spacingPx);
    };

    const int barStride = timelineGridBarStride(pixelsPerBeat, beatsPerBar);

    constexpr float kZebraAlpha = 0.030f;
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
    drawZebraLevel(barStride, kZebraAlpha * fineT);
    drawZebraLevel(barStride * 2, kZebraAlpha * (1.0f - fineT));

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
    const float beatLineAlpha = lineAlpha(pixelsPerBeat);
    const float halfBeatLineAlpha = lineAlpha(pixelsPerBeat * 0.5f);

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
            const float alpha = lineAlpha(levelSpacing);
            if (alpha > 0.001f) drawGridLine(barX, alpha);
        }

        if (beatLineAlpha > 0.001f) {
            for (int beat = 1; beat < beatsPerBar; ++beat) {
                const float beatX = barX + beat * pixelsPerBeat;
                if (beatX >= gridStartX && beatX <= gridEndX) drawGridLine(beatX, beatLineAlpha);
            }
        }

        if (halfBeatLineAlpha > 0.001f) {
            const float halfBeatOffset = pixelsPerBeat * 0.5f;
            for (int beat = 0; beat < beatsPerBar; ++beat) {
                const float subBeatX = barX + beat * pixelsPerBeat + halfBeatOffset;
                if (subBeatX >= gridStartX && subBeatX <= gridEndX) {
                    drawGridLine(subBeatX, halfBeatLineAlpha);
                }
            }
        }
    }
}

} // namespace AestraUI
