// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
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
    renderer.fillRect(bounds, theme.getColor("timelineBed"));

    // The ruler loop zone: pattern playback loops only this range (SPEC 3 §5.2).
    if (loopZoneActive_ && loopZoneEnd_ > loopZoneStart_) {
        const float x1 = bounds.x + static_cast<float>(loopZoneStart_ * pixelsPerBeat_) - scrollX_;
        const float x2 = bounds.x + static_cast<float>(loopZoneEnd_ * pixelsPerBeat_) - scrollX_;
        renderer.fillRect(NUIRect(x1, bounds.y, x2 - x1, bounds.height),
                          theme.getColor("accentPrimary").withAlpha(0.07f));
    }

    const auto gridInk = theme.getCurrentTheme().textPrimary;
    const auto accidentalRow = gridInk.withAlpha(0.018f);
    const auto rootRow = theme.getColor("accentPrimary").withAlpha(0.035f);
    // Out-of-scale rows recede toward the bed's own polarity, not toward black.
    const auto outOfScaleRow = gridInk.withAlpha(0.025f);
    const auto rowLine = gridInk.withAlpha(0.020f);

    int startPitch = 127 - static_cast<int>(scrollY_ / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + bounds.height) / keyHeight_);
    startPitch = std::clamp(startPitch + 2, 0, 127);
    endPitch = std::clamp(endPitch - 2, 0, 127);

    for (int pitch = startPitch; pitch >= endPitch; --pitch) {
        const float y = bounds.y + (127 - pitch) * keyHeight_ - scrollY_;
        const NUIRect row(bounds.x, y, bounds.width, keyHeight_);
        const int pitchClass = ((pitch % 12) + 12) % 12;
        const bool isRoot = pitchClass == rootKey_;
        const bool isInScale =
            scaleType_ == ScaleType::Chromatic || MusicTheory::isNoteInScale(pitch, rootKey_, scaleType_);

        if (isRoot) {
            renderer.fillRect(row, rootRow);
        } else if (isBlackKey(pitch)) {
            renderer.fillRect(row, accidentalRow);
        }
        if (!isInScale) renderer.fillRect(row, outOfScaleRow);
        renderer.drawLine(NUIPoint(bounds.x, y), NUIPoint(bounds.right(), y), 1.0f, rowLine);
    }

    TimelineGridStyle gridStyle;
    // Gamma-space alphas (F3): converted from linear-era tuning, which inflated
    // low-alpha light-on-dark roughly 6x before compositing was unified. Same
    // rendered result, different number to express it.
    gridStyle.barLineAlpha = 0.148f;         // was 0.02
    gridStyle.beatLineAlpha = 0.057f;        // was 0.005
    gridStyle.subdivisionLineAlpha = 0.023f; // was 0.002
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
void PianoRollGrid::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollGrid::setKeyHeight(float height) { keyHeight_ = std::max(8.0f, height); repaint(); }
void PianoRollGrid::setScrollOffsetX(float offset) { scrollX_ = offset; repaint(); }
void PianoRollGrid::setScrollOffsetY(float offset) { scrollY_ = offset; repaint(); }

} // namespace AestraUI
