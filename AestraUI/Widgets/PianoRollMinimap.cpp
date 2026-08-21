// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "../Platform/NUIPlatformBridge.h"
#include "PianoRollWidgetShared.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// =============================================================================
// PianoRollMinimap (split from NUIPianoRollWidgets.cpp)
// =============================================================================
PianoRollMinimap::PianoRollMinimap() 
    : startBeat_(0.0), viewDuration_(1.0), totalDuration_(100.0)
{
    // Surface tooltip to explain the meaning of the overview visualization.
    // It renders notes by pitch across the pattern (no density mapping), plus
    // the playhead and the visible-range handles.
    setTooltip("Overview: notes across the piano roll (by pitch)");
}

float PianoRollMinimap::beatToX(double beat) const {
    double ratio = beat / totalDuration_;
    return static_cast<float>(ratio * getWidth());
}

double PianoRollMinimap::xToBeat(float x) const {
    double ratio = x / getWidth();
    return ratio * totalDuration_;
}

void PianoRollMinimap::setView(double start, double duration, bool preserveEdge) {
    if (isDragging_) return;
    if (preserveEdge) {
        const double previousStart = startBeat_;
        const double previousEnd = startBeat_ + viewDuration_;
        viewDuration_ = std::clamp(duration, 0.25, totalDuration_);
        if (std::abs(previousStart) <= 1e-9) {
            startBeat_ = 0.0;
        } else if (std::abs(previousEnd - totalDuration_) <= 1e-6) {
            startBeat_ = std::max(0.0, totalDuration_ - viewDuration_);
        } else {
            startBeat_ = std::clamp(start, 0.0, std::max(0.0, totalDuration_ - viewDuration_));
        }
    } else {
        viewDuration_ = std::clamp(duration, 0.25, totalDuration_);
        startBeat_ = std::clamp(start, 0.0, std::max(0.0, totalDuration_ - viewDuration_));
    }
    repaint();
}

void PianoRollMinimap::setTotalDuration(double total) {
    const double previousTotal = totalDuration_;
    const double previousStart = startBeat_;
    const double previousEnd = startBeat_ + viewDuration_;
    totalDuration_ = std::max(1.0, total);
    viewDuration_ = std::clamp(viewDuration_, 0.25, totalDuration_);
    if (std::abs(previousStart) <= 1e-9) {
        startBeat_ = 0.0;
    } else if (std::abs(previousEnd - previousTotal) <= 1e-6) {
        startBeat_ = std::max(0.0, totalDuration_ - viewDuration_);
    } else {
        startBeat_ = std::clamp(startBeat_, 0.0, std::max(0.0, totalDuration_ - viewDuration_));
    }
    repaint();
}

void PianoRollMinimap::setPlayheadBeat(double beat) {
    playheadBeat_ = std::max(0.0, beat);
    repaint();
}

void PianoRollMinimap::setNotes(const std::vector<MidiNote>& notes) {
    notes_ = notes;
    repaint();
}

void PianoRollMinimap::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    const auto panelBg = theme.getColor("recessedPanel");
    const auto border = theme.getColor("border").withAlpha(0.48f);
    renderer.fillRoundedRect(NUIRect(b.x + 4.0f, b.y + 3.0f, b.width - 8.0f, b.height - 6.0f), 5.0f, panelBg);
    renderer.strokeRoundedRect(NUIRect(b.x + 4.0f, b.y + 3.0f, b.width - 8.0f, b.height - 6.0f), 5.0f, 1.0f, border);
    
    // View Rect
    float x1 = b.x + beatToX(startBeat_);
    float w = beatToX(viewDuration_);
    NUIRect viewRect(x1, b.y + 2, w, b.height - 4);
    
    // Navigation chrome, matching the Track Manager minimap: the viewport is a
    // neutral frame (transparent fill, grey outline, light edge grips) — not a
    // purple range. Semantic accent colors are reserved for musical content.
    const auto viewOutline = NUIColor(0.31f, 0.33f, 0.37f, 1.0f);
    const auto viewHandle = NUIColor(0.62f, 0.64f, 0.68f, 0.92f);
    
    renderer.fillRoundedRect(viewRect, 5.0f, NUIColor::transparent());
    renderer.strokeRoundedRect(viewRect, 5.0f, 1.0f, viewOutline);

    renderer.setClipRect(NUIRect(b.x + 2.0f, b.y + 2.0f, b.width - 4.0f, b.height - 4.0f));
    int minPitch = 127;
    int maxPitch = 0;
    for (const auto& note : notes_) {
        if (note.isDeleted || note.durationBeats <= 0.0) continue;
        minPitch = std::min(minPitch, std::clamp(note.pitch, 0, 127));
        maxPitch = std::max(maxPitch, std::clamp(note.pitch, 0, 127));
    }
    if (minPitch > maxPitch) {
        minPitch = 48;
        maxPitch = 72;
    } else {
        minPitch = std::max(0, minPitch - 2);
        maxPitch = std::min(127, maxPitch + 2);
    }

    const float noteAreaY = b.y + 4.0f;
    const float noteAreaH = std::max(8.0f, b.height - 8.0f);
    const int pitchSpan = std::max(1, maxPitch - minPitch + 1);
    const float laneHeight = noteAreaH / static_cast<float>(pitchSpan);
    const float visibleNoteH = std::max(3.0f, std::min(5.0f, laneHeight * 0.95f));
    const auto noteFill = theme.getColor("accentPrimary").lightened(0.08f).withAlpha(0.94f);
    const auto noteEdge = theme.getColor("accentSecondary").withAlpha(0.55f);

    for (const auto& note : notes_) {
        if (note.isDeleted || note.durationBeats <= 0.0) continue;
        const int clampedPitch = std::clamp(note.pitch, minPitch, maxPitch);
        const int relativePitch = maxPitch - clampedPitch;
        const float x = std::round(b.x + beatToX(note.startBeat));
        const float wNote = std::max(4.0f, std::round(beatToX(std::max(0.05, note.durationBeats))));
        const float laneY = noteAreaY + static_cast<float>(relativePitch) * laneHeight;
        const float y = std::round(laneY + std::max(0.0f, (laneHeight - visibleNoteH) * 0.5f));
        const float maxW = std::max(0.0f, (b.x + b.width - 4.0f) - x);
        if (maxW <= 0.0f) continue;
        NUIRect noteRect(x, y, std::min(wNote, maxW), visibleNoteH);
        renderer.fillRoundedRect(noteRect, std::min(2.0f, visibleNoteH * 0.45f), noteFill);
        renderer.strokeRoundedRect(noteRect, std::min(2.0f, visibleNoteH * 0.45f), 1.0f, noteEdge);
    }
    renderer.clearClipRect();

    const auto playheadColor = theme.getColor("textPrimary").withAlpha(0.85f);
    const auto playheadDark = theme.getColor("shadow").withAlpha(0.75f);
    const float playheadX = b.x + beatToX(playheadBeat_);
    if (playheadX >= b.x && playheadX <= b.x + b.width) {
        renderer.drawLine(NUIPoint(playheadX, b.y + 1.0f),
                          NUIPoint(playheadX, b.y + b.height - 1.0f),
                          3.0f,
                          playheadDark);
        renderer.drawLine(NUIPoint(playheadX, b.y + 1.0f),
                          NUIPoint(playheadX, b.y + b.height - 1.0f),
                          1.0f,
                          playheadColor);
    }
    
    // Edge grips — rubberband handles, styled like the Track Manager minimap:
    // slim vertical bars just inside the viewport frame, vertically centred.
    const float gripW = 2.0f;
    const float gripH = std::min(10.0f, std::max(4.0f, viewRect.height - 4.0f));
    const float gripY = viewRect.y + (viewRect.height - gripH) * 0.5f;
    const auto gripColor = viewHandle;
    renderer.fillRoundedRect(NUIRect(viewRect.x + 2.0f, gripY, gripW, gripH), 1.0f, gripColor);
    renderer.fillRoundedRect(NUIRect(viewRect.right() - gripW - 2.0f, gripY, gripW, gripH), 1.0f, gripColor);

    // Bottom border to anchor the overview visually to the ruler beneath it.
    // Color-matched to the ruler tick marks (use the theme border with stronger alpha).
    renderer.drawLine(NUIPoint(b.x + 2.0f, b.bottom() - 1.0f),
                      NUIPoint(b.right() - 2.0f, b.bottom() - 1.0f),
                      1.0f,
                      theme.getColor("border").withAlpha(0.82f));
}

void PianoRollMinimap::updateHoverCursor(const NUIMouseEvent& event) {
    if (!m_platformBridge) {
        return;
    }
    if (isDragging_) {
        return; // keep the interaction cursor while dragging; next move re-resolves
    }
    if (!getBounds().contains(event.position)) {
        m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
        return;
    }

    const auto b = getBounds();
    const float localX = event.position.x - b.x;
    const float localY = event.position.y - b.y;
    const float x1 = beatToX(startBeat_);
    const float w = beatToX(viewDuration_);
    constexpr float kHandleW = 10.0f;
    const NUIRect leftGrip(x1 + 2.0f, 4.0f, kHandleW, b.height - 8.0f);
    const NUIRect rightGrip(x1 + w - kHandleW - 2.0f, 4.0f, kHandleW, b.height - 8.0f);
    const NUIRect bar(x1, 2.0f, w, b.height - 4.0f);
    const NUIPoint localPos(localX, localY);

    // Rubberband affordance: resize cursor on the edge grips, grab on the bar,
    // default elsewhere (Track Manager minimap behavior).
    if (leftGrip.contains(localPos) || rightGrip.contains(localPos)) {
        m_platformBridge->setCursorStyle(NUICursorStyle::ResizeEW);
    } else if (bar.contains(localPos)) {
        m_platformBridge->setCursorStyle(NUICursorStyle::Grab);
    } else {
        m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
    }
}

void PianoRollMinimap::onMouseEnter() {
    NUIComponent::onMouseEnter();
}

void PianoRollMinimap::onMouseLeave() {
    // Preserve the interaction cursor while a drag is active; only a parked
    // pointer yields to the default.
    if (m_platformBridge && !isDragging_) {
        m_platformBridge->setCursorStyle(NUICursorStyle::Arrow);
    }
    NUIComponent::onMouseLeave();
}

bool PianoRollMinimap::onMouseEvent(const NUIMouseEvent& event) {
    updateHoverCursor(event);
    if (!getBounds().contains(event.position) && !isDragging_) return false;

    auto b = getBounds();
    float localX = event.position.x - b.x;
    float localY = event.position.y - b.y;
    
    float x1 = beatToX(startBeat_);
    float w = beatToX(viewDuration_);
    float x2 = x1 + w;
    // Rubbery edge grips: generous hit area (~10px) so resizing the viewport
    // is easy to grab, mirroring the Track Manager minimap handles.
    const float handleW = 10.0f;
    const float handleH = std::max(8.0f, b.height - 8.0f);
    const NUIRect leftHandleRect(x1 + 2.0f, 4.0f, handleW, handleH);
    const NUIRect rightHandleRect(x1 + w - handleW - 2.0f, 4.0f, handleW, handleH);
    const NUIRect viewportRect(x1, 2.0f, w, b.height - 4.0f);
    const NUIPoint localPos(localX, localY);

    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Hit Test
        if (leftHandleRect.contains(localPos)) {
            isDragging_ = true;
            isResizingL_ = true;
            dragStartPos_ = event.position;
            dragStartStart_ = startBeat_;
            dragStartDuration_ = viewDuration_;
        } else if (rightHandleRect.contains(localPos)) {
            isDragging_ = true;
            isResizingR_ = true;
            dragStartPos_ = event.position;
            dragStartStart_ = startBeat_;
            dragStartDuration_ = viewDuration_;
        } else if (viewportRect.contains(localPos)) {
            isDragging_ = true;
            dragStartPos_ = event.position;
            dragStartStart_ = startBeat_;
            dragStartDuration_ = viewDuration_;
        } else {
             double beat = xToBeat(localX);
             startBeat_ = std::clamp(beat - viewDuration_ * 0.5, 0.0, std::max(0.0, totalDuration_ - viewDuration_));
             isDragging_ = false;
             isResizingL_ = false;
             isResizingR_ = false;
             if (onViewChanged) onViewChanged(startBeat_, viewDuration_);
             repaint();
             return true;
        }
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left) {
        isDragging_ = false;
        isResizingL_ = false;
        isResizingR_ = false;
        // Re-resolve the cursor for the release position now that the drag
        // flags are clear (updateHoverCursor skips while dragging).
        updateHoverCursor(event);
        return true;
    }
    else if (!event.pressed && isDragging_) {
        float dx = event.position.x - dragStartPos_.x;
        double db = dx / b.width * totalDuration_;
        
        if (isResizingL_) {
            double newStart = dragStartStart_ + db;
            double newDur = dragStartDuration_ - db;
            
            if (newDur < 0.25) { newStart -= (0.25 - newDur); newDur = 0.25; }
            
            viewDuration_ = std::clamp(newDur, 0.25, totalDuration_);
            startBeat_ = std::clamp(newStart, 0.0, std::max(0.0, totalDuration_ - viewDuration_));
        } 
        else if (isResizingR_) {
             double newDur = dragStartDuration_ + db;
             viewDuration_ = std::clamp(newDur, 0.25, totalDuration_);
             startBeat_ = std::clamp(startBeat_, 0.0, std::max(0.0, totalDuration_ - viewDuration_));
        }
        else {
             startBeat_ = std::clamp(dragStartStart_ + db, 0.0, std::max(0.0, totalDuration_ - viewDuration_));
        }
        
        if (onViewChanged) onViewChanged(startBeat_, viewDuration_);
        repaint();
        return true;
    }
    
    return NUIComponent::onMouseEvent(event);
}

} // namespace AestraUI
