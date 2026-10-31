// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollLook.h"
#include "PianoRollWidgetShared.h"
#include "../Helpers/TimelineGridRenderer.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// =============================================================================
// PianoRollGrid (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollGrid::PianoRollGrid()
    : pixelsPerBeat_(80.0f), keyHeight_(24.0f), scrollX_(0.0f), scrollY_(0.0f),
      beatsPerBar_(4)
{
}

void PianoRollGrid::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    const auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    renderer.setClipRect(bounds);
    renderer.fillRect(bounds, NUIColor::fromHex(0x000000)); // FD-63: the board's bed is true black

    // The ruler loop zone: pattern playback loops only this range (SPEC 3 §5.2).
    if (loopZoneActive_ && loopZoneEnd_ > loopZoneStart_) {
        const float x1 = bounds.x + static_cast<float>(loopZoneStart_ * pixelsPerBeat_) - scrollX_;
        const float x2 = bounds.x + static_cast<float>(loopZoneEnd_ * pixelsPerBeat_) - scrollX_;
        renderer.fillRect(NUIRect(x1, bounds.y, x2 - x1, bounds.height),
                          theme.getColor("accentPrimary").withAlpha(0.07f));
    }

    const auto gridInk = theme.getCurrentTheme().textPrimary;

    int startPitch = 127 - static_cast<int>(scrollY_ / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + bounds.height) / keyHeight_);
    startPitch = std::clamp(startPitch + 2, 0, 127);
    endPitch = std::clamp(endPitch - 2, 0, 127);

    for (int pitch = startPitch; pitch >= endPitch; --pitch) {
        const float y = bounds.y + (127 - pitch) * keyHeight_ - scrollY_;
        const NUIRect row(bounds.x, y, bounds.width, keyHeight_);
        // FD-63, board "A + C": the root's row warm, the scale's rows a step off black, a line under each.
        renderer.fillRect(row, PianoRollLook::rowFill(pitch, rootKey_, scaleType_));
        renderer.drawLine(NUIPoint(bounds.x, row.bottom() - 0.5f), NUIPoint(bounds.right(), row.bottom() - 0.5f), 1.0f,
                          PianoRollLook::rowLine(pitch));
    }

    // FD-63 chord-tone rows: over each chord's span, the rows of its tones are banded in the warm accent (board A + C).
    if (chords_) {
        const auto band = NUIColor::fromHex(0xf2a35e).withAlpha(0.075f);
        for (const auto& c : *chords_) {
            if (c.root < 0 || c.toneMask == 0) continue;
            const float x1 = std::max(bounds.x, bounds.x + static_cast<float>(c.startBeat * pixelsPerBeat_) - scrollX_);
            const float x2 = std::min(bounds.right(), bounds.x + static_cast<float>(c.endBeat * pixelsPerBeat_) - scrollX_);
            if (x2 <= x1) continue;
            for (int pitch = startPitch; pitch >= endPitch; --pitch) {
                if (!(c.toneMask & (1u << (pitch % 12)))) continue;
                const float y = bounds.y + (127 - pitch) * keyHeight_ - scrollY_;
                renderer.fillRect(NUIRect(x1, y, x2 - x1, keyHeight_ - 1.0f), band);
            }
        }
    }

    TimelineGridStyle gridStyle;
    // Gamma-space alphas (F3): converted from linear-era tuning, which inflated
    // low-alpha light-on-dark roughly 6x before compositing was unified. Same
    // rendered result, different number to express it.
    // FD-63 board lines on black, in the text ink: bars #57514a, beats #25221f, steps #100f0d.
    gridStyle.barLineAlpha = 0.34f;
    gridStyle.beatLineAlpha = 0.135f;
    gridStyle.subdivisionLineAlpha = 0.055f;
    // No bar zebra (SPEC 3 §5.1, owner): at 0.066 every other bar read as a band of near
    // black (bed RGB 0-4 against 15-19) in which the beat lines vanished. The timeline gets
    // by with lines alone, and at matching line alphas so does this.
    gridStyle.zebraAlpha = 0.0f;
    renderTimelineGrid(renderer, bounds, bounds.x, bounds.right(), scrollX_, pixelsPerBeat_, beatsPerBar_, gridInk,
                       gridStyle, getSnapSubdivisionBeats());

    if (hoveredPitch_ >= 0 && hoveredPitch_ <= 127) {
        const float hoverY = bounds.y + (127 - hoveredPitch_) * keyHeight_ - scrollY_;
        const NUIRect hoverRow(bounds.x, hoverY, bounds.width, keyHeight_);
        const auto hoverColor = theme.getColor("accentPrimary").withAlpha(0.075f);
        renderer.fillRect(hoverRow, hoverColor);
        renderer.drawLine(NUIPoint(bounds.x, hoverRow.y),
                          NUIPoint(bounds.right(), hoverRow.y),
                          1.0f,
                          theme.getColor("accentPrimary").withAlpha(0.28f));
        renderer.drawLine(NUIPoint(bounds.x, hoverRow.bottom()),
                          NUIPoint(bounds.right(), hoverRow.bottom()),
                          1.0f,
                          theme.getColor("accentPrimary").withAlpha(0.18f));
    }

    const float patternEndX =
        beatToScreenX(totalDurationBeats_, pixelsPerBeat_, scrollX_, bounds.x);
    if (patternEndX >= bounds.x && patternEndX <= bounds.right()) {
        renderer.drawLine(NUIPoint(patternEndX, bounds.y),
                          NUIPoint(patternEndX, bounds.bottom()),
                          1.0f,
                          theme.getColor("accentPrimary").withAlpha(0.34f));
    }

    renderer.clearClipRect();
}
void PianoRollGrid::setPixelsPerBeat(float ppb) { const auto next = std::max(10.0f, ppb); if (pixelsPerBeat_ == next) return; pixelsPerBeat_ = next; repaint(); }
void PianoRollGrid::setKeyHeight(float height) { const auto next = std::max(8.0f, height); if (keyHeight_ == next) return; keyHeight_ = next; repaint(); }
void PianoRollGrid::setScrollOffsetX(float offset) { const auto next = offset; if (scrollX_ == next) return; scrollX_ = next; repaint(); }
void PianoRollGrid::setScrollOffsetY(float offset) { const auto next = offset; if (scrollY_ == next) return; scrollY_ = next; repaint(); }

} // namespace AestraUI
