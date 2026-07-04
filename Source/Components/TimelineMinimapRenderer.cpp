// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "TimelineMinimapRenderer.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Widgets/TrackColorPalette.h"
#include "TimelineMinimapModel.h"
#include "TimelineSummaryCache.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {

constexpr double kEpsilonDomain = 1e-9;

inline float clamp01(float v)
{
    return std::max(0.0f, std::min(1.0f, v));
}

inline float logNorm(float value, float maxValue)
{
    if (!(maxValue > 0.0f)) return 0.0f;
    if (value <= 0.0f) return 0.0f;
    const float n = std::log1p(value) / std::log1p(maxValue);
    return clamp01(n);
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline NUIColor withAlphaClamp(const NUIColor& c, float a)
{
    return c.withAlpha(clamp01(a));
}

inline NUIColor blendAdd(const NUIColor& a, const NUIColor& b)
{
    return NUIColor(
        std::min(1.0f, a.r + b.r),
        std::min(1.0f, a.g + b.g),
        std::min(1.0f, a.b + b.b),
        std::min(1.0f, a.a + b.a)
    );
}

inline float bucketValueClamped(int32_t v)
{
    return static_cast<float>(std::max(0, v));
}

inline float aggregateValue(const TimelineSummaryBucket* buckets, int a, int b, TimelineMinimapAggregation agg,
                            float (*bucketFn)(const TimelineSummaryBucket&))
{
    if (a > b) std::swap(a, b);
    float acc = 0.0f;
    if (agg == TimelineMinimapAggregation::SumDensity) {
        for (int i = a; i <= b; ++i) acc += bucketFn(buckets[i]);
        return acc;
    }

    for (int i = a; i <= b; ++i) acc = std::max(acc, bucketFn(buckets[i]));
    return acc;
}

} // namespace

void TimelineMinimapRenderer::render(NUIRenderer& renderer, const TimelineMinimapLayout& layout,
                                    const TimelineMinimapModel& model, const TimelineMinimapRenderColors& colors) const
{
    const NUIRect& b = layout.bounds;
    if (b.isEmpty()) return;

    // Glass shell — flat dark base. The old lightened top half was a frosty
    // highlight that fought the neutral black/grey material language.
    renderer.fillRoundedRect(b, 7.0f, colors.glassFill);
    if (colors.glassBorder.a > 0.0f) {
        renderer.strokeRoundedRect(b, 7.0f, 1.0f, colors.glassBorder);
    }

    // Corner separator (matches the ruler corner boundary).
    if (!layout.cornerRect.isEmpty() && colors.cornerSeparator.a > 0.0f) {
        renderer.drawLine(NUIPoint(layout.cornerRect.right(), b.y), NUIPoint(layout.cornerRect.right(), b.bottom()),
                          1.0f, colors.cornerSeparator);
    }

    const TimelineSummarySnapshot snap = model.summary ? TimelineSummarySnapshot{model.summary->summary, model.summary->version}
                                                       : TimelineSummarySnapshot{};
    const TimelineSummary* s = snap.summary;
    if (!s || s->bucketCount == 0 || s->buckets.empty()) {
        return;
    }

    const double domainStart = s->domainStartBeat;
    const double domainEnd = s->domainEndBeat;
    const double denom = domainEnd - domainStart;
    if (!(denom > kEpsilonDomain)) {
        return;
    }

    const NUIRect map = layout.mapRect;
    if (map.isEmpty()) return;

    // "Loaded baseline" so empty looks different from not-loaded.
    renderer.drawLine(NUIPoint(map.x, map.bottom() - 1.0f), NUIPoint(map.right(), map.bottom() - 1.0f), 1.0f,
                      colors.baseline);

    NUIRect viewportRect;
    bool hasViewportRect = false;
    if (model.view.isValid()) {
        const float x0 = timeToX(model.view.start, map, domainStart, domainEnd);
        const float x1 = timeToX(model.view.end, map, domainStart, domainEnd);
        const float vx = std::min(x0, x1);
        const float vw = std::max(1.0f, std::abs(x1 - x0));
        viewportRect = NUIRect(vx, map.y, vw, map.height);
        hasViewportRect = true;
        renderer.fillRoundedRect(viewportRect, 5.0f, colors.viewFill);
    }

    const int N = static_cast<int>(s->bucketCount);
    if (N <= 0) return; // No data to render
    const int W = std::max(1, static_cast<int>(std::round(map.width)));
    const auto* buckets = s->buckets.data();

    // Histogram bars: one column per pixel (resampling via MAX or SUM).
    const float barTop = map.y + 1.0f;
    const float barBottom = map.bottom() - 1.0f;
    const float barHeight = std::max(0.0f, barBottom - barTop);
    const float minH = 1.0f;
    const float maxH = std::max(minH, barHeight);

    int globalMaxLaneWithContent = -1;
    for (int i = static_cast<int>(TimelineSummaryBucket::kTrackLaneCount) - 1; i >= 0; --i) {
        bool lanePresent = false;
        for (int k = 0; k < N; ++k) {
            if (buckets[k].trackCounts[i] > 0) {
                lanePresent = true;
                break;
            }
        }
        if (lanePresent) {
            globalMaxLaneWithContent = i;
            break;
        }
    }

    const int numTracksToShow = globalMaxLaneWithContent + 1;
    const float availableHeight = barBottom - barTop;
    const float minTrackH = 1.0f;
    const float maxTrackH = 3.0f;
    const float gap = 0.0f;

    float trackH = maxTrackH;
    float rowH = maxTrackH;
    if (numTracksToShow > 0) {
        rowH = availableHeight / static_cast<float>(numTracksToShow);
        trackH = std::max(minTrackH, std::min(maxTrackH, rowH - gap));
    }

    for (int px = 0; px < W; ++px) {
        const int a = std::clamp(static_cast<int>((static_cast<int64_t>(px) * N) / W), 0, N - 1);
        const int b = std::clamp(static_cast<int>(((static_cast<int64_t>(px + 1) * N) / W) - 1), 0, N - 1);

        float value = 0.0f;
        float maxValue = 1.0f;
        NUIColor tint = colors.audioTint;

        if (model.mode == TimelineMinimapMode::Automation) {
            value = aggregateValue(
                buckets, a, b, model.aggregation,
                [](const TimelineSummaryBucket& bk) { return bucketValueClamped(bk.automationCount); });
            maxValue = static_cast<float>(std::max<uint32_t>(1u, s->maxAutomation));
            tint = colors.automationTint;
        } else if (model.mode == TimelineMinimapMode::Energy) {
            value = aggregateValue(
                buckets, a, b, model.aggregation,
                [](const TimelineSummaryBucket& bk) { return std::max(0.0f, bk.energySum); });
            maxValue = std::max(1.0f, s->maxEnergySum);
            tint = colors.audioTint;
        } else {
            const float audio = aggregateValue(
                buckets, a, b, model.aggregation,
                [](const TimelineSummaryBucket& bk) { return bucketValueClamped(bk.audioCount); });
            const float midi = aggregateValue(
                buckets, a, b, model.aggregation,
                [](const TimelineSummaryBucket& bk) { return bucketValueClamped(bk.midiCount); });

            value = std::max(audio, midi);
            const float maxAudio = static_cast<float>(std::max<uint32_t>(1u, s->maxAudio));
            const float maxMidi = static_cast<float>(std::max<uint32_t>(1u, s->maxMidi));
            maxValue = std::max(maxAudio, maxMidi);

            // Subtle type tint: prefer the dominant signal, blend when similar.
            const float na = logNorm(audio, maxAudio);
            const float nm = logNorm(midi, maxMidi);
            if (na <= 0.0f && nm <= 0.0f) {
                tint = colors.baseline; // "empty" uses neutral baseline, not an accent.
            } else if (na > nm * 1.2f) {
                tint = colors.audioTint;
            } else if (nm > na * 1.2f) {
                tint = colors.midiTint;
            } else {
                tint = blendAdd(colors.audioTint.withAlpha(0.5f), colors.midiTint.withAlpha(0.5f));
            }
        }

        const float x = map.x + static_cast<float>(px);
        const float normValue = (maxValue > 0.0f) ? std::clamp(value / maxValue, 0.0f, 1.0f) : 0.0f;
        if (normValue > 0.0f) {
            const float barHeight = std::max(1.0f, normValue * (barBottom - barTop));
            const float barY = barBottom - barHeight;
            renderer.fillRect(NUIRect(x, barY, 1.0f, barHeight), tint.withAlpha(0.18f));
        }
        
        // Loop through supported lanes.
        for (uint32_t i = 0; i < TimelineSummaryBucket::kTrackLaneCount; ++i) {
             bool isPresent = false;
             // Check presence in any bucket covering this pixel
             for (int k = a; k <= b; ++k) {
                if (buckets[k].trackCounts[i] > 0) {
                    isPresent = true;
                    break;
                }
             }
             
             if (isPresent) {
                 float currentY = barTop + (static_cast<float>(i) * rowH);
                 
                 // Stop drawing if out of bounds (culling)
                 if (currentY + trackH > barBottom) break; 
                 
                 // Shared TRACK_PALETTE, same modulo as the track-color fallback
                 const NUIColor lineTint =
                     NUIColor::fromARGB(paletteIndexToARGB(static_cast<int>(i % PALETTE_SIZE))).withAlpha(0.9f);

                 NUIRect lineRect(x, currentY, 1.0f, trackH);
                 renderer.fillRect(lineRect, lineTint);
             }
        }
    }

    // Overlays: selection + loop under viewport.
    if (model.showLoop && model.loop.isValid()) {
        const float x0 = timeToX(model.loop.start, map, domainStart, domainEnd);
        const float x1 = timeToX(model.loop.end, map, domainStart, domainEnd);
        const float lx = std::min(x0, x1);
        const float lw = std::abs(x1 - x0);
        if (lw > 0.5f) {
            renderer.fillRect(NUIRect(lx, map.y, lw, map.height), colors.loopFill);
        }
    }

    if (model.showSelection && model.selection.isValid()) {
        const float x0 = timeToX(model.selection.start, map, domainStart, domainEnd);
        const float x1 = timeToX(model.selection.end, map, domainStart, domainEnd);
        const float sx = std::min(x0, x1);
        const float sw = std::abs(x1 - x0);
        if (sw > 0.5f) {
            renderer.fillRect(NUIRect(sx, map.y, sw, map.height), colors.selectionFill);
        }
    }

    // Viewport outline stays above the content; fill was rendered beneath it.
    if (hasViewportRect) {
        renderer.strokeRoundedRect(viewportRect, 5.0f, 1.0f, colors.viewOutline);
    }

    // Playhead: collision-free outline (dark underlay + bright center).
    if (model.showPlayhead) {
        const float phX = timeToX(model.playheadBeat, map, domainStart, domainEnd);
        if (phX >= map.x - 1.0f && phX <= map.right() + 1.0f) {
            renderer.drawLine(NUIPoint(phX, map.y), NUIPoint(phX, map.bottom()), 2.0f, colors.playheadDark);
            renderer.drawLine(NUIPoint(phX, map.y), NUIPoint(phX, map.bottom()), 1.0f, colors.playheadBright);
        }
    }
}

float TimelineMinimapRenderer::timeToX(double beat, const NUIRect& mapRect, double domainStartBeat, double domainEndBeat)
{
    const double denom = domainEndBeat - domainStartBeat;
    if (!(denom > kEpsilonDomain)) return mapRect.x;
    const double u = std::max(0.0, std::min(1.0, (beat - domainStartBeat) / denom));
    return mapRect.x + static_cast<float>(u) * mapRect.width;
}

double TimelineMinimapRenderer::xToTime(float x, const NUIRect& mapRect, double domainStartBeat, double domainEndBeat)
{
    const double denom = domainEndBeat - domainStartBeat;
    if (!(denom > kEpsilonDomain)) return domainStartBeat;
    const double u = std::max(0.0, std::min(1.0, static_cast<double>((x - mapRect.x) / mapRect.width)));
    return domainStartBeat + u * denom;
}

} // namespace AestraUI
