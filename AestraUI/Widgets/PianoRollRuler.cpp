// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "PlayheadMarker.h"
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
    if (!bounds.contains(event.position) && !isScrubbing_ && !isDrawingZone_) return false;

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

    // Loop zone (SPEC 3 §5.2): right-drag, or Ctrl+left-drag, like the timeline ruler. A plain
    // left press still scrubs the playhead.
    auto zoneBeatAtPointer = [&]() {
        const double localX = static_cast<double>(event.position.x - bounds.x + scrollX_);
        const double beat = std::max(0.0, localX / static_cast<double>(pixelsPerBeat_));
        return snapBeat ? std::max(0.0, snapBeat(beat)) : beat;
    };
    const bool zonePress = event.pressed && (event.button == NUIMouseButton::Right ||
                                             (event.button == NUIMouseButton::Left &&
                                              (event.modifiers & NUIModifiers::Ctrl)));
    if (zonePress && allowLoopZone_ && !isScrubbing_ && !isDrawingZone_) {
        isDrawingZone_ = true;
        zoneButton_ = event.button;
        zoneAnchorBeat_ = zoneBeatAtPointer();
        zoneDragBeat_ = zoneAnchorBeat_;
        repaint();
        return true;
    }
    if (isDrawingZone_) {
        zoneDragBeat_ = zoneBeatAtPointer();
        if (event.released && event.button == zoneButton_) {
            isDrawingZone_ = false;
            zoneButton_ = NUIMouseButton::None;
            const double start = std::min(zoneAnchorBeat_, zoneDragBeat_);
            const double end = std::max(zoneAnchorBeat_, zoneDragBeat_);
            if (end - start > 1e-6 && onLoopZoneDrawn) {
                onLoopZoneDrawn(start, end);
            } else if (onLoopZoneCleared) {
                onLoopZoneCleared(); // a click, not a drag: it clears the highlight
            }
        }
        repaint();
        return true;
    }

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

    // FD-63, board "A + C": one cell per bar on the bed, a rule at its left edge and its number at the top left. The
    // chord the harmony service finds goes under the number (V83-HS1); the loop range is a 4 px accent strip on top.
    const auto bed = theme.getColor("backgroundPrimary");
    const auto rule = theme.getColor("textDisabled");
    const auto number = theme.getColor("textMuted");
    const auto accent = theme.getColor("accentPrimary");
    renderer.fillRect(bounds, bed);
    renderer.drawLine(NUIPoint(bounds.x, bounds.bottom() - 0.5f), NUIPoint(bounds.right(), bounds.bottom() - 0.5f), 1.0f,
                      theme.getColor("border"));

    renderer.setClipRect(bounds);

    {
        const bool drawing = isDrawingZone_;
        const double zs = drawing ? std::min(zoneAnchorBeat_, zoneDragBeat_) : loopZoneStart_;
        const double ze = drawing ? std::max(zoneAnchorBeat_, zoneDragBeat_) : loopZoneEnd_;
        if ((drawing || loopZoneActive_) && ze > zs) {
            const float x1 = bounds.x + static_cast<float>(zs * pixelsPerBeat_) - scrollX_;
            const float x2 = bounds.x + static_cast<float>(ze * pixelsPerBeat_) - scrollX_;
            renderer.fillRect(NUIRect(x1, bounds.y, x2 - x1, 4.0f), accent);
        }
    }

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
        renderer.drawLine(NUIPoint(x, bounds.y), NUIPoint(x, bounds.bottom()), 1.0f, rule);
        renderer.drawText(std::to_string(bar + 1), NUIPoint(x + 8.0f, bounds.y + 6.0f), 12.0f, number);
    }

    const float playheadX =
        snapVerticalLineX(beatToScreenX(playheadBeat_, pixelsPerBeat_, scrollX_, bounds.x));
    if (playheadX >= bounds.x && playheadX <= bounds.right()) {
        Playhead::drawHead(renderer, playheadX, bounds.bottom(), theme.getColor("textPrimary"),
                           isScrubbing_ ? 1.25f : 1.0f);
    }

    renderer.clearClipRect();
}
void PianoRollRuler::setPixelsPerBeat(float ppb) { const auto next = std::max(10.0f, ppb); if (pixelsPerBeat_ == next) return; pixelsPerBeat_ = next; repaint(); }
void PianoRollRuler::setScrollX(float scrollX) { const auto next = scrollX; if (scrollX_ == next) return; scrollX_ = next; repaint(); }

} // namespace AestraUI
