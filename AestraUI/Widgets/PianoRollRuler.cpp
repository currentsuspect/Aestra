// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollWidgetShared.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// =============================================================================
// PianoRollRuler (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollRuler::PianoRollRuler()
    : scrollX_(0.0f), pixelsPerBeat_(80.0f), beatsPerBar_(4)
{
}

bool PianoRollRuler::onMouseEvent(const NUIMouseEvent& event) {
    const auto bounds = getBounds();
    if (!bounds.contains(event.position) && !isScrubbing_) return false;

    // Zoom on Wheel
    if (event.wheelDelta != 0.0f) {
        if (onZoomRequested) {
            float localX = event.position.x - bounds.x;
            onZoomRequested(event.wheelDelta, localX);
            return true;
        }
    }

    auto scrubToPointer = [&]() {
        const double localX = static_cast<double>(event.position.x - bounds.x + scrollX_);
        const double beat = std::max(0.0, localX / static_cast<double>(pixelsPerBeat_));
        playheadBeat_ = beat;
        if (onPlayheadScrubbed) onPlayheadScrubbed(beat, true);
        repaint();
    };

    if (event.pressed && event.button == NUIMouseButton::Left) {
        isScrubbing_ = true;
        scrubToPointer();
        return true;
    }
    if (isScrubbing_) {
        if (event.released && event.button == NUIMouseButton::Left) {
            scrubToPointer();
            isScrubbing_ = false;
            if (onPlayheadScrubbed) onPlayheadScrubbed(playheadBeat_, false);
            return true;
        }
        scrubToPointer();
        return true;
    }
    return NUIComponent::onMouseEvent(event);
}

// Ruler Render: "Mature" Playlist Style
void PianoRollRuler::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    const auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    const auto rulerBackground = NUIThemeManager::getInstance().getColor("backgroundPrimary");
    const auto border = NUIColor::white().withAlpha(0.075f);
    const auto highlight = NUIColor::white().withAlpha(0.014f);
    const auto text = theme.getColor("textPrimary").withAlpha(0.86f);
    const auto tick = NUIColor::fromHex(0x2e2e2e).withAlpha(0.82f);

    renderer.fillRoundedRect(bounds, 3.0f, rulerBackground);
    renderer.fillRect(NUIRect(bounds.x, bounds.y, bounds.width, 1.0f), highlight);
    renderer.strokeRoundedRect(bounds, 3.0f, 1.0f, border);
    renderer.drawLine(NUIPoint(bounds.x, bounds.bottom() - 1.0f),
                      NUIPoint(bounds.right(), bounds.bottom() - 1.0f),
                      1.0f,
                      border);

    renderer.setClipRect(bounds);

    const int beatsPerBar = std::max(1, beatsPerBar_);
    const float pixelsPerBar = pixelsPerBeat_ * beatsPerBar;
    int barStride = 1;
    while ((pixelsPerBar * static_cast<float>(barStride)) < 28.0f && barStride < 128) {
        barStride *= 2;
    }

    int startBar = static_cast<int>(std::floor(scrollX_ / pixelsPerBar));
    startBar = static_cast<int>(std::floor(static_cast<double>(startBar) / barStride)) * barStride;
    const int visibleBars = static_cast<int>(std::ceil((scrollX_ + bounds.width) / pixelsPerBar)) - startBar;
    const int endBar = startBar + visibleBars + barStride;

    for (int bar = startBar; bar <= endBar; bar += barStride) {
        const float x = snapVerticalLineX(bounds.x + bar * pixelsPerBar - scrollX_);
        const int barNumber = bar + 1;
        const bool isMajorBar = barStride > 1 || barNumber == 1 || ((barNumber - 1) % 4 == 0);
        const float fontSize = isMajorBar ? 11.0f : 10.0f;
        const float textY = std::round(renderer.calculateTextY(bounds, fontSize));
        renderer.drawText(std::to_string(barNumber),
                          NUIPoint(x + 4.0f, textY),
                          fontSize,
                          isMajorBar ? text : text.withAlpha(0.76f));

        const float tickHeight = isMajorBar ? bounds.height * 0.58f : bounds.height * 0.24f;
        renderer.drawLine(NUIPoint(x, bounds.bottom() - tickHeight),
                          NUIPoint(x, bounds.bottom()),
                          isMajorBar ? 1.15f : 1.0f,
                          isMajorBar ? tick : tick.withAlpha(0.58f));

        if (pixelsPerBeat_ >= 10.0f && barStride == 1) {
            for (int beat = 1; beat < beatsPerBar; ++beat) {
                const float beatX = x + beat * pixelsPerBeat_;
                renderer.drawLine(NUIPoint(beatX, bounds.bottom() - bounds.height * 0.22f),
                                  NUIPoint(beatX, bounds.bottom()),
                                  1.0f,
                                  NUIColor::fromHex(0x262626).withAlpha(0.36f));
            }
        }
    }

    const float playheadX =
        snapVerticalLineX(beatToScreenX(playheadBeat_, pixelsPerBeat_, scrollX_, bounds.x));
    if (playheadX >= bounds.x && playheadX <= bounds.right()) {
        // Same marker language as the Track Manager playhead: a small downward
        // triangle at the ruler's bottom edge, tip pointing down at the grid line.
        const auto accent = theme.getColor("accentPrimary");
        constexpr float markerHalfW = 4.0f;
        constexpr float markerH = 5.0f;
        const float scale = isScrubbing_ ? 1.25f : 1.0f;
        const float halfW = markerHalfW * scale;
        const float height = markerH * scale;
        const float markerTop = bounds.bottom() - 2.0f - height;
        const NUIPoint tip(playheadX, bounds.bottom() - 2.0f);
        const NUIPoint left(playheadX - halfW, markerTop);
        const NUIPoint right(playheadX + halfW, markerTop);
        renderer.drawLine(left, right, 1.2f, accent.withAlpha(0.92f));
        renderer.drawLine(left, tip, 1.2f, accent.withAlpha(0.92f));
        renderer.drawLine(right, tip, 1.2f, accent.withAlpha(0.92f));
    }

    renderer.clearClipRect();
}
void PianoRollRuler::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollRuler::setScrollX(float scrollX) { scrollX_ = scrollX; repaint(); }

} // namespace AestraUI
