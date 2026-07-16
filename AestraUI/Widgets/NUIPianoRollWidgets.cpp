// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPianoRollWidgets.h"
#include "../Platform/NUIPlatformBridge.h"
#include "../Common/MusicHelpers.h"
#include "../Helpers/PianoRollInteraction.h"
#include "../Helpers/TimelineGridRenderer.h"
#include "NUIDropdown.h"
#include "NUIButton.h"
#include "NUIContextMenu.h"
#include "NUIIcon.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <iomanip> // For string formatting
#include <random>  // Humanize velocity jitter

namespace AestraUI {

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================
static bool isBlackKey(int midiPitch) {
    int m = midiPitch % 12;
    return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);
}

static std::string getNoteLabel(int midiPitch) {
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (midiPitch / 12) - 2; // C3=60 standard
    return std::string(noteNames[midiPitch % 12]) + std::to_string(octave);
}

static float safeClampScroll(float value, float upper) {
    if (!std::isfinite(value) || !std::isfinite(upper) || upper <= 0.0f) {
        return 0.0f;
    }
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

static float safeClampRange(float value, float lower, float upper) {
    if (!std::isfinite(value) || !std::isfinite(lower) || !std::isfinite(upper)) {
        return std::max(0.0f, lower);
    }
    if (upper < lower) {
        upper = lower;
    }
    if (value <= lower) {
        return lower;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

static float beatToScreenX(double beat, float pixelsPerBeat, float scrollX, float originX) {
    return originX + static_cast<float>((beat * static_cast<double>(pixelsPerBeat)) - static_cast<double>(scrollX));
}

static float snapVerticalLineX(float x) {
    return std::floor(x) + 0.5f;
}

static float snapRectX(float x) {
    return std::round(x);
}

// =============================================================================
// MusicTheory Implementation
// =============================================================================
// MusicTheory moved to AestraUI/Common/MusicHelpers.cpp

// =============================================================================
// PianoRollKeyLane
// =============================================================================
PianoRollKeyLane::PianoRollKeyLane()
    : keyHeight_(24.0f), scrollY_(0.0f), hoveredKey_(-1), previewPitch_(-1), onPreviewNote_(nullptr), m_isPlayingCallback(nullptr)
{
}

void PianoRollKeyLane::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();

    renderer.setClipRect(b);

    const auto laneBg = themeManager.getColor("backgroundSecondary").darkened(0.02f);
    const auto naturalKey = themeManager.getColor("surfaceRaised").withAlpha(0.72f);
    const auto naturalHover = themeManager.getColor("surfaceRaised").lightened(0.08f).withAlpha(0.92f);
    const auto accidentalKey = themeManager.getColor("backgroundPrimary").darkened(0.08f);
    const auto accidentalHover = themeManager.getColor("accentPrimary").darkened(0.35f).withAlpha(0.92f);
    const auto separator = themeManager.getColor("border").withAlpha(0.34f);
    const auto rootAccent = themeManager.getColor("accentPrimary").withAlpha(0.86f);
    const auto naturalText = themeManager.getColor("textPrimary").withAlpha(0.72f);
    
    int startPitch = 127 - static_cast<int>((scrollY_) / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + b.height) / keyHeight_);
    
    // Expand range for safety margin (2 extra keys on each end)
    startPitch = std::clamp(startPitch + 2, 0, 127);
    endPitch = std::clamp(endPitch - 2, 0, 127);

    renderer.fillRect(b, laneBg);

    for (int p = startPitch; p >= endPitch; --p) {
        float worldY = (127 - p) * keyHeight_;
        float y = b.y + worldY - scrollY_;
        const bool accidental = isBlackKey(p);
        const bool hovered = p == hoveredKey_ || p == previewPitch_;
        const float rightPad = 3.0f;
        const float accidentalW = std::round(b.width * 0.68f);
        NUIRect keyRect(b.x + 3.0f,
                        y + 1.0f,
                        accidental ? accidentalW : b.width - 3.0f - rightPad,
                        std::max(4.0f, keyHeight_ - 2.0f));

        renderer.fillRoundedRect(keyRect,
                                 accidental ? 2.0f : 3.0f,
                                 hovered ? (accidental ? accidentalHover : naturalHover)
                                         : (accidental ? accidentalKey : naturalKey));
        renderer.strokeRoundedRect(keyRect, accidental ? 2.0f : 3.0f, 1.0f, separator);

        if (!accidental) {
            renderer.drawLine(NUIPoint(keyRect.x, keyRect.bottom()),
                              NUIPoint(keyRect.right(), keyRect.bottom()),
                              1.0f,
                              separator.withAlpha(0.52f));
        }

        if (keyHeight_ >= 15.0f && p % 12 == 0) {
            const std::string label = getNoteLabel(p);
            const float fontSize = 10.0f;
            const auto textSize = renderer.measureText(label, fontSize);
            const float textX = keyRect.right() - textSize.width - 8.0f;
            const float textY = keyRect.y + (keyRect.height - textSize.height) * 0.5f;
            renderer.drawText(label,
                              NUIPoint(textX, textY),
                              fontSize,
                              naturalText.lightened(0.12f));
        }

        if (p % 12 == 0) {
            renderer.fillRoundedRect(NUIRect(b.x + 3.0f, y + 4.0f, 2.0f, std::max(2.0f, keyHeight_ - 8.0f)),
                                     1.0f,
                                     rootAccent);
        }
    }

    renderer.drawLine(NUIPoint(b.right() - 0.5f, b.y),
                      NUIPoint(b.right() - 0.5f, b.bottom()),
                      1.0f,
                      separator.withAlpha(0.9f));

    renderer.clearClipRect();
}

bool PianoRollKeyLane::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position)) {
        if (hoveredKey_ != -1) {
            hoveredKey_ = -1;
            if (onHoveredKeyChanged_) onHoveredKeyChanged_(-1);
            repaint();
        }
        return false;
    }

    auto b = getBounds();
    float localY = event.position.y - b.y + scrollY_;
    int pitch = 127 - static_cast<int>(localY / keyHeight_);
    pitch = std::clamp(pitch, 0, 127);

    if (hoveredKey_ != pitch) {
        hoveredKey_ = pitch;
        if (onHoveredKeyChanged_) onHoveredKeyChanged_(pitch);
        repaint();
    }

    if (m_isPlayingCallback && m_isPlayingCallback()) {
        return NUIComponent::onMouseEvent(event);
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        previewPitch_ = pitch;
        if (onPreviewNote_) {
            onPreviewNote_(pitch, 100);
        }
    }
    else if (event.released && event.button == NUIMouseButton::Left && previewPitch_ != -1) {
        if (onPreviewNote_) {
            onPreviewNote_(previewPitch_, 0);
        }
        previewPitch_ = -1;
    }
    
    return NUIComponent::onMouseEvent(event);
}

void PianoRollKeyLane::setKeyHeight(float height) {
    keyHeight_ = std::max(8.0f, height);
    repaint();
}

void PianoRollKeyLane::setScrollOffsetY(float offset) {
    scrollY_ = offset;
    repaint();
}

void PianoRollKeyLane::setOnPreviewNote(std::function<void(int pitch, int velocity)> cb) {
    onPreviewNote_ = std::move(cb);
}

// =============================================================================
// PianoRollMinimap
// =============================================================================
PianoRollMinimap::PianoRollMinimap() 
    : startBeat_(0.0), viewDuration_(1.0), totalDuration_(100.0)
{
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
    
    const auto panelBg = theme.getColor("backgroundPrimary").darkened(0.03f);
    const auto border = theme.getColor("border").withAlpha(0.48f);
    renderer.fillRoundedRect(NUIRect(b.x + 4.0f, b.y + 3.0f, b.width - 8.0f, b.height - 6.0f), 5.0f, panelBg);
    renderer.strokeRoundedRect(NUIRect(b.x + 4.0f, b.y + 3.0f, b.width - 8.0f, b.height - 6.0f), 5.0f, 1.0f, border);
    
    // View Rect
    float x1 = b.x + beatToX(startBeat_);
    float w = beatToX(viewDuration_);
    NUIRect viewRect(x1, b.y + 2, w, b.height - 4);
    
    auto thumbCol = theme.getColor("accentPrimary").withAlpha(0.13f);
    auto borderCol = theme.getColor("accentPrimary").withAlpha(0.72f);
    
    renderer.fillRoundedRect(viewRect, 5.0f, thumbCol);
    renderer.strokeRoundedRect(viewRect, 5.0f, 1.0f, borderCol);

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

    const auto playheadColor = NUIThemeManager::getInstance().getColor("accentPrimary");
    const float playheadX = b.x + beatToX(playheadBeat_);
    if (playheadX >= b.x && playheadX <= b.x + b.width) {
        renderer.drawLine(NUIPoint(playheadX, b.y + 1.0f),
                          NUIPoint(playheadX, b.y + b.height - 1.0f),
                          2.0f,
                          playheadColor.withAlpha(0.9f));
    }
    
    // Handles (Visual only, logic in mouse)
    const float handleW = 4.0f;
    const float handleH = std::max(8.0f, b.height - 8.0f);
    const auto handleFill = borderCol.withAlpha(0.5f);
    NUIRect leftHandle(x1 + 2.0f, b.y + 4.0f, handleW, handleH);
    NUIRect rightHandle(x1 + w - 6.0f, b.y + 4.0f, handleW, handleH);
    renderer.fillRoundedRect(leftHandle, 2.0f, handleFill);
    renderer.fillRoundedRect(rightHandle, 2.0f, handleFill);
}

bool PianoRollMinimap::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position) && !isDragging_) return false;

    auto b = getBounds();
    float localX = event.position.x - b.x;
    float localY = event.position.y - b.y;
    
    float x1 = beatToX(startBeat_);
    float w = beatToX(viewDuration_);
    float x2 = x1 + w;
    const float handleW = 8.0f;
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


// =============================================================================
// PianoRollRuler
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

    const auto rulerBackground = NUIColor(0.012f, 0.012f, 0.012f, 1.0f);
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
        const auto accent = theme.getColor("accentPrimary");
        const float markerRadius = isScrubbing_ ? 6.5f : 6.0f;
        const NUIPoint markerCenter(playheadX, bounds.bottom() - markerRadius);
        renderer.fillCircle(markerCenter,
                            markerRadius + 2.0f,
                            theme.getColor("backgroundPrimary").withAlpha(0.94f));
        renderer.fillCircle(markerCenter, markerRadius, accent.withAlpha(isScrubbing_ ? 0.34f : 0.20f));
        renderer.strokeCircle(markerCenter, markerRadius, 1.3f, accent.withAlpha(0.98f));
        renderer.fillCircle(markerCenter, 1.7f, accent);
        renderer.drawLine(NUIPoint(playheadX, markerCenter.y + markerRadius),
                          NUIPoint(playheadX, bounds.bottom()),
                          1.0f,
                          accent.withAlpha(0.88f));
    }

    renderer.clearClipRect();
}
void PianoRollRuler::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollRuler::setScrollX(float scrollX) { scrollX_ = scrollX; repaint(); }



// =============================================================================
// PianoRollToolbar
// =============================================================================
PianoRollToolbar::PianoRollToolbar() {
    setupUI();
}

void PianoRollToolbar::closeActiveContextMenu() {
    if (!m_activeContextMenu) {
        return;
    }

    if (auto* parent = m_activeContextMenu->getParent()) {
        parent->removeChild(m_activeContextMenu);
    } else {
        removeChild(m_activeContextMenu);
    }

    m_activeContextMenu = nullptr;
}

void PianoRollToolbar::setupUI() {
    // 0. Menu Button (Scale, Snap, etc.)
    m_menuBtn = std::make_shared<NUIButton>("");
    m_menuBtn->setTooltip("Scale, Snap & Settings");
    
    const char* menuSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 18h18v-2H3v2zm0-5h18v-2H3v2zm0-7v2h18V6H3z"/></svg>)";
    m_menuIcon = std::make_shared<NUIIcon>(menuSvg);
    
    m_menuBtn->setOnClick([this]() {
        if (m_activeContextMenu) {
            if (!m_activeContextMenu->isVisible()) {
                closeActiveContextMenu();
            } else {
                closeActiveContextMenu();
                return;
            }
        }

        auto menu = std::make_shared<NUIContextMenu>();
        m_activeContextMenu = std::static_pointer_cast<NUIComponent>(menu);
        
        // --- SNAP SUBMENU ---
        auto snapMenu = std::make_shared<NUIContextMenu>();
        auto snaps = MusicTheory::getSnapOptions();
        for (auto snap : snaps) {
            snapMenu->addItem(MusicTheory::getSnapName(snap), [this, snap]() {
                m_currentSnap = snap;
                if (auto g = grid_.lock()) g->setSnap(snap);
                if (auto n = notes_.lock()) n->setSnap(snap);
                repaint();
            });
        }
        menu->addSubmenu("Snap", snapMenu);
        
        // --- ROOT KEY SUBMENU ---
        auto rootMenu = std::make_shared<NUIContextMenu>();
        auto roots = MusicTheory::getRootNames();
        for (size_t i = 0; i < roots.size(); ++i) {
            rootMenu->addItem(roots[i], [this, i]() {
                if (auto g = grid_.lock()) g->setRootKey(static_cast<int>(i));
                if (auto n = notes_.lock()) n->setRootKey(static_cast<int>(i));
            });
        }
        menu->addSubmenu("Root Key", rootMenu);
        
        // --- SCALE SUBMENU ---
        auto scaleMenu = std::make_shared<NUIContextMenu>();
        auto scales = MusicTheory::getScales();
        for (size_t i = 0; i < scales.size(); ++i) {
            scaleMenu->addItem(scales[i].name, [this, i]() {
                if (auto g = grid_.lock()) g->setScaleType(static_cast<ScaleType>(i));
                if (auto n = notes_.lock()) n->setScaleType(static_cast<ScaleType>(i));
            });
        }
        menu->addSubmenu("Scale Type", scaleMenu);

        // --- SNAP TO SCALE TOGGLE ---
        bool snapToScaleActive = false;
        if (auto n = notes_.lock()) {
            snapToScaleActive = n->getSnapToScale();
        }
        menu->addCheckbox("Snap to Scale", snapToScaleActive, [this](bool) {
            if (auto n = notes_.lock()) {
                n->setSnapToScale(!n->getSnapToScale());
            }
        });

        // --- CHORD MODE TOGGLE (pencil stamps a diatonic triad) ---
        bool chordModeActive = false;
        if (auto n = notes_.lock()) {
            chordModeActive = n->getChordMode();
        }
        menu->addCheckbox("Chord (Triad)", chordModeActive, [this](bool) {
            if (auto n = notes_.lock()) {
                n->setChordMode(!n->getChordMode());
            }
        });

        // --- STRUM SUBMENU (staggers the selected chord low → high) ---
        auto strumMenu = std::make_shared<NUIContextMenu>();
        const struct { const char* name; double beats; } kStrumSpreads[] = {
            {"Tight (1/64)", 0.0625}, {"1/32", 0.125}, {"1/16", 0.25}, {"Wide (1/8)", 0.5},
        };
        for (const auto& spread : kStrumSpreads) {
            const double beats = spread.beats;
            strumMenu->addItem(spread.name, [this, beats]() {
                if (auto n = notes_.lock()) n->strumSelectedNotes(beats);
            });
        }
        menu->addSubmenu("Strum Selection", strumMenu);

        // --- HUMANIZE ---
        menu->addItem("Humanize Velocity", [this]() {
            if (auto n = notes_.lock()) n->humanizeSelectedVelocities();
        });

        // --- SHORTCUT CHEAT SHEET ---
        menu->addItem("Keyboard Shortcuts", [this]() {
            if (onShowShortcutHelp_) onShowShortcutHelp_();
        });

        auto b = m_menuBtn->getBounds();
        if (auto* parent = getParent()) {
            parent->addChild(m_activeContextMenu);
            const auto pb = parent->getBounds();
            menu->showAt(static_cast<int>(b.x - pb.x),
                         static_cast<int>(b.y - pb.y + b.height + 2.0f));
        } else {
            addChild(m_activeContextMenu);
            menu->showAt(static_cast<int>(b.x), static_cast<int>(b.y + b.height + 2.0f));
        }
    });

    // 1. Tool Buttons
    m_ptrBtn = std::make_shared<NUIButton>("");
    m_ptrBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pointer); });

    m_pencilBtn = std::make_shared<NUIButton>("");
    m_pencilBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pencil); });

    m_eraserBtn = std::make_shared<NUIButton>("");
    m_eraserBtn->setOnClick([this](){ setActiveTool(GlobalTool::Eraser); });

    m_patternDropdown = std::make_shared<NUIDropdown>();
    m_patternDropdown->setPlaceholderText("Pattern");
    m_patternDropdown->setMaxVisibleItems(8);
    m_patternDropdown->setItemHeight(24.0f);
    m_patternDropdown->setOnSelectionChanged([this](int, int value, const std::string&) {
        if (!m_updatingPatternDropdown && onPatternChoiceSelected_) {
            onPatternChoiceSelected_(value);
        }
    });

    m_lengthDownBtn = std::make_shared<NUIButton>("");
    m_lengthDownBtn->setTooltip("Shorten Pattern By 2 Bars");
    m_lengthDownBtn->setOnClick([this]() {
        if (onAdjustPatternLength_) onAdjustPatternLength_(-2);
    });

    m_lengthUpBtn = std::make_shared<NUIButton>("");
    m_lengthUpBtn->setTooltip("Extend Pattern By 2 Bars");
    m_lengthUpBtn->setOnClick([this]() {
        if (onAdjustPatternLength_) onAdjustPatternLength_(2);
    });
    
    // Icons
    const char* ptrSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path fill-rule="evenodd" d="M5.4 2.5v15.9l4.2-3.9 3.1 6.7 2.6-1.2-3.1-6.5 5.7-.6L5.4 2.5zm2.1 4.4 5.6 4.6-3.9.4 3 6.4-.7.3-3-6.5-1 1V6.9z"/></svg>)";
    m_ptrIcon = std::make_shared<NUIIcon>(ptrSvg);
    
    const char* penSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>)";
    m_pencilIcon = std::make_shared<NUIIcon>(penSvg);
    
    const char* eraserSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M15.14 3c-.51 0-1.02.2-1.41.59L2.59 14.73c-.78.78-.78 2.05 0 2.83L5.43 20.39c.39.39.9.59 1.41.59.51 0 1.02-.2 1.41-.59l10.96-10.96c.78-.78.78-2.05 0-2.83l-2.66-2.67c-.39-.39-.9-.59-1.41-.59zM9 16l-3.37-3.37L15.14 3.1 19 6.9 9 16z"/></svg>)";
    m_eraserIcon = std::make_shared<NUIIcon>(eraserSvg);

    const char* minusSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M5 11h14v2H5z"/></svg>)";
    const char* plusSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M11 5h2v14h-2zM5 11h14v2H5z"/></svg>)";
    m_lengthDownIcon = std::make_shared<NUIIcon>(minusSvg);
    m_lengthUpIcon = std::make_shared<NUIIcon>(plusSvg);

    addChild(m_menuBtn);
    addChild(m_ptrBtn);
    addChild(m_pencilBtn);
    addChild(m_eraserBtn);
    addChild(m_patternDropdown);
    addChild(m_lengthDownBtn);
    addChild(m_lengthUpBtn);
}

void PianoRollToolbar::setPatternName(const std::string& name) {
    m_patternName = name;
    repaint();
}

void PianoRollToolbar::setPatternLengthBeats(double beats) {
    m_patternLengthBeats = std::max(8.0, beats);
    repaint();
}

void PianoRollToolbar::setPatternChoices(const std::vector<PatternChoice>& choices, int selectedValue) {
    if (!m_patternDropdown) {
        return;
    }

    m_updatingPatternDropdown = true;
    m_patternDropdown->clearItems();
    for (const auto& choice : choices) {
        m_patternDropdown->addItem(choice.label, choice.value);
    }
    m_patternDropdown->setSelectedByValue(selectedValue);
    m_updatingPatternDropdown = false;
    repaint();
}


void PianoRollToolbar::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();

    const auto toolbarBg = themeManager.getColor("backgroundSecondary").darkened(0.015f);
    const auto groupBg = themeManager.getColor("backgroundPrimary").withAlpha(0.72f);
    const auto groupBorder = themeManager.getColor("border").withAlpha(0.52f);
    renderer.fillRect(b, toolbarBg);
    renderer.drawLine(NUIPoint(b.x, b.bottom() - 0.5f),
                      NUIPoint(b.right(), b.bottom() - 0.5f),
                      1.0f,
                      groupBorder);

    // Let child buttons keep input/hit-testing, but draw our custom toolbar chrome and glyphs after them.
    renderChildren(renderer);

    const float buttonSize = 30.0f;
    const float buttonSpacing = 4.0f;
    const float innerPad = 10.0f;
    const float radius = 6.0f;

    float currentX = b.x + innerPad;
    float currentY = b.y + (b.height - buttonSize) * 0.5f;

    auto idleBg = themeManager.getColor("surfaceSecondary").withAlpha(0.76f);
    auto hoverBg = themeManager.getColor("buttonBgHover").withAlpha(0.98f);
    auto activeBg = themeManager.getColor("accentPrimary").withAlpha(0.84f);
    auto borderCol = themeManager.getColor("border").withAlpha(0.42f);
    auto iconIdle = themeManager.getColor("textPrimary").withAlpha(0.78f);
    auto iconActive = themeManager.getColor("textPrimary");

    auto drawGroup = [&](float x, float width) {
        NUIRect groupRect(x, currentY - 3.0f, width, buttonSize + 6.0f);
        renderer.fillRoundedRect(groupRect, 8.0f, groupBg);
        renderer.strokeRoundedRect(groupRect, 8.0f, 1.0f, groupBorder);
    };

    auto renderButton = [&](std::shared_ptr<NUIButton> btn, std::shared_ptr<NUIIcon> icon, bool isActive) {
        btn->setBounds(NUIRect(currentX, currentY, buttonSize, buttonSize));
        btn->setText("");

        AestraUI::NUIColor bg = idleBg;
        AestraUI::NUIColor border = borderCol;

        if (isActive) {
            bg = activeBg;
            border = themeManager.getColor("accentPrimary").lightened(0.18f).withAlpha(0.88f);
        } else if (btn->isHovered()) {
            bg = hoverBg;
        }

        renderer.fillRoundedRect(btn->getBounds(), radius, bg);
        renderer.strokeRoundedRect(btn->getBounds(), radius, 1.0f, border);

        if (icon) {
            const float iconSz = 15.0f;
            NUIRect iconRect(
                std::round(currentX + (buttonSize - iconSz) * 0.5f),
                std::round(currentY + (buttonSize - iconSz) * 0.5f),
                iconSz, iconSz
            );
            icon->setBounds(iconRect);
            icon->setColor(isActive ? iconActive : iconIdle);
            icon->onRender(renderer);
        }
        
        currentX += buttonSize + buttonSpacing;
    };

    drawGroup(currentX - 3.0f, buttonSize + 6.0f);
    renderButton(m_menuBtn, m_menuIcon, false);

    currentX += 8.0f;

    drawGroup(currentX - 3.0f, buttonSize * 3.0f + buttonSpacing * 2.0f + 6.0f);
    renderButton(m_ptrBtn, m_ptrIcon, activeTool_ == GlobalTool::Pointer);
    renderButton(m_pencilBtn, m_pencilIcon, activeTool_ == GlobalTool::Pencil);
    renderButton(m_eraserBtn, m_eraserIcon, activeTool_ == GlobalTool::Eraser);

    float patternDropdownRight = currentX;
    if (m_patternDropdown) {
        currentX += 8.0f;

        const float dropdownW = std::clamp(b.width * 0.24f, 180.0f, 250.0f);
        drawGroup(currentX - 3.0f, dropdownW + 6.0f);
        m_patternDropdown->setBounds(NUIRect(currentX, currentY, dropdownW, buttonSize));
        m_patternDropdown->onRender(renderer);
        patternDropdownRight = currentX + dropdownW;
        currentX += dropdownW + buttonSpacing;
    }

    currentX += 8.0f;

    const int patternBars = std::max(1, static_cast<int>(std::lround(m_patternLengthBeats / 4.0)));
    const std::string lengthLabel = std::to_string(patternBars) + " Bars";
    const float lengthFontSize = 10.0f;
    const auto lengthSize = renderer.measureText(lengthLabel, lengthFontSize);
    const float pillPadX = 12.0f;
    const float pillW = std::max(64.0f, lengthSize.width + pillPadX * 2.0f);
    const float lengthGroupX = currentX - 3.0f;
    const float lengthGroupW = buttonSize * 2.0f + pillW + buttonSpacing * 2.0f + 6.0f;
    drawGroup(lengthGroupX, lengthGroupW);

    renderButton(m_lengthDownBtn, m_lengthDownIcon, false);
    const NUIRect pillRect(currentX, currentY, pillW, buttonSize);
    renderer.fillRoundedRect(pillRect, radius, themeManager.getColor("surfaceRaised").withAlpha(0.80f));
    renderer.strokeRoundedRect(pillRect, radius, 1.0f, borderCol);
    renderer.drawText(lengthLabel,
                      NUIPoint(pillRect.x + (pillRect.width - lengthSize.width) * 0.5f,
                               pillRect.y + (pillRect.height - lengthSize.height) * 0.5f + 1.0f),
                      lengthFontSize,
                      themeManager.getColor("textPrimary").withAlpha(0.82f));
    currentX += pillW + buttonSpacing;

    renderButton(m_lengthUpBtn, m_lengthUpIcon, false);

    currentX += 8.0f;
    const std::string snapLabel = "SNAP  " + MusicTheory::getSnapName(m_currentSnap);
    const float snapFontSize = 9.0f;
    const auto snapTextSize = renderer.measureText(snapLabel, snapFontSize);
    const float snapWidth = std::max(78.0f, snapTextSize.width + 18.0f);
    const NUIRect snapRect(currentX, currentY, snapWidth, buttonSize);
    renderer.fillRoundedRect(snapRect, radius, groupBg);
    renderer.strokeRoundedRect(snapRect, radius, 1.0f, groupBorder);
    renderer.drawText(snapLabel,
                      NUIPoint(snapRect.x + (snapRect.width - snapTextSize.width) * 0.5f,
                               snapRect.y + (snapRect.height - snapTextSize.height) * 0.5f + 1.0f),
                      snapFontSize,
                      themeManager.getColor("textSecondary").withAlpha(0.76f));
    currentX += snapWidth;

    // Editing Pattern Label (Right Side)
    if (!m_patternName.empty()) {
        const float labelLeft = std::max(patternDropdownRight + 10.0f, currentX + 12.0f);
        const float labelRight = b.right() - innerPad - 4.0f;
        const float maxLabelWidth = labelRight - labelLeft;
        if (maxLabelWidth < 24.0f) {
            return;
        }

        std::string labelStr = m_patternName;
        float fontSize = 10.0f;
        auto measured = renderer.measureText(labelStr, fontSize);
        while (measured.width > maxLabelWidth && labelStr.length() > 3) {
            labelStr.pop_back();
            measured = renderer.measureText(labelStr + "...", fontSize);
        }
        if (measured.width > maxLabelWidth) {
            labelStr = "...";
        } else if (labelStr.length() < m_patternName.length()) {
            labelStr += "...";
        }
        auto size = renderer.measureText(labelStr, fontSize);
        float lx = std::max(labelLeft, labelRight - size.width);
        renderer.drawText(labelStr,
                          NUIPoint(lx, currentY + (buttonSize - size.height) * 0.5f + 2.0f),
                          fontSize,
                          themeManager.getColor("textSecondary").withAlpha(0.68f));
    }

}

void PianoRollToolbar::setGrid(std::shared_ptr<PianoRollGrid> grid) {
    grid_ = grid;
    if (grid) {
        // Sync initial default state if needed
    }
}

void PianoRollToolbar::setNoteLayer(std::shared_ptr<PianoRollNoteLayer> notes) {
    notes_ = notes;
    if (notes) {
        notes->setTool(activeTool_);
    }
}

void PianoRollToolbar::setActiveTool(GlobalTool tool) {
    activeTool_ = tool;
    if (auto n = notes_.lock()) n->setTool(tool);
    repaint();
}


bool PianoRollToolbar::onMouseEvent(const NUIMouseEvent& event) {
    if (m_menuBtn->onMouseEvent(event)) return true;
    if (m_ptrBtn->onMouseEvent(event)) return true;
    if (m_pencilBtn->onMouseEvent(event)) return true;
    if (m_eraserBtn->onMouseEvent(event)) return true;
    if (m_lengthDownBtn->onMouseEvent(event)) return true;
    if (m_lengthUpBtn->onMouseEvent(event)) return true;
    
    return false;
}

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
    renderer.fillRect(bounds, NUIColor::black());

    const auto accidentalRow = NUIColor::white().withAlpha(0.022f);
    const auto rootRow = theme.getColor("accentPrimary").withAlpha(0.055f);
    const auto outOfScaleRow = NUIColor::black().withAlpha(0.24f);
    const auto rowLine = NUIColor::white().withAlpha(0.045f);

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

    renderTimelineGrid(
        renderer, bounds, bounds.x, bounds.right(), scrollX_, pixelsPerBeat_, beatsPerBar_);

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
    if (patternEndX < bounds.right()) {
        const float shadeX = std::max(bounds.x, patternEndX);
        renderer.fillRect(NUIRect(shadeX, bounds.y, bounds.right() - shadeX, bounds.height),
                          NUIColor::black().withAlpha(0.58f));
    }
    if (patternEndX >= bounds.x && patternEndX <= bounds.right()) {
        renderer.drawLine(NUIPoint(patternEndX, bounds.y),
                          NUIPoint(patternEndX, bounds.bottom()),
                          2.0f,
                          theme.getColor("accentPrimary").withAlpha(0.58f));
    }

    renderer.clearClipRect();
}
void PianoRollGrid::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollGrid::setKeyHeight(float height) { keyHeight_ = std::max(8.0f, height); repaint(); }
void PianoRollGrid::setScrollOffsetX(float offset) { scrollX_ = offset; repaint(); }
void PianoRollGrid::setScrollOffsetY(float offset) { scrollY_ = offset; repaint(); }

// =============================================================================
// PianoRollNoteLayer
// =============================================================================
PianoRollNoteLayer::PianoRollNoteLayer()
    : pixelsPerBeat_(80.0f), keyHeight_(24.0f), scrollX_(0.0f), scrollY_(0.0f)
{
}

double PianoRollNoteLayer::snapToGrid(double beat) {
    if (fineDrag_) return beat; // Alt held mid-drag: free positioning
    if (snap_ == SnapGrid::None) return beat;
    double grid = MusicTheory::getSnapDuration(snap_);
    if (grid <= 0.00001) return beat;
    return std::round(beat / grid) * grid;
}

int PianoRollNoteLayer::snapPitchToScale(int pitch) {
    if (!snapToScale_ || scaleType_ == ScaleType::Chromatic) return pitch;
    if (MusicTheory::isNoteInScale(pitch, rootKey_, scaleType_)) return pitch;
    int bestPitch = pitch;
    int bestDist = 128;
    for (int candidate = pitch - 6; candidate <= pitch + 6; ++candidate) {
        if (candidate < 0 || candidate > 127) continue;
        if (MusicTheory::isNoteInScale(candidate, rootKey_, scaleType_)) {
            int dist = std::abs(candidate - pitch);
            if (dist < bestDist) {
                bestDist = dist;
                bestPitch = candidate;
            }
        }
    }
    return bestPitch;
}

void PianoRollNoteLayer::auditionPitch(int pitch) {
    // Never talk over the transport — playback owns the audio focus. Clear the
    // sounding pitch while suppressed so the very next idle placement re-fires
    // (otherwise the same-pitch guard below would swallow it after playback stops).
    if (isPlayingCallback_ && isPlayingCallback_()) {
        auditionPitch_ = -1;
        return;
    }
    pitch = std::clamp(pitch, 0, 127);
    // The audition path is a one-shot voice that auto-releases (~125 ms), so a
    // fresh press must always fire. auditionStop() resets the guard on release;
    // the guard's only job is to avoid machine-gunning one pitch while a drag
    // jitters within the same row.
    if (pitch == auditionPitch_) return;
    if (onPreviewNote_) {
        onPreviewNote_(pitch, static_cast<int>(std::lround(lastNoteVelocity_ * 127.0f)));
    }
    auditionPitch_ = pitch;
}

void PianoRollNoteLayer::auditionStop() {
    // The one-shot voice releases itself; just clear the guard so the next
    // placement or pitch-drag can audition again, even on the same pitch.
    auditionPitch_ = -1;
}

std::vector<int> PianoRollNoteLayer::buildTriad(int rootPitch) const {
    rootPitch = std::clamp(rootPitch, 0, 127);
    // No scale context → a plain major triad is the sensible default.
    if (scaleType_ == ScaleType::Chromatic) {
        std::vector<int> chord = {rootPitch};
        if (rootPitch + 4 <= 127) chord.push_back(rootPitch + 4);
        if (rootPitch + 7 <= 127) chord.push_back(rootPitch + 7);
        return chord;
    }

    // Diatonic triad = root + the 2nd and 4th scale tones above it (the third and
    // fifth), so the chord quality follows the degree the root sits on.
    std::vector<int> chord = {rootPitch};
    const int wantDegrees[] = {2, 4};
    int found = 0;
    int scaleStepsUp = 0;
    for (int p = rootPitch + 1; p <= 127 && found < 2; ++p) {
        if (!MusicTheory::isNoteInScale(p, rootKey_, scaleType_)) continue;
        ++scaleStepsUp;
        if (scaleStepsUp == wantDegrees[found]) {
            chord.push_back(p);
            ++found;
        }
    }
    return chord;
}

bool PianoRollNoteLayer::paintBrushAt(float localX, float localY) {
    const double snappedBeat = snapToGrid(std::max(0.0, static_cast<double>(localX) / pixelsPerBeat_));
    const int pitch = snapPitchToScale(std::clamp(127 - static_cast<int>(localY / keyHeight_), 0, 127));

    // One note per cell: skip if a note already starts on this pitch+beat so a
    // jittery drag doesn't stack duplicates.
    for (const auto& n : notes_) {
        if (n.isDeleted) continue;
        if (n.pitch == pitch && std::abs(n.startBeat - snappedBeat) < 0.001) return false;
    }

    MidiNote note;
    note.pitch = pitch;
    note.startBeat = snappedBeat;
    note.durationBeats = lastNoteDuration_;
    note.velocity = lastNoteVelocity_;
    note.unitId = defaultUnitId_;
    note.selected = true; // the whole stroke ends up selected
    note.animationScale = 1.0f;
    notes_.push_back(note);
    auditionPitch(pitch);
    return true;
}

void PianoRollNoteLayer::connectSelectedNotes() {
    std::vector<int> selected;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].selected && !notes_[i].isDeleted) selected.push_back(static_cast<int>(i));
    }
    if (selected.empty()) return;

    // Grid used for the "nothing follows" fallback.
    double snapDur = MusicTheory::getSnapDuration(snap_);
    if (snap_ == SnapGrid::None || snapDur <= 0.0001) snapDur = 1.0; // fall back to one beat

    // Candidate starts to connect against (own start is excluded by the helper's
    // strict "> start" test, so passing all of them is fine).
    std::vector<double> starts;
    starts.reserve(notes_.size());
    for (const auto& n : notes_) {
        if (!n.isDeleted) starts.push_back(n.startBeat);
    }

    auto oldNotes = notes_;
    bool changed = false;
    for (int idx : selected) {
        const double start = notes_[idx].startBeat;
        const double end = start + notes_[idx].durationBeats;
        const double newEnd = computeConnectedNoteEnd(start, end, starts, snapDur);
        if (newEnd > end + 0.0001) {
            notes_[idx].durationBeats = newEnd - start;
            changed = true;
        }
    }

    if (changed) {
        pushUndo("Connect", oldNotes, notes_);
        commitNotes();
        repaint();
    }
}

void PianoRollNoteLayer::quantizeSelectedNotes() {
    double snapDur = MusicTheory::getSnapDuration(snap_);
    if (snap_ == SnapGrid::None || snapDur <= 0.0001) snapDur = 0.25; // sensible 1/16 default

    auto oldNotes = notes_;
    bool changed = false;
    for (auto& n : notes_) {
        if (!n.selected || n.isDeleted) continue;
        const double q = quantizeBeatToGrid(n.startBeat, snapDur);
        if (std::abs(q - n.startBeat) > 0.0001) {
            n.startBeat = q;
            changed = true;
        }
    }
    if (changed) {
        pushUndo("Quantize", oldNotes, notes_);
        commitNotes();
        repaint();
    }
}

void PianoRollNoteLayer::glueSelectedNotes() {
    // Collect selected note indices grouped by pitch.
    std::vector<int> selected;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].selected && !notes_[i].isDeleted) selected.push_back(static_cast<int>(i));
    }
    if (selected.size() < 2) return;

    auto oldNotes = notes_;
    std::vector<MidiNote> merged;      // rebuilt selected notes
    std::vector<bool> consumed(notes_.size(), false);

    // For each pitch, sweep left→right and coalesce runs that overlap or touch.
    std::sort(selected.begin(), selected.end(), [&](int a, int b) {
        if (notes_[a].pitch != notes_[b].pitch) return notes_[a].pitch < notes_[b].pitch;
        return notes_[a].startBeat < notes_[b].startBeat;
    });

    bool changed = false;
    size_t k = 0;
    while (k < selected.size()) {
        MidiNote run = notes_[selected[k]];
        double runEnd = run.startBeat + run.durationBeats;
        size_t j = k + 1;
        while (j < selected.size() && notes_[selected[j]].pitch == run.pitch &&
               notes_[selected[j]].startBeat <= runEnd + 0.0001) {
            runEnd = std::max(runEnd, notes_[selected[j]].startBeat + notes_[selected[j]].durationBeats);
            changed = true; // at least two notes folded together
            ++j;
        }
        run.durationBeats = runEnd - run.startBeat;
        run.selected = true;
        merged.push_back(run);
        for (size_t m = k; m < j; ++m) consumed[selected[m]] = true;
        k = j;
    }

    if (!changed) return;

    // Keep everything that wasn't glued, then append the merged notes.
    std::vector<MidiNote> rebuilt;
    rebuilt.reserve(notes_.size());
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (!consumed[i]) rebuilt.push_back(notes_[i]);
    }
    for (auto& m : merged) rebuilt.push_back(m);
    notes_ = std::move(rebuilt);

    pushUndo("Glue", oldNotes, notes_);
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::strumSelectedNotes(double spreadBeats) {
    std::vector<int> selected;
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].selected && !notes_[i].isDeleted) selected.push_back(static_cast<int>(i));
    }
    if (selected.size() < 2) return;

    // Anchor at the earliest selected start so the strum begins where the chord
    // sits, then cascade low pitch → high pitch.
    double baseStart = std::numeric_limits<double>::max();
    for (int i : selected) baseStart = std::min(baseStart, notes_[i].startBeat);
    std::sort(selected.begin(), selected.end(),
              [&](int a, int b) { return notes_[a].pitch < notes_[b].pitch; });

    auto oldNotes = notes_;
    for (size_t k = 0; k < selected.size(); ++k) {
        notes_[selected[k]].startBeat = std::max(0.0, baseStart + static_cast<double>(k) * spreadBeats);
    }
    pushUndo("Strum", oldNotes, notes_);
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::humanizeSelectedVelocities() {
    // A gentle ±10 MIDI steps of jitter — enough to break the machine-gun
    // feel of identical velocities without mangling drawn dynamics.
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.08f, 0.08f);

    auto oldNotes = notes_;
    bool changed = false;
    for (auto& n : notes_) {
        if (n.selected && !n.isDeleted) {
            n.velocity = std::clamp(n.velocity + jitter(rng), 0.05f, 1.0f);
            changed = true;
        }
    }
    if (!changed) return;
    pushUndo("Humanize", oldNotes, notes_);
    commitNotes();
    repaint();
}

// --- Note Properties popup -------------------------------------------------
// Fixed layout shared by the renderer and the hit tests below.
namespace {
constexpr float kPropW = 208.0f;
constexpr float kPropPad = 10.0f;
constexpr float kPropTitleH = 24.0f;
constexpr float kPropRowH = 22.0f;
constexpr int kPropRowCount = 5; // Pitch, Velocity, Pan, Start, Length
constexpr float kPropFooterH = 28.0f;
constexpr float kPropH =
    kPropPad + kPropTitleH + kPropRowCount * kPropRowH + 8.0f + kPropFooterH + kPropPad;
} // namespace

void PianoRollNoteLayer::openNoteProperties(int noteIndex) {
    if (noteIndex < 0 || noteIndex >= static_cast<int>(notes_.size()) ||
        notes_[noteIndex].isDeleted) {
        return;
    }
    propNoteIndex_ = noteIndex;
    propUndoSnapshot_ = notes_;
    propOriginalNote_ = notes_[noteIndex];
    propDragField_ = -1;

    // Sit beside the note, flipped/clamped to stay inside the layer.
    const auto b = getBounds();
    const auto& n = notes_[noteIndex];
    const float nx = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
    const float ny = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
    float px = nx + 26.0f;
    float py = ny - kPropH - 8.0f;
    if (py < b.y + 4.0f) py = ny + keyHeight_ + 8.0f;
    px = std::clamp(px, b.x + 4.0f, std::max(b.x + 4.0f, b.right() - kPropW - 4.0f));
    py = std::clamp(py, b.y + 4.0f, std::max(b.y + 4.0f, b.bottom() - kPropH - 4.0f));
    propPanelRect_ = NUIRect(px, py, kPropW, kPropH);
    repaint();
}

void PianoRollNoteLayer::closeNoteProperties(bool accept) {
    if (propNoteIndex_ < 0) return;
    if (!accept) {
        notes_ = propUndoSnapshot_;
    } else if (propNoteIndex_ < static_cast<int>(notes_.size())) {
        const auto& n = notes_[propNoteIndex_];
        const auto& o = propOriginalNote_;
        const bool changed = n.pitch != o.pitch ||
                             std::abs(n.startBeat - o.startBeat) > 1e-9 ||
                             std::abs(n.durationBeats - o.durationBeats) > 1e-9 ||
                             std::abs(n.velocity - o.velocity) > 1e-6f ||
                             std::abs(n.pan - o.pan) > 1e-6f;
        if (changed) {
            pushUndo("Note Properties", propUndoSnapshot_, notes_);
        }
    }
    propNoteIndex_ = -1;
    propDragField_ = -1;
    propUndoSnapshot_.clear();
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::applyNotePropertyDelta(int field, float dy, bool coarseStep) {
    if (propNoteIndex_ < 0 || propNoteIndex_ >= static_cast<int>(notes_.size())) return;
    auto& n = notes_[propNoteIndex_];
    const MidiNote& s = propDragStartNote_;

    double timeStep = MusicTheory::getSnapDuration(snap_);
    if (timeStep <= 0.0) timeStep = 0.25; // SnapGrid::None still steps musically
    if (!coarseStep) timeStep = 0.01;     // Alt: fine time adjustment

    switch (field) {
        case 0: { // Pitch — one semitone per few pixels, auditioned as it moves
            const int np = std::clamp(s.pitch + static_cast<int>(std::lround(dy / 6.0f)), 0, 127);
            if (np != n.pitch) {
                n.pitch = np;
                auditionPitch(np);
            }
            break;
        }
        case 1: // Velocity
            n.velocity = std::clamp(s.velocity + dy / 160.0f, 0.0f, 1.0f);
            break;
        case 2: // Pan
            n.pan = std::clamp(s.pan + dy / 90.0f, -1.0f, 1.0f);
            break;
        case 3: { // Start time
            const double steps = std::trunc(dy / 8.0f);
            n.startBeat = std::max(0.0, s.startBeat + steps * timeStep);
            break;
        }
        case 4: { // Length
            const double steps = std::trunc(dy / 8.0f);
            n.durationBeats = std::max(0.125, s.durationBeats + steps * timeStep);
            break;
        }
        default:
            break;
    }
    repaint();
}

bool PianoRollNoteLayer::handleNotePropertiesMouse(const NUIMouseEvent& event) {
    if (propNoteIndex_ < 0) return false;
    if (propNoteIndex_ >= static_cast<int>(notes_.size())) {
        propNoteIndex_ = -1;
        return false;
    }

    const NUIRect& r = propPanelRect_;
    const float rowsTop = r.y + kPropPad + kPropTitleH;
    const auto fieldAt = [&](const NUIPoint& p) -> int {
        if (p.x < r.x + 4.0f || p.x > r.right() - 4.0f) return -1;
        const int row = static_cast<int>(std::floor((p.y - rowsTop) / kPropRowH));
        return (row >= 0 && row < kPropRowCount) ? row : -1;
    };

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (!r.contains(event.position)) {
            closeNoteProperties(true); // click-away accepts, like Accept
            return true;
        }
        const float btnY = r.bottom() - kPropPad - 22.0f;
        const NUIRect resetRect(r.x + kPropPad, btnY, 88.0f, 22.0f);
        const NUIRect acceptRect(r.right() - kPropPad - 88.0f, btnY, 88.0f, 22.0f);
        if (resetRect.contains(event.position)) {
            notes_[propNoteIndex_] = propOriginalNote_;
            repaint();
            return true;
        }
        if (acceptRect.contains(event.position)) {
            closeNoteProperties(true);
            return true;
        }
        const int field = fieldAt(event.position);
        if (field != -1) {
            propDragField_ = field;
            propDragStartPos_ = event.position;
            propDragStartNote_ = notes_[propNoteIndex_];
        }
        return true;
    }

    if (event.released) {
        if (propDragField_ != -1) {
            propDragField_ = -1;
            commitNotes();
        }
        return true;
    }

    if (propDragField_ != -1) {
        // Row drag: up increases. Alt refines the time fields.
        const float dy = propDragStartPos_.y - event.position.y;
        applyNotePropertyDelta(propDragField_, dy, !(event.modifiers & NUIModifiers::Alt));
        return true;
    }

    if (event.wheelDelta != 0.0f) {
        const int field = fieldAt(event.position);
        if (field != -1) {
            // One step per notch, expressed as the drag distance of one step.
            static constexpr float kStepPixels[kPropRowCount] = {6.0f, 4.0f, 4.0f, 8.0f, 8.0f};
            propDragStartNote_ = notes_[propNoteIndex_];
            applyNotePropertyDelta(field,
                                   (event.wheelDelta > 0.0f ? 1.0f : -1.0f) * kStepPixels[field],
                                   !(event.modifiers & NUIModifiers::Alt));
            commitNotes();
        }
        return true;
    }

    return true; // modal: swallow hover/motion so nothing edits underneath
}

void PianoRollNoteLayer::renderNoteProperties(NUIRenderer& renderer) {
    if (propNoteIndex_ < 0 || propNoteIndex_ >= static_cast<int>(notes_.size())) return;
    const auto& n = notes_[propNoteIndex_];
    auto& theme = NUIThemeManager::getInstance();
    const NUIRect& r = propPanelRect_;

    renderer.drawShadow(r, 0.0f, 5.0f, 18.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.5f));
    renderer.fillRoundedRect(r, 8.0f, theme.getColor("backgroundSecondary").withAlpha(0.98f));
    renderer.strokeRoundedRect(r, 8.0f, 1.0f, theme.getColor("border").withAlpha(0.85f));

    const auto accent = theme.getColor("accentPrimary");
    const auto labelColor = theme.getColor("textSecondary").withAlpha(0.85f);
    const auto valueColor = theme.getColor("textPrimary").withAlpha(0.96f);

    const std::string title = "Note Properties - " + MusicTheory::getPitchName(n.pitch);
    renderer.drawText(title, NUIPoint(r.x + kPropPad + 2.0f, r.y + kPropPad + 2.0f), 10.5f,
                      accent.withAlpha(0.95f));

    // Row values
    const int velMidi = static_cast<int>(std::lround(std::clamp(n.velocity, 0.0f, 1.0f) * 127.0f));
    const int panPct = static_cast<int>(std::lround(std::abs(n.pan) * 100.0f));
    const std::string panText =
        panPct == 0 ? "C" : ((n.pan < 0.0f ? "L " : "R ") + std::to_string(panPct));
    char startBuf[32];
    {
        const int bpb = std::max(1, beatsPerBar_);
        const int bar = static_cast<int>(n.startBeat / bpb) + 1;
        const double rem = n.startBeat - static_cast<double>((bar - 1) * bpb);
        const int beat = static_cast<int>(rem) + 1;
        const int pct = static_cast<int>(std::lround((rem - (beat - 1)) * 100.0));
        std::snprintf(startBuf, sizeof(startBuf), "%d:%d +%02d", bar, beat, pct);
    }
    char lenBuf[24];
    std::snprintf(lenBuf, sizeof(lenBuf), "%.2f beats", n.durationBeats);

    struct Row {
        const char* label;
        std::string value;
    };
    const Row rows[kPropRowCount] = {
        {"Pitch", MusicTheory::getPitchName(n.pitch)},
        {"Velocity", std::to_string(velMidi)},
        {"Pan", panText},
        {"Start", startBuf},
        {"Length", lenBuf},
    };

    const float rowsTop = r.y + kPropPad + kPropTitleH;
    for (int i = 0; i < kPropRowCount; ++i) {
        const NUIRect rowRect(r.x + 4.0f, rowsTop + i * kPropRowH, r.width - 8.0f, kPropRowH);
        if (i == propDragField_) {
            renderer.fillRoundedRect(rowRect, 4.0f, accent.withAlpha(0.16f));
        }
        const float textY = rowRect.y + 5.0f;
        renderer.drawText(rows[i].label, NUIPoint(r.x + kPropPad + 2.0f, textY), 10.0f, labelColor);
        const auto valDim = renderer.measureText(rows[i].value, 10.0f);
        renderer.drawText(rows[i].value,
                          NUIPoint(r.right() - kPropPad - 2.0f - valDim.width, textY), 10.0f,
                          i == propDragField_ ? accent.withAlpha(0.98f) : valueColor);
    }

    // Footer buttons
    const float btnY = r.bottom() - kPropPad - 22.0f;
    const NUIRect resetRect(r.x + kPropPad, btnY, 88.0f, 22.0f);
    const NUIRect acceptRect(r.right() - kPropPad - 88.0f, btnY, 88.0f, 22.0f);
    renderer.fillRoundedRect(resetRect, 5.0f, theme.getColor("surfaceRaised").withAlpha(0.9f));
    renderer.strokeRoundedRect(resetRect, 5.0f, 1.0f, theme.getColor("border").withAlpha(0.7f));
    renderer.fillRoundedRect(acceptRect, 5.0f, accent.withAlpha(0.28f));
    renderer.strokeRoundedRect(acceptRect, 5.0f, 1.0f, accent.withAlpha(0.8f));
    const auto resetDim = renderer.measureText("Reset", 10.0f);
    renderer.drawText("Reset",
                      NUIPoint(resetRect.x + (resetRect.width - resetDim.width) * 0.5f,
                               resetRect.y + 5.0f),
                      10.0f, labelColor);
    const auto acceptDim = renderer.measureText("Accept", 10.0f);
    renderer.drawText("Accept",
                      NUIPoint(acceptRect.x + (acceptRect.width - acceptDim.width) * 0.5f,
                               acceptRect.y + 5.0f),
                      10.0f, valueColor);
}

void PianoRollNoteLayer::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    
    // CLIP TO BOUNDS
    renderer.setClipRect(b);
    
    const auto noteColor = themeManager.getColor("accentPrimary").lightened(0.04f);
    const auto noteColorSelected = themeManager.getColor("accentSecondary").lightened(0.12f);
    
    // 1. GHOST NOTES (Read-only backgrounds)
    for (const auto& ghost : ghostPatterns_) {
        NUIColor gCol = ghost.color.withAlpha(0.1f); 
        NUIColor gBorder = ghost.color.withAlpha(0.2f);
        
        for (const auto& n : ghost.notes) {
            float x = snapRectX(beatToScreenX(n.startBeat, pixelsPerBeat_, scrollX_, b.x));
            float y = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
            float w = std::max(1.0f, std::round(static_cast<float>(n.durationBeats * pixelsPerBeat_)));
            float h = keyHeight_;
            
            if (x + w < b.x || x > b.x + b.width || y + h < b.y || y > b.y + b.height) continue;
            
            NUIRect r(x, y + 2, std::max(4.0f, w), h - 4);
            renderer.fillRoundedRect(r, 2.0f, gCol);
            renderer.strokeRoundedRect(r, 2.0f, 1.0f, gBorder);
        }
    }
    
    const double visibleEndBeat = (scrollX_ + b.width) / pixelsPerBeat_;

    for (size_t noteIndex = 0; noteIndex < notes_.size(); ++noteIndex) {
        const auto& n = notes_[noteIndex];
        if (n.startBeat > visibleEndBeat) break;
        
        float x = snapRectX(beatToScreenX(n.startBeat, pixelsPerBeat_, scrollX_, b.x));
        float y = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
        float w = std::max(1.0f, std::round(static_cast<float>(n.durationBeats * pixelsPerBeat_)));
        float h = keyHeight_;
        
        if (x + w < b.x || y + h < b.y || y > b.y + b.height) continue;
        
        // Skip deleted notes
        if (n.isDeleted) continue;
        
        NUIRect r(x + 1.0f, y + 2.0f, std::max(6.0f, w - 2.0f), std::max(5.0f, h - 4.0f));

        const float normalizedVelocity = std::clamp(n.velocity, 0.0f, 1.0f);
        const bool isHovered = static_cast<int>(noteIndex) == hoveredNoteIndex_;
        // A note "sounds" while the playhead sits within its span during
        // playback — it briefly lifts so the ear and eye stay in sync.
        const bool isSounding = isPlaying_ && playheadBeat_ >= n.startBeat &&
                                playheadBeat_ < n.startBeat + n.durationBeats;
        const NUIColor baseColor = n.selected ? noteColorSelected : noteColor;
        NUIColor coreColor = baseColor.withAlpha(0.68f + normalizedVelocity * 0.28f);
        NUIColor edgeColor = n.selected ? NUIColor::white().withAlpha(0.78f)
                                        : (isHovered ? NUIColor::white().withAlpha(0.38f)
                                                     : baseColor.lightened(0.16f).withAlpha(0.74f));
        if (isSounding) {
            coreColor = baseColor.lightened(0.30f).withAlpha(1.0f);
            edgeColor = NUIColor::white().withAlpha(0.88f);
        }

        if (isSounding) {
            renderer.drawShadow(r, 0.0f, 0.0f, 9.0f, baseColor.withAlpha(0.55f));
        }
        renderer.drawShadow(NUIRect(r.x, r.y + 1.0f, r.width, r.height),
                            0.0f,
                            2.0f,
                            5.0f,
                            NUIColor(0, 0, 0, n.selected ? 0.22f : 0.14f));
        renderer.fillRoundedRect(r, 3.0f, coreColor);
        renderer.strokeRoundedRect(r, 3.0f, n.selected ? 1.5f : (isHovered ? 1.25f : 1.0f), edgeColor);
        renderer.fillRoundedRect(NUIRect(r.x + 1.0f, r.y + 2.0f, 2.5f, std::max(2.0f, r.height - 4.0f)),
                                 1.0f,
                                 NUIColor::white().withAlpha(n.selected ? 0.68f : 0.42f));

        if (isHovered && !hoverOnRightEdge_ && !hoverOnLeftEdge_) {
            renderer.fillRoundedRect(NUIRect(r.x + 4.0f, r.y + 1.0f, std::max(2.0f, r.width - 8.0f), 1.0f),
                                     0.5f,
                                     NUIColor::white().withAlpha(0.24f));
        }

        // Selected notes retain subtle resize handles; hovered edges intensify them.
        if (n.selected || (isHovered && (hoverOnRightEdge_ || hoverOnLeftEdge_))) {
            const NUIColor affordanceColor = NUIColor::white().withAlpha(0.72f);
            if (n.selected || hoverOnRightEdge_) {
                renderer.fillRoundedRect(NUIRect(r.right() - 3.0f, r.y + 3.0f, 2.0f, r.height - 6.0f), 1.0f, affordanceColor);
            }
            if (n.selected || hoverOnLeftEdge_) {
                renderer.fillRoundedRect(NUIRect(r.x + 1.0f, r.y + 3.0f, 2.0f, r.height - 6.0f), 1.0f, affordanceColor);
            }
        }

        // [FEATURE] Render pitch name label inside the note block
        // Minimum width threshold: hide label if note is too narrow to avoid overflow
        constexpr float kMinWidthForLabel = 22.0f;
        if (w >= kMinWidthForLabel) {
            std::string pitchLabel = MusicTheory::getPitchName(n.pitch);
            constexpr float kFontSize = 10.0f;
            auto measured = renderer.measureText(pitchLabel, kFontSize);
            // Left-align with padding, vertically centered
            float labelX = r.x + 7.0f;
            float labelY = r.y + (r.height - measured.height) * 0.5f;
            // Clip label to note bounds (renderer should handle this, but extra safety)
            if (labelX + measured.width < r.x + r.width) {
                NUIColor labelColor = NUIColor::white().withAlpha(0.88f);
                renderer.drawText(pitchLabel, NUIPoint(labelX, labelY), kFontSize, labelColor);
            }
        }
    }

    // Draw-mode preview: a translucent phantom of the note a click would place,
    // tracking the snapped cursor cell so placement reads before you commit it.
    // Only while the pencil hovers empty space — never over an existing note.
    if (tool_ == GlobalTool::Pencil && state_ == State::None && hoveredNoteIndex_ == -1 &&
        hoveredPitch_ >= 0 && hoverBeat_ >= 0.0) {
        // In chord mode the phantom shows the whole diatonic triad a click stamps.
        const std::vector<int> previewPitches =
            chordMode_ ? buildTriad(snapPitchToScale(hoveredPitch_))
                       : std::vector<int>{snapPitchToScale(hoveredPitch_)};
        const float px = snapRectX(beatToScreenX(hoverBeat_, pixelsPerBeat_, scrollX_, b.x));
        const float pw = std::max(6.0f, std::round(static_cast<float>(lastNoteDuration_ * pixelsPerBeat_)) - 2.0f);
        const float ph = std::max(5.0f, keyHeight_ - 4.0f);
        for (int previewPitch : previewPitches) {
            const float py = b.y + (127 - previewPitch) * keyHeight_ - scrollY_;
            if (px + pw >= b.x && px <= b.right() && py + ph >= b.y && py <= b.bottom()) {
                const NUIRect pr(px + 1.0f, py + 2.0f, pw, ph);
                renderer.fillRoundedRect(pr, 3.0f, noteColor.withAlpha(0.20f));
                renderer.strokeRoundedRect(pr, 3.0f, 1.0f, noteColor.lightened(0.16f).withAlpha(0.48f));
            }
        }
    }

    // Floating edit HUD while placing/moving/resizing a note: read what you're
    // doing — pitch + bar:beat when moving, length while resizing, pitch + length
    // while drawing. Tracks the grabbed note and flips below if it clips the top.
    const bool isEditingNote = (state_ == State::Painting || state_ == State::Moving ||
                                state_ == State::Resizing || state_ == State::ResizingLeft);
    if (isEditingNote && dragAnchorIndex_ >= 0 && dragAnchorIndex_ < static_cast<int>(notes_.size()) &&
        !notes_[dragAnchorIndex_].isDeleted) {
        const auto& an = notes_[dragAnchorIndex_];

        const auto formatLength = [](double beats) {
            char buf[24];
            if (std::abs(beats - std::round(beats)) < 0.01)
                std::snprintf(buf, sizeof(buf), "%d beat%s", static_cast<int>(std::round(beats)),
                              std::abs(beats - 1.0) < 0.01 ? "" : "s");
            else
                std::snprintf(buf, sizeof(buf), "%.2f beats", beats);
            return std::string(buf);
        };
        const auto formatBarBeat = [this](double startBeat) {
            const int bpb = std::max(1, beatsPerBar_);
            const double barIndex = std::floor(startBeat / bpb);
            const double beatInBar = startBeat - barIndex * bpb;
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d:%.2f", static_cast<int>(barIndex) + 1, beatInBar + 1.0);
            return std::string(buf);
        };

        std::string label;
        if (state_ == State::Resizing || state_ == State::ResizingLeft)
            label = formatLength(an.durationBeats);
        else if (state_ == State::Moving)
            label = MusicTheory::getPitchName(an.pitch) + "  " + formatBarBeat(an.startBeat);
        else // Painting: pitch + the length being dragged out
            label = MusicTheory::getPitchName(an.pitch) + "  " + formatLength(an.durationBeats);

        const float ax = snapRectX(beatToScreenX(an.startBeat, pixelsPerBeat_, scrollX_, b.x));
        const float ay = b.y + (127 - an.pitch) * keyHeight_ - scrollY_;
        constexpr float kFontSize = 10.0f;
        const auto measured = renderer.measureText(label, kFontSize);
        const float bubbleW = measured.width + 12.0f;
        const float bubbleH = 17.0f;
        float bubbleY = ay - bubbleH - 3.0f;
        if (bubbleY < b.y) bubbleY = ay + keyHeight_ + 3.0f;
        const float bubbleX = std::clamp(ax, b.x + 2.0f, b.right() - bubbleW - 2.0f);
        const NUIRect bubble(bubbleX, bubbleY, bubbleW, bubbleH);
        renderer.fillRoundedRect(bubble, 4.0f, NUIColor::black().withAlpha(0.92f));
        renderer.strokeRoundedRect(bubble, 4.0f, 1.0f, themeManager.getColor("accentPrimary").withAlpha(0.80f));
        renderer.drawText(label,
                          NUIPoint(bubble.x + 6.0f, bubble.y + (bubble.height - measured.height) * 0.5f),
                          kFontSize,
                          NUIColor::white().withAlpha(0.95f));
    }

    // Velocity value bubble after an Alt+wheel nudge, while the cursor stays on
    // the note it edited (guarded so a -1/-1 match can't show a phantom bubble).
    if (velocityBubbleIndex_ >= 0 && velocityBubbleIndex_ == hoveredNoteIndex_ &&
        velocityBubbleIndex_ < static_cast<int>(notes_.size()) && !notes_[velocityBubbleIndex_].isDeleted) {
        const auto& vn = notes_[velocityBubbleIndex_];
        const float vx = snapRectX(beatToScreenX(vn.startBeat, pixelsPerBeat_, scrollX_, b.x));
        const float vy = b.y + (127 - vn.pitch) * keyHeight_ - scrollY_;
        const std::string label = "V " + std::to_string(static_cast<int>(std::lround(vn.velocity * 127.0f)));
        constexpr float kFontSize = 10.0f;
        const auto measured = renderer.measureText(label, kFontSize);
        const float bubbleW = measured.width + 12.0f;
        const float bubbleH = 17.0f;
        float bubbleY = vy - bubbleH - 3.0f;
        if (bubbleY < b.y) bubbleY = vy + keyHeight_ + 3.0f;
        const float bubbleX = std::clamp(vx, b.x + 2.0f, b.right() - bubbleW - 2.0f);
        const NUIRect bubble(bubbleX, bubbleY, bubbleW, bubbleH);
        renderer.fillRoundedRect(bubble, 4.0f, NUIColor::black().withAlpha(0.92f));
        renderer.strokeRoundedRect(bubble, 4.0f, 1.0f, themeManager.getColor("accentPrimary").withAlpha(0.80f));
        renderer.drawText(label,
                          NUIPoint(bubble.x + 6.0f, bubble.y + (bubble.height - measured.height) * 0.5f),
                          kFontSize,
                          NUIColor::white().withAlpha(0.95f));
    }

    // Rubber-band selection rectangle (already normalized during drag)
    if (state_ == State::SelectingBox && selectionRect_.width > 0 && selectionRect_.height > 0) {
        renderer.fillRoundedRect(selectionRect_, 2.0f, NUIColor(0.545f, 0.498f, 1.0f, 0.15f));
        renderer.strokeRoundedRect(selectionRect_, 2.0f, 1.0f, NUIColor(0.545f, 0.498f, 1.0f, 0.55f));
    }

    renderNoteProperties(renderer);

    renderer.clearClipRect();

    // Cleanup Deleted Notes
    bool cleanNeeded = false;
    for (const auto& n : notes_) { 
        if (n.isDeleted && n.animationScale <= 0.001f) {
            cleanNeeded = true; 
            break; 
        }
    }
    
    if (cleanNeeded) {
        auto it = std::remove_if(notes_.begin(), notes_.end(), [](const MidiNote& n){ 
            return n.isDeleted && n.animationScale <= 0.001f; 
        });
        if (it != notes_.end()) {
            notes_.erase(it, notes_.end());
            commitNotes(); // Notify removal
        }
    }
}

int PianoRollNoteLayer::findNoteAt(float localX, float localY) {
    return findNoteAtLocal(notes_, localX, localY, pixelsPerBeat_, keyHeight_);
}

bool PianoRollNoteLayer::onMouseEvent(const NUIMouseEvent& event) {
    if (state_ == State::None && !getBounds().contains(event.position)) {
        // Reset hover when leaving bounds
        if (hoveredNoteIndex_ != -1) {
            hoveredNoteIndex_ = -1;
            hoverOnRightEdge_ = false;
            hoverOnLeftEdge_ = false;
            if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
        }
        if (hoveredPitch_ != -1) {
            hoveredPitch_ = -1;
            if (onHoveredPitchChanged_) onHoveredPitchChanged_(-1);
        }
        if (hoverBeat_ >= 0.0) {
            hoverBeat_ = -1.0;
            repaint();
        }
        return false;
    }

    auto b = getBounds();
    float localX = event.position.x - b.x + scrollX_;
    float localY = event.position.y - b.y + scrollY_;

    // Holding Alt mid-drag bypasses the grid for fine placement. Clone drags
    // (CopyDragging) are excluded — Alt is what starts them, so it can't
    // double as the fine toggle there. Recomputed every event so it can never
    // go stale between gestures.
    fineDrag_ = (event.modifiers & NUIModifiers::Alt) &&
                (state_ == State::Painting || state_ == State::Moving ||
                 state_ == State::Resizing || state_ == State::ResizingLeft);

    // The Note Properties popup is modal within the layer while open.
    if (propNoteIndex_ >= 0) {
        return handleNotePropertiesMouse(event);
    }

    const int cursorPitch = std::clamp(127 - static_cast<int>(localY / keyHeight_), 0, 127);
    if (hoveredPitch_ != cursorPitch) {
        hoveredPitch_ = cursorPitch;
        if (onHoveredPitchChanged_) onHoveredPitchChanged_(cursorPitch);
    }

    // --- HOVER / SMART CURSOR (no button activity) ---
    if (state_ == State::None && !event.pressed && !event.released) {
        int hitIdx = findNoteAt(localX, localY);
        // Track the snapped beat the cursor sits on so the pencil can render a
        // phantom of the note a click would place. Only meaningful over empty
        // space with the pencil active; suppressed elsewhere so it never lingers.
        const double snappedHoverBeat =
            (tool_ == GlobalTool::Pencil && hitIdx == -1)
                ? snapToGrid(std::max(0.0, static_cast<double>(localX) / pixelsPerBeat_))
                : -1.0;
        if (hoverBeat_ != snappedHoverBeat) {
            hoverBeat_ = snappedHoverBeat;
            repaint();
        }
        if (hitIdx == -1) {
            if (hoveredNoteIndex_ != -1) {
                hoveredNoteIndex_ = -1;
                hoverOnRightEdge_ = false;
                hoverOnLeftEdge_ = false;
                repaint();
            }
            if (platformBridge_) {
                platformBridge_->setCursorStyle(
                    tool_ == GlobalTool::Pencil ? NUICursorStyle::Crosshair : NUICursorStyle::Arrow);
            }
        } else {
            const auto& n = notes_[hitIdx];
            float nx = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
            float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);
            float edgeZone = std::min(10.0f, nw * 0.30f);
            bool onLeftEdge = (event.position.x <= nx + edgeZone);
            bool onRightEdge = (event.position.x >= nx + nw - edgeZone);

            if (hoveredNoteIndex_ != hitIdx || hoverOnRightEdge_ != onRightEdge || hoverOnLeftEdge_ != onLeftEdge) {
                hoveredNoteIndex_ = hitIdx;
                hoverOnRightEdge_ = onRightEdge;
                hoverOnLeftEdge_ = onLeftEdge;
                repaint();
            }
            if (platformBridge_) {
                if (onLeftEdge || onRightEdge)
                    platformBridge_->setCursorStyle(NUICursorStyle::ResizeEW);
                else
                    platformBridge_->setCursorStyle(NUICursorStyle::Grab);
            }
        }
    }

    // --- WHEEL OVER A NOTE = VELOCITY ---
    // Scrolling over a note shapes its velocity (and the whole selection if
    // that note is part of it) — no modifier needed, though Alt still works.
    // Ctrl (zoom) and Shift (h-scroll) keep their bindings, and over empty
    // space the wheel scrolls the grid as usual.
    if (event.wheelDelta != 0.0f && !(event.modifiers & NUIModifiers::Ctrl) &&
        !(event.modifiers & NUIModifiers::Shift) && hoveredNoteIndex_ >= 0 &&
        hoveredNoteIndex_ < static_cast<int>(notes_.size()) && !notes_[hoveredNoteIndex_].isDeleted) {
        auto oldNotes = notes_;
        const float delta = event.wheelDelta * 0.04f; // ~5 MIDI steps per notch
        const bool editSelection = notes_[hoveredNoteIndex_].selected;
        for (auto& n : notes_) {
            if (n.isDeleted) continue;
            if (editSelection ? n.selected : (&n == &notes_[hoveredNoteIndex_])) {
                n.velocity = std::clamp(n.velocity + delta, 0.0f, 1.0f);
            }
        }
        lastNoteVelocity_ = notes_[hoveredNoteIndex_].velocity; // adopt for new notes
        velocityBubbleIndex_ = hoveredNoteIndex_;
        // Coalesce a continuous scrub into one undo step instead of one per notch.
        if (!undoStack_.empty() && undoStack_.back().description == "Velocity") {
            undoStack_.back().notesAfter = notes_;
        } else {
            pushUndo("Velocity", oldNotes, notes_);
        }
        commitNotes();
        repaint();
        return true;
    }

    // --- RIGHT CLICK / ERASER (FAST ERASE) ---
    if (event.button == NUIMouseButton::Right) {
        if (event.pressed) {
             state_ = State::Erasing;
             
             // Delete immediately on press
             int idx = findNoteAt(localX, localY);
             if (idx != -1) {
                 auto oldNotes = notes_;
                 notes_.erase(notes_.begin() + idx);
                 pushUndo("Delete Note", oldNotes, notes_);
                 commitNotes();
                 repaint();
             }
        }
        if (event.released) state_ = State::None;
    }
    
    if (state_ == State::Erasing && !event.released) {
         int idx = findNoteAt(localX, localY);
         if (idx != -1 && !notes_[idx].isDeleted) {
             auto oldNotes = notes_;
             notes_.erase(notes_.begin() + idx);
             commitNotes();
             repaint();
         }
         return true;
    }

    // --- LEFT CLICK HANDLING ---
    if (event.pressed && event.button == NUIMouseButton::Left) {
        setFocused(true); // Gain keyboard focus for shortcuts
        int clickedIndex = findNoteAt(localX, localY);
        
        // DOUBLE CLICK: note → precision properties popup; empty space → add.
        // (Deletion stays on right-click / eraser / Delete.)
        if (event.doubleClick) {
            if (clickedIndex != -1) {
                 openNoteProperties(clickedIndex);
            } else {
                 auto oldNotes = notes_;
                 // Create New Note
                 double beat = std::max(0.0, static_cast<double>(localX / pixelsPerBeat_));
                 beat = snapToGrid(beat);
                 
                  int pitch = 127 - static_cast<int>(localY / keyHeight_);
                  pitch = std::clamp(pitch, 0, 127);
                  pitch = snapPitchToScale(pitch);
                 
                 MidiNote newNote;
                 newNote.pitch = pitch;
                 newNote.startBeat = beat;
                 newNote.durationBeats = lastNoteDuration_; 
                 newNote.velocity = lastNoteVelocity_;
                 newNote.unitId = defaultUnitId_;
                 newNote.selected = true;
                 newNote.animationScale = 1.0f; // Instant appearance

                 if (!(event.modifiers & NUIModifiers::Shift)) {
                    for(auto& n : notes_) n.selected = false;
                 }
                 
                 notes_.push_back(newNote);
                 pushUndo("Add Note", oldNotes, notes_);
                 commitNotes();
                 repaint();
            }
            return true;
        }

        // Ctrl+drag on empty space = marquee select, in any tool. This is how a
        // lasso coexists with the pencil (whose plain drag places notes): the
        // pencil keeps drawing, and Ctrl temporarily borrows a selection box.
        if (clickedIndex == -1 && (event.modifiers & NUIModifiers::Ctrl)) {
            state_ = State::SelectingBox;
            dragStartPos_ = event.position;
            selectionRect_ = NUIRect(event.position.x, event.position.y, 0, 0);
            if (!(event.modifiers & NUIModifiers::Shift)) {
                for (auto& n : notes_) n.selected = false;
            }
            repaint();
            return true;
        }

        // 1. Eraser Tool
        if (tool_ == GlobalTool::Eraser) {
            if (clickedIndex != -1) {
                auto oldNotes = notes_;
                notes_.erase(notes_.begin() + clickedIndex);
                pushUndo("Erase", oldNotes, notes_);
                commitNotes();
                repaint();
            }
            return true;
        }
        
        // 2. Pencil / Pointer
        // Smart Logic: If hovering a note, allow manipulation (Move/Resize) unless explicitly blocked
        // User Request: "Pen... placing notes moving notes... place but not extend or move" -> Enable Move/Resize in Pen mode.
        
        bool intentToPaint = (tool_ == GlobalTool::Pencil && clickedIndex == -1);

        // Shift+pencil on empty space starts a paint-brush stroke: notes are
        // stamped into each snap cell the cursor crosses (see drag handling).
        // (Ctrl is reserved for the marquee, so the brush lives on Shift.)
        if (intentToPaint && (event.modifiers & NUIModifiers::Shift)) {
            for (auto& note : notes_) note.selected = false;
            state_ = State::BrushPainting;
            dragStartNotes_ = notes_; // snapshot for a single-stroke undo
            dragStartPos_ = event.position;
            dragStartScrollX_ = scrollX_;
            dragStartScrollY_ = scrollY_;
            paintBrushAt(localX, localY);
            repaint();
            return true;
        }

        // Chord mode: a click stamps the diatonic triad rooted at the cell
        // (discrete — no drag-to-lengthen; the stamped chord lands selected so
        // it can be resized or strummed as a unit right after).
        if (intentToPaint && chordMode_) {
            auto oldNotes = notes_;
            if (!(event.modifiers & NUIModifiers::Shift)) {
                for (auto& note : notes_) note.selected = false;
            }
            const double startBeat = snapToGrid(std::max(0.0, static_cast<double>(localX) / pixelsPerBeat_));
            const int rootPitch = snapPitchToScale(std::clamp(127 - static_cast<int>(localY / keyHeight_), 0, 127));
            for (int p : buildTriad(rootPitch)) {
                bool occupied = false;
                for (const auto& n : notes_) {
                    if (!n.isDeleted && n.pitch == p && std::abs(n.startBeat - startBeat) < 0.001) {
                        occupied = true;
                        break;
                    }
                }
                if (occupied) continue;
                MidiNote note;
                note.pitch = p;
                note.startBeat = startBeat;
                note.durationBeats = lastNoteDuration_;
                note.velocity = lastNoteVelocity_;
                note.unitId = defaultUnitId_;
                note.selected = true;
                note.animationScale = 1.0f;
                notes_.push_back(note);
            }
            // Sound the whole triad — the engine keeps a small pool of
            // audition voices, one command per pitch.
            if (!(isPlayingCallback_ && isPlayingCallback_()) && onPreviewNote_) {
                const int velocity = static_cast<int>(std::lround(lastNoteVelocity_ * 127.0f));
                for (int p : buildTriad(rootPitch)) {
                    onPreviewNote_(p, velocity);
                }
                auditionPitch_ = rootPitch;
            }
            pushUndo("Add Chord", oldNotes, notes_);
            commitNotes();
            repaint();
            return true;
        }

        if (intentToPaint) {
            // --- PAINT NEW NOTE ---
            if (!(event.modifiers & NUIModifiers::Shift)) {
                for (auto& note : notes_) note.selected = false;
            }

            state_ = State::Painting;
            dragStartNotes_ = notes_; 
            
            double beat = std::max(0.0, static_cast<double>(localX / pixelsPerBeat_));
            paintStartBeat_ = snapToGrid(beat);
            
            int pitch = 127 - static_cast<int>(localY / keyHeight_);
            pitch = std::clamp(pitch, 0, 127);
            paintPitch_ = snapPitchToScale(pitch);
            
            MidiNote newNote;
            newNote.pitch = paintPitch_;
            newNote.startBeat = paintStartBeat_;
            newNote.durationBeats = lastNoteDuration_; 
            newNote.velocity = lastNoteVelocity_;
            newNote.unitId = defaultUnitId_;
            newNote.selected = true;
            newNote.animationScale = 1.0f; // Instant appearance
            
            notes_.push_back(newNote);
            paintingNoteIndex_ = static_cast<int>(notes_.size()) - 1;
            dragAnchorIndex_ = paintingNoteIndex_;

            auditionPitch(paintPitch_); // sound the note as it's laid down

            dragStartPos_ = event.position;
            dragStartScrollX_ = scrollX_;
            dragStartScrollY_ = scrollY_;
            repaint();
            return true;
        }
        
        // Interact with Existing Note (Move/Resize/Select)
        if (clickedIndex != -1) {
            bool wasSelected = notes_[clickedIndex].selected;

            // Ctrl = Toggle
            if (event.modifiers & NUIModifiers::Ctrl) {
                notes_[clickedIndex].selected = !wasSelected;
                return true;
            }

            // Alt+drag: clone selection and drag copies — skip selection logic
            if (event.modifiers & NUIModifiers::Alt) {
                // Ensure clicked note is part of the selection
                notes_[clickedIndex].selected = true;
                copyDragIndices_.clear();

                // Clone all selected notes
                std::vector<int> selectedIndices;
                for (int i = 0; i < static_cast<int>(notes_.size()); ++i) {
                    if (notes_[i].selected && !notes_[i].isDeleted)
                        selectedIndices.push_back(i);
                }

                // Deselect originals, create clones, select clones
                for (int idx : selectedIndices) {
                    notes_[idx].selected = false;
                    MidiNote clone = notes_[idx];
                    clone.selected = true;
                    clone.isDeleted = false;
                    notes_.push_back(clone);
                    copyDragIndices_.push_back(static_cast<int>(notes_.size()) - 1);
                }

                if (copyDragIndices_.empty()) return true;

                // Snapshot AFTER cloning so dragStartNotes_ includes clones
                dragStartNotes_ = notes_;

                state_ = State::CopyDragging;
                dragStartPos_ = event.position;
                dragStartScrollX_ = scrollX_;
                dragStartScrollY_ = scrollY_;
                if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Grabbing);
                repaint();
                return true;
            }

            // Shift = add to selection
            if (event.modifiers & NUIModifiers::Shift) {
                notes_[clickedIndex].selected = true;
            } else if (!wasSelected) {
                // Clicked unselected note without modifiers -> clear others and select this
                for (auto& N : notes_) N.selected = false;
                notes_[clickedIndex].selected = true;
            }
            // If clicked selected note, keep others selected (for group move)

            const auto& n = notes_[clickedIndex];
            float nx = static_cast<float>(n.startBeat * pixelsPerBeat_);
            float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);

            // Smart Edge Detection — left and right
            float edgeZone = std::min(10.0f, nw * 0.3f);
            bool isLeftEdge = (localX <= nx + edgeZone);
            bool isRightEdge = (localX >= nx + nw - edgeZone);

            if (isLeftEdge)
                state_ = State::ResizingLeft;
            else if (isRightEdge)
                state_ = State::Resizing;
            else
                state_ = State::Moving;

            dragStartPos_ = event.position;
            dragStartScrollX_ = scrollX_;
            dragStartScrollY_ = scrollY_;
            dragStartNotes_ = notes_;

            // Anchor the edit HUD to the grabbed note for move/resize.
            dragAnchorIndex_ = clickedIndex;
            // Grabbing a note to move it sounds its pitch, and keeps sounding
            // the new pitch as you drag it up/down.
            if (state_ == State::Moving) {
                moveAnchorPitch_ = notes_[clickedIndex].pitch;
                auditionPitch(moveAnchorPitch_);
            }

            // Spec 2: adopt clicked note's length for subsequent placement
            lastNoteDuration_ = notes_[clickedIndex].durationBeats;

            if (platformBridge_) {
                platformBridge_->setCursorStyle(
                    (isLeftEdge || isRightEdge) ? NUICursorStyle::ResizeEW : NUICursorStyle::Grabbing);
            }

            repaint();
            return true;
        }
        
        // Empty Click (Pointer or Pencil logic fell through) -> Selection Box
        // Only if NOT pencil (Pencil paints) - handled by intentToPaint
        if (tool_ == GlobalTool::Pointer) {
             state_ = State::SelectingBox;
             dragStartPos_ = event.position;
             selectionRect_ = NUIRect(event.position.x, event.position.y, 0, 0); // Start size 0
             
             if (!(event.modifiers & NUIModifiers::Shift)) {
                 for (auto& n : notes_) n.selected = false;
             }
             repaint();
             return true;
        }
    }
    
    // --- DRAGGING (Left Button) ---
    if (!event.pressed && !event.released && state_ != State::None) {
        auto parent = getParent();
        updateEdgeScrolling(event.position.x, event.position.y, b, [this, parent]() {
            if (parent) {
                parent->repaint();
            }
        });
        
        if (state_ == State::SelectingBox) {
            // Normalize at storage so render sees positive dimensions
            float rawW = event.position.x - dragStartPos_.x;
            float rawH = event.position.y - dragStartPos_.y;
            NUIRect norm(dragStartPos_.x, dragStartPos_.y, rawW, rawH);
            if (norm.width < 0) { norm.x += norm.width; norm.width *= -1; }
            if (norm.height < 0) { norm.y += norm.height; norm.height *= -1; }
            selectionRect_ = norm;
            
            // Select Intersecting Notes (marquee adds to selection during drag)
            for (auto& n : notes_) {
                // Skip deleted notes
                if (n.isDeleted) continue;

                float nx = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
                float ny = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
                float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);
                float nh = keyHeight_;

                NUIRect nr(nx, ny, nw, nh);

                // Standard marquee: select notes inside the box
                // Notes outside the box are left unchanged during drag
                // (final selection replacement happens on release)
                if (nr.x < norm.x + norm.width && nr.x + nr.width > norm.x &&
                    nr.y < norm.y + norm.height && nr.y + nr.height > norm.y) {
                    n.selected = true;
                }
            }
            repaint();
            return true;
        }
        
        if (state_ == State::BrushPainting) {
            // Stamp a note in whatever snap cell the cursor is over now.
            if (paintBrushAt(localX, localY)) repaint();
            return true;
        }

        if (state_ == State::Painting && paintingNoteIndex_ != -1) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            double beatDelta = dx / pixelsPerBeat_;

            double newDur = lastNoteDuration_ + beatDelta;
            newDur = std::max(0.125, snapToGrid(newDur));
            
            notes_[paintingNoteIndex_].durationBeats = newDur;
            repaint();
            return true;
        }
        else if (state_ == State::Moving) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            float dy = (event.position.y - dragStartPos_.y) + (scrollY_ - dragStartScrollY_);

            double beatDelta = dx / pixelsPerBeat_;
            int pitchDelta = -static_cast<int>(dy / keyHeight_);

            // Clamp beatDelta so the leftmost selected note doesn't go below 0
            double minStart = std::numeric_limits<double>::max();
            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected)
                    minStart = std::min(minStart, dragStartNotes_[i].startBeat);
            }
            if (minStart + beatDelta < 0.0)
                beatDelta = -minStart;

            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    notes_[i].startBeat = snapToGrid(dragStartNotes_[i].startBeat + beatDelta);
                    int newPitch = dragStartNotes_[i].pitch + pitchDelta;
                    newPitch = std::clamp(newPitch, 0, 127);
                    notes_[i].pitch = snapPitchToScale(newPitch);
                }
            }
            // Re-audition when the grabbed note lands on a new pitch.
            auditionPitch(snapPitchToScale(std::clamp(moveAnchorPitch_ + pitchDelta, 0, 127)));
            repaint();
            return true;
        }
        else if (state_ == State::Resizing) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            double beatDelta = dx / pixelsPerBeat_;

            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    double newDur = dragStartNotes_[i].durationBeats + beatDelta;
                    notes_[i].durationBeats = std::max(0.125, snapToGrid(newDur));
                }
            }
            repaint();
            return true;
        }
        else if (state_ == State::ResizingLeft) {
            // Left-edge resize: move start, adjust duration to keep end fixed
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            double beatDelta = dx / pixelsPerBeat_;

            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    double origStart = dragStartNotes_[i].startBeat;
                    double origEnd = origStart + dragStartNotes_[i].durationBeats;
                    double newStart = std::max(0.0, snapToGrid(origStart + beatDelta));
                    double newDur = origEnd - newStart;
                    if (newDur < 0.125) {
                        newDur = 0.125;
                        newStart = origEnd - 0.125;
                        if (newStart < 0.0) newStart = 0.0;
                    }
                    notes_[i].startBeat = newStart;
                    notes_[i].durationBeats = newDur;
                }
            }
            repaint();
            return true;
        }
        else if (state_ == State::CopyDragging) {
            float dx = (event.position.x - dragStartPos_.x) + (scrollX_ - dragStartScrollX_);
            float dy = (event.position.y - dragStartPos_.y) + (scrollY_ - dragStartScrollY_);

            double beatDelta = dx / pixelsPerBeat_;
            int pitchDelta = -static_cast<int>(dy / keyHeight_);

            // Clamp beatDelta so the leftmost clone doesn't go below 0
            double minStart = std::numeric_limits<double>::max();
            for (int idx : copyDragIndices_) {
                if (idx >= 0 && idx < static_cast<int>(dragStartNotes_.size()))
                    minStart = std::min(minStart, dragStartNotes_[idx].startBeat);
            }
            if (minStart + beatDelta < 0.0)
                beatDelta = -minStart;

            for (int idx : copyDragIndices_) {
                if (idx >= 0 && idx < static_cast<int>(notes_.size())) {
                    notes_[idx].startBeat = snapToGrid(dragStartNotes_[idx].startBeat + beatDelta);
                    int newPitch = dragStartNotes_[idx].pitch + pitchDelta;
                    newPitch = std::clamp(newPitch, 0, 127);
                    notes_[idx].pitch = snapPitchToScale(newPitch);
                }
            }
            commitNotes();
            repaint();
            return true;
        }
    }
    
    // --- RELEASE ---
    if (event.released && event.button == NUIMouseButton::Left) {
        auditionStop(); // release any note sounded while painting/dragging
        dragAnchorIndex_ = -1;
        if (state_ == State::SelectingBox) {
            // Marquee selection done. Selection was cleared on click (without Shift),
            // and notes inside the box were selected during drag.
            state_ = State::None;
            selectionRect_ = NUIRect(0, 0, 0, 0);
            if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
            repaint();
            return true;
        }

        if (state_ != State::None) {
            // Update Memory
            if (state_ == State::Painting && paintingNoteIndex_ != -1) {
                lastNoteDuration_ = notes_[paintingNoteIndex_].durationBeats;
            } else if (state_ == State::Resizing || state_ == State::ResizingLeft) {
                for (const auto& n : notes_) { if (n.selected) { lastNoteDuration_ = n.durationBeats; break; } }
            }

            pushUndo(state_ == State::CopyDragging ? "Alt+Drag Copy" : "Edit", dragStartNotes_, notes_);
            state_ = State::None;
            paintingNoteIndex_ = -1;
            copyDragIndices_.clear();
            if (platformBridge_) platformBridge_->setCursorStyle(NUICursorStyle::Arrow);
            commitNotes();
            repaint();
            return true;
        }
    }

    return NUIComponent::onMouseEvent(event);
}


// Static clipboard for now (shared across instances is fine/better)
static std::vector<MidiNote> s_noteClipboard;

bool PianoRollNoteLayer::onKeyEvent(const NUIKeyEvent& event) {
    bool ctrl = (event.modifiers & NUIModifiers::Ctrl);

    // Note Properties popup is modal: Enter accepts, Escape cancels, and
    // everything else is swallowed so shortcuts can't edit the note beneath.
    if (propNoteIndex_ >= 0 && event.pressed) {
        if (event.keyCode == NUIKeyCode::Escape) {
            closeNoteProperties(false);
            return true;
        }
        if (event.keyCode == NUIKeyCode::Enter) {
            closeNoteProperties(true);
            return true;
        }
        return true;
    }

    if (event.pressed) {
        // Undo / Redo
        if (ctrl && event.keyCode == NUIKeyCode::Z) {
            bool shift = (event.modifiers & NUIModifiers::Shift);
            if (shift) redo(); else undo();
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::Y) {
            redo();
            return true;
        }

        // Ctrl+L: elongate selected notes to connect to the next note (legato),
        // or out to the next snap/beat boundary when nothing follows.
        if (ctrl && event.keyCode == NUIKeyCode::L) {
            connectSelectedNotes();
            return true;
        }

        // Q: quantize selected note starts to the grid. Ctrl+G: glue selected
        // notes on the same pitch into one. Both are no-ops without a selection.
        if (!ctrl && event.keyCode == NUIKeyCode::Q) {
            quantizeSelectedNotes();
            return true;
        }
        if (ctrl && event.keyCode == NUIKeyCode::G) {
            glueSelectedNotes();
            return true;
        }

        if (event.keyCode == NUIKeyCode::Delete || event.keyCode == NUIKeyCode::Backspace) {
            auto oldNotes = notes_; // Snapshot

            // Erase-remove: actually delete selected notes from the vector
            notes_.erase(
                std::remove_if(notes_.begin(), notes_.end(),
                    [](const MidiNote& n) { return n.selected; }),
                notes_.end());

            if (notes_.size() != oldNotes.size()) {
                pushUndo("Delete", oldNotes, notes_);
                commitNotes();
                repaint();
            }
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::C) {
            // Copy
            s_noteClipboard.clear();
            for (const auto& n : notes_) {
                if (n.selected && !n.isDeleted) s_noteClipboard.push_back(n); // Don't copy deleted
            }
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::V) {
            // Spec 5: Paste at playhead position
            if (s_noteClipboard.empty()) return true;

            auto oldNotes = notes_; // Snapshot

            // Deselect current
            for (auto& n : notes_) n.selected = false;

            // Find earliest note in clipboard, offset so it lands at playhead
            double earliest = s_noteClipboard[0].startBeat;
            for (const auto& n : s_noteClipboard) {
                if (n.startBeat < earliest) earliest = n.startBeat;
            }
            double offset = playheadBeat_ - earliest;

            for (auto n : s_noteClipboard) {
                n.startBeat += offset;
                n.selected = true;
                n.isDeleted = false;
                notes_.push_back(n);
            }
            pushUndo("Paste", oldNotes, notes_);
            commitNotes();
            repaint();
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::D) {
            // Duplicate (Ctrl+D)
            double minStart = 100000.0;
            double maxEnd = -1.0;
            bool hasSelection = false;
            
            for (const auto& n : notes_) {
                if (n.selected && !n.isDeleted) {
                    hasSelection = true;
                    minStart = std::min(minStart, n.startBeat);
                    maxEnd = std::max(maxEnd, n.startBeat + n.durationBeats);
                }
            }
            
            if (hasSelection && maxEnd > 0) {
                double shift = maxEnd - minStart;
                if (shift < 0.25) shift = 0.25;
                
                
                auto oldNotes = notes_; // Snapshot
                
                for (auto& n : notes_) n.selected = false;

                for (const auto& n : oldNotes) {
                    if (n.selected && !n.isDeleted) {
                        MidiNote clone = n;
                        clone.startBeat += shift;
                        clone.selected = true; 
                        clone.isDeleted = false;
                        notes_.push_back(clone);
                    }
                }
                
                pushUndo("Duplicate", oldNotes, notes_);
                commitNotes();
                repaint();
                return true;
            }
        }
    // Select All (Ctrl+A)
        if (ctrl && event.keyCode == NUIKeyCode::A) {
            for (auto& n : notes_) {
                if (!n.isDeleted) n.selected = true;
            }
            repaint();
            return true;
        }

        // Clear selection on Escape
        if (event.keyCode == NUIKeyCode::Escape) {
            for (auto& n : notes_) n.selected = false;
            state_ = State::None;
            repaint();
            return true;
        }

        // Arrow nudge — only when notes are selected
        {
            bool anySelected = false;
            for (const auto& n : notes_) { if (n.selected && !n.isDeleted) { anySelected = true; break; } }
            if (anySelected) {
                double snapStep = MusicTheory::getSnapDuration(snap_);
                if (snapStep <= 0.0) snapStep = 1.0;

                // Shift modifies behavior: plain = move, Shift = resize
                bool shift = (event.modifiers & NUIModifiers::Shift);

                if (event.keyCode == NUIKeyCode::Left) {
                    auto oldNotes = notes_;
                    if (shift) {
                        // Shrink: reduce duration of selected notes
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.durationBeats = std::max(0.125, n.durationBeats - snapStep);
                            }
                        }
                    } else {
                        // Nudge left
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.startBeat = std::max(0.0, n.startBeat - snapStep);
                            }
                        }
                    }
                    pushUndo(shift ? "Resize Left" : "Nudge Left", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
                else if (event.keyCode == NUIKeyCode::Right) {
                    auto oldNotes = notes_;
                    if (shift) {
                        // Extend: increase duration of selected notes
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.durationBeats += snapStep;
                            }
                        }
                    } else {
                        // Nudge right
                        for (auto& n : notes_) {
                            if (n.selected && !n.isDeleted) {
                                n.startBeat += snapStep;
                            }
                        }
                    }
                    pushUndo(shift ? "Resize Right" : "Nudge Right", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
                else if (event.keyCode == NUIKeyCode::Up) {
                    auto oldNotes = notes_;
                    for (auto& n : notes_) {
                        if (n.selected && !n.isDeleted) {
                            if (shift) {
                                n.pitch = std::min(127, n.pitch + 12); // whole octave
                            } else if (snapToScale_ && scaleType_ != ScaleType::Chromatic) {
                                n.pitch = MusicTheory::nextPitchInScale(n.pitch, rootKey_, scaleType_);
                            } else {
                                n.pitch = std::min(127, n.pitch + 1);
                            }
                        }
                    }
                    pushUndo(shift ? "Octave Up" : "Transpose Up", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
                else if (event.keyCode == NUIKeyCode::Down) {
                    auto oldNotes = notes_;
                    for (auto& n : notes_) {
                        if (n.selected && !n.isDeleted) {
                            if (shift) {
                                n.pitch = std::max(0, n.pitch - 12); // whole octave
                            } else if (snapToScale_ && scaleType_ != ScaleType::Chromatic) {
                                n.pitch = MusicTheory::previousPitchInScale(n.pitch, rootKey_, scaleType_);
                            } else {
                                n.pitch = std::max(0, n.pitch - 1);
                            }
                        }
                    }
                    pushUndo(shift ? "Octave Down" : "Transpose Down", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
            }
        }
    }
    return false;
}

void PianoRollNoteLayer::setTool(PianoRollTool tool) {
    tool_ = tool;
    // Reset interaction state if needed?
    state_ = State::None;
    repaint();
}

void PianoRollNoteLayer::pushUndo(const std::string& desc, const std::vector<MidiNote>& oldN, const std::vector<MidiNote>& newN) {
    PianoRollCommand cmd;
    cmd.description = desc;
    cmd.notesBefore = oldN;
    cmd.notesAfter = newN;
    undoStack_.push_back(cmd);
    redoStack_.clear();

    // Enforce Limits (Count & Memory)
    // 1. Hard count limit
    if (undoStack_.size() > 50) {
        undoStack_.erase(undoStack_.begin());
    }

    // 2. Memory Cap (100MB) - "Cockroach Chrysalis"
    // Calculate total size and evict from front (LRU)
    size_t totalBytes = 0;
    const size_t kMaxBytes = 100 * 1024 * 1024; // 100MB

    // Reverse iterate to count from newest (keep these)
    // Actually simpler to just calc total and pop front.
    for (const auto& c : undoStack_) {
        totalBytes += c.description.capacity();
        totalBytes += c.notesBefore.capacity() * sizeof(MidiNote);
        totalBytes += c.notesAfter.capacity() * sizeof(MidiNote);
    }

    while (totalBytes > kMaxBytes && !undoStack_.empty()) {
        const auto& c = undoStack_.front();
        size_t cmdSize = c.description.capacity() + 
                         c.notesBefore.capacity() * sizeof(MidiNote) + 
                         c.notesAfter.capacity() * sizeof(MidiNote);
        
        if (totalBytes >= cmdSize) totalBytes -= cmdSize; 
        else totalBytes = 0;

        undoStack_.erase(undoStack_.begin());
    }
}

void PianoRollNoteLayer::undo() {
    if (undoStack_.empty()) return;
    auto cmd = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back(cmd);
    
    notes_ = cmd.notesBefore;
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::redo() {
    if (redoStack_.empty()) return;
    auto cmd = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back(cmd);
    
    notes_ = cmd.notesAfter;
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::commitNotes() {
    // Sort logic to ensure efficient culling
    std::sort(notes_.begin(), notes_.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat < b.startBeat;
    });

    if (onNotesChanged_) {
        onNotesChanged_(notes_);
    }
}

void PianoRollNoteLayer::setNotes(const std::vector<MidiNote>& notes) {
    notes_ = notes;
    // Ensure sorted as well
    std::sort(notes_.begin(), notes_.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat < b.startBeat;
    });
    repaint();
}

void PianoRollNoteLayer::setGhostPatterns(const std::vector<GhostPattern>& ghosts) {
    ghostPatterns_ = ghosts;
    repaint();
}

void PianoRollNoteLayer::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollNoteLayer::setKeyHeight(float height) { keyHeight_ = std::max(8.0f, height); repaint(); }
void PianoRollNoteLayer::setScrollOffsetX(float offset) { scrollX_ = offset; repaint(); }
void PianoRollNoteLayer::setScrollOffsetY(float offset) { scrollY_ = offset; repaint(); }

void PianoRollNoteLayer::updateEdgeScrolling(float mouseX, float mouseY, const NUIRect& bounds, std::function<void()> syncCallback) {
    if (state_ == State::None) {
        isEdgeScrolling_ = false;
        edgeScrollDir_ = {0.0f, 0.0f};
        return;
    }

    float edgeL = mouseX - bounds.x;
    float edgeR = (bounds.x + bounds.width) - mouseX;
    float edgeT = mouseY - bounds.y;
    float edgeB = (bounds.y + bounds.height) - mouseY;

    float speedX = 0.0f;
    float speedY = 0.0f;

    if (edgeL < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeL / kEdgeThreshold), 0.0f, 1.0f);
        speedX = -kMaxScrollSpeed * t;
    } else if (edgeR < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeR / kEdgeThreshold), 0.0f, 1.0f);
        speedX = kMaxScrollSpeed * t;
    }

    if (edgeT < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeT / kEdgeThreshold), 0.0f, 1.0f);
        speedY = -kMaxScrollSpeed * t;
    } else if (edgeB < kEdgeThreshold) {
        const float t = std::clamp(1.0f - (edgeB / kEdgeThreshold), 0.0f, 1.0f);
        speedY = kMaxScrollSpeed * t;
    }

    if (std::abs(speedX) > 0.1f || std::abs(speedY) > 0.1f) {
        isEdgeScrolling_ = true;
        edgeScrollDir_ = {speedX, speedY};

        float totalH = 128.0f * keyHeight_;
        float maxScrollY = std::max(0.0f, totalH - bounds.height);
        float totalW = static_cast<float>(std::max(4.0, totalDurationBeats_)) * pixelsPerBeat_;
        float maxScrollX = std::max(0.0f, totalW - bounds.width);

        scrollX_ = safeClampScroll(scrollX_ + speedX, maxScrollX);
        scrollY_ = safeClampScroll(scrollY_ + speedY, maxScrollY);

        if (auto* view = dynamic_cast<PianoRollView*>(getParent())) {
            view->applyEdgeAutoScroll(scrollX_, scrollY_);
        }

        repaint();
        
        if (syncCallback) {
            syncCallback();
        }
    } else {
        isEdgeScrolling_ = false;
        edgeScrollDir_ = {0.0f, 0.0f};
    }
}
void PianoRollNoteLayer::setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb) { onNotesChanged_ = cb; }

void PianoRollNoteLayer::setPlatformBridge(NUIPlatformBridge* bridge) { platformBridge_ = bridge; }

// =============================================================================
// PianoRollControlPanel (Velocity)
// =============================================================================
// =============================================================================
// PianoRollControlPanel (Velocity + Settings)
// =============================================================================
PianoRollControlPanel::PianoRollControlPanel()
    : pixelsPerBeat_(80.0f), scrollX_(0.0f)
{
}

// REMOVED setupUI (Moved to Toolbar)

void PianoRollControlPanel::setPixelsPerBeat(float ppb) {
    if (std::abs(pixelsPerBeat_ - ppb) > 0.001f) {
        pixelsPerBeat_ = std::max(10.0f, ppb);
        repaint();
    }
}

void PianoRollControlPanel::setScrollX(float scrollX) {
    if (std::abs(scrollX_ - scrollX) > 0.001f) {
        scrollX_ = scrollX;
        repaint();
    }
}

void PianoRollControlPanel::setNoteLayer(std::shared_ptr<PianoRollNoteLayer> layer) {
    noteLayer_ = layer;
    repaint();
}

bool PianoRollControlPanel::onMouseEvent(const NUIMouseEvent& event) {
    // Note: If dropdown consumes event, we return true above.
    // Else check sidebar/velocity area.
    
    auto b = getBounds();
    
    // CRITICAL FIX: Ignore events outside bounds unless we are already dragging
    if (!b.contains(event.position) && !isDragging_) return false;

    constexpr float sidebarW = 76.0f;
    // Note Layer Interaction (Velocity)
    // We assume clicks in content area are for velocity
    // COPIED FROM OLD LOGIC
    auto layer = noteLayer_.lock();
    if (layer) {
        auto b = getBounds();
        // Sidebar: clicking it flips the lane between velocity and pan.
        if (!isDragging_ && event.position.x < b.x + sidebarW) {
             if (event.pressed && event.button == NUIMouseButton::Left) {
                 laneMode_ = (laneMode_ == LaneMode::Velocity) ? LaneMode::Pan : LaneMode::Velocity;
                 repaint();
                 return true;
             }
             return NUIComponent::onMouseEvent(event);
        }

        const bool panMode = laneMode_ == LaneMode::Pan;
        float localX = event.position.x - b.x + scrollX_ - sidebarW;

        // Find the note whose velocity stem sits under a given lane-x, preferring
        // a selected one. Shared by the initial press and the sweep-drag below.
        auto noteUnderX = [&](float lx) -> int {
            const auto& notes = layer->getNotes();
            int found = -1;
            for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
                if (notes[i].isDeleted) continue;
                float nStart = static_cast<float>(notes[i].startBeat * pixelsPerBeat_);
                float nEnd = static_cast<float>((notes[i].startBeat + notes[i].durationBeats) * pixelsPerBeat_);
                if (nEnd < nStart + 6.0f) nEnd = nStart + 6.0f; // lollipop head radius
                const bool hitHead = std::abs(nStart - lx) < 10.0f;
                const bool hitBody = (lx >= nStart - 2.0f && lx <= nEnd + 2.0f);
                if (hitHead || hitBody) {
                    found = i; // last (topmost) wins by default
                    if (notes[i].selected) return i; // but a selected note takes priority
                }
            }
            return found;
        };

        if (event.pressed && event.button == NUIMouseButton::Left) {
            int foundIdx = noteUnderX(localX);

            // Map the cursor height to the lane's value: velocity is unipolar
            // from the lane floor; pan is bipolar around the centre line
            // (top = right, bottom = left).
            const auto laneValueAtY = [&](float y) -> float {
                const float availH = std::max(1.0f, b.height - 28.0f);
                const float bottomY = b.bottom() - 8.0f;
                if (panMode) {
                    const float centerY = bottomY - availH * 0.5f;
                    return std::clamp((centerY - y) / std::max(1.0f, availH * 0.5f), -1.0f, 1.0f);
                }
                return velocityFromPanelPosition(y, bottomY, availH);
            };

            if (foundIdx != -1) {
                isDragging_ = true;
                hoveringNoteIndex_ = foundIdx;
                dragStartPos_ = event.position;
                dragUndoSnapshot_ = layer->getNotes(); // baseline for one undo step

                // Set the value immediately based on click Y (Global)
                const float newValue = laneValueAtY(event.position.y);

                auto modNotes = layer->getNotes();
                if (panMode) modNotes[foundIdx].pan = newValue;
                else modNotes[foundIdx].velocity = newValue;

                // Single Edit Only (Batch removed)

                layer->setNotes(modNotes);
                repaint();
                return true;
            }
        }
        else if (isDragging_) {
            // Drag logic
            if (event.released) {
                 isDragging_ = false;
                 hoveringNoteIndex_ = -1;
                 // Fold the whole lane drag into a single undo step.
                 if (!dragUndoSnapshot_.empty()) {
                     layer->pushExternalEdit(dragUndoSnapshot_, panMode ? "Pan" : "Velocity");
                     dragUndoSnapshot_.clear();
                 }
                 return true;
            }

            // Sweep-paint: as the cursor moves across the lane, the note under it
            // takes the cursor's height, so dragging draws a ramp/curve.
            // When between stems, keep painting the last note so quick sweeps
            // don't drop out.
            const float availH = std::max(1.0f, b.height - 28.0f);
            const float bottomY = b.bottom() - 8.0f;
            float newValue;
            if (panMode) {
                const float centerY = bottomY - availH * 0.5f;
                newValue = std::clamp((centerY - event.position.y) / std::max(1.0f, availH * 0.5f),
                                      -1.0f, 1.0f);
            } else {
                newValue = velocityFromPanelPosition(event.position.y, bottomY, availH);
            }

            const int swept = noteUnderX(localX);
            if (swept != -1) hoveringNoteIndex_ = swept;

            auto modNotes = layer->getNotes();
            if (hoveringNoteIndex_ >= 0 && static_cast<size_t>(hoveringNoteIndex_) < modNotes.size()) {
                 if (panMode) modNotes[hoveringNoteIndex_].pan = newValue;
                 else modNotes[hoveringNoteIndex_].velocity = newValue;
                 layer->setNotes(modNotes);
                 repaint();
            }
            return true;
        }
    }
    
    return NUIComponent::onMouseEvent(event);
}

void PianoRollControlPanel::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    
    const auto panelBg = themeManager.getColor("backgroundSecondary").darkened(0.025f);
    const auto border = themeManager.getColor("border").withAlpha(0.52f);
    renderer.fillRect(b, panelBg);
    renderer.drawLine(NUIPoint(b.x, b.y), NUIPoint(b.right(), b.y), 1.0f, border);
    
    // Sidebar Area (Left)
    constexpr float sidebarW = 76.0f;
    NUIRect sidebarRect(b.x, b.y, sidebarW, b.height);
    
    renderer.fillRect(sidebarRect, themeManager.getColor("backgroundPrimary").withAlpha(0.72f));
    renderer.drawLine(NUIPoint(sidebarRect.right(), sidebarRect.y),
                      NUIPoint(sidebarRect.right(), sidebarRect.bottom()),
                      1.0f,
                      border);

    // Stacked mode tabs — the active lane reads accent, the other dim; a click
    // anywhere on the sidebar swaps them (handled in onMouseEvent).
    const bool panMode = laneMode_ == LaneMode::Pan;
    const float labelSize = themeManager.getFontSize("xs");
    const auto activeColor = themeManager.getColor("accentPrimary").withAlpha(0.95f);
    const auto inactiveColor = themeManager.getColor("textSecondary").withAlpha(0.42f);
    const auto velDim = renderer.measureText("VELOCITY", labelSize);
    renderer.drawText("VELOCITY",
                      NUIPoint(b.x + (sidebarW - velDim.width) * 0.5f, b.y + 13.0f),
                      labelSize,
                      panMode ? inactiveColor : activeColor);
    const auto panDim = renderer.measureText("PAN", labelSize);
    renderer.drawText("PAN",
                      NUIPoint(b.x + (sidebarW - panDim.width) * 0.5f, b.y + 31.0f),
                      labelSize,
                      panMode ? activeColor : inactiveColor);
    const char* rangeText = panMode ? "L - C - R" : "MIDI 0 - 127";
    const auto rangeDim = renderer.measureText(rangeText, 8.0f);
    renderer.drawText(rangeText,
                      NUIPoint(b.x + (sidebarW - rangeDim.width) * 0.5f, b.y + 49.0f),
                      8.0f,
                      themeManager.getColor("textSecondary").withAlpha(0.48f));
    
    auto layer = noteLayer_.lock();
    if (!layer || !isVisible()) return;

    // Content Area Clip
    NUIRect contentRect(b.x + sidebarW, b.y, b.width - sidebarW, b.height);
    renderer.setClipRect(contentRect);

    constexpr int beatsPerBar = 4;
    const float startX = contentRect.x;
    renderTimelineGrid(renderer, contentRect, startX, contentRect.right(), scrollX_, pixelsPerBeat_, beatsPerBar);

    const float availH = std::max(1.0f, b.height - 28.0f);
    const float bottomY = b.bottom() - 8.0f;
    const float topY = bottomY - availH;
    const auto guideColor = themeManager.getColor("border").withAlpha(0.11f);
    renderer.drawLine(NUIPoint(contentRect.x, topY), NUIPoint(contentRect.right(), topY), 1.0f, guideColor);
    renderer.drawLine(NUIPoint(contentRect.x, topY + availH * 0.5f),
                      NUIPoint(contentRect.right(), topY + availH * 0.5f),
                      1.0f,
                      guideColor.withAlpha(0.72f));
    renderer.drawLine(NUIPoint(contentRect.x, bottomY), NUIPoint(contentRect.right(), bottomY), 1.0f, guideColor);

    const float patternEndX = startX + static_cast<float>(layer->getTotalDurationBeats() * pixelsPerBeat_) - scrollX_;
    if (patternEndX < contentRect.right()) {
        const float shadeX = std::max(contentRect.x, patternEndX);
        renderer.fillRect(NUIRect(shadeX, contentRect.y, contentRect.right() - shadeX, contentRect.height),
                          NUIColor::black().withAlpha(0.58f));
    }
    if (patternEndX >= contentRect.x && patternEndX <= contentRect.right()) {
        renderer.drawLine(NUIPoint(patternEndX, contentRect.y),
                          NUIPoint(patternEndX, contentRect.bottom()),
                          2.0f,
                          themeManager.getColor("accentPrimary").withAlpha(0.58f));
    }
    
    // 2. Render the lane stems: velocity rises from the floor; pan hangs off
    //    the centre line (up = right, down = left).
    const auto& notes = layer->getNotes();
    auto velColorBase = themeManager.getColor("accentPrimary").lightened(0.05f);
    const float centerY = bottomY - availH * 0.5f;

    for (size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const auto& n = notes[noteIndex];
        if (n.isDeleted && n.animationScale < 0.01f) continue;

        float x = startX + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;

        // Skip if out of view
        if (x > b.x + b.width) continue;

        const float normalizedVelocity = std::clamp(n.velocity, 0.0f, 1.0f);
        float y;
        float stemTop;
        float stemH;
        float intensity; // drives the stem alpha
        if (panMode) {
            const float pv = std::clamp(n.pan, -1.0f, 1.0f);
            y = centerY - pv * availH * 0.5f;
            stemTop = std::min(y, centerY);
            stemH = std::max(2.0f, std::abs(y - centerY));
            intensity = std::abs(pv);
        } else {
            const float h = velocityToPanelHeight(normalizedVelocity, availH);
            y = bottomY - h;
            stemTop = y;
            stemH = std::max(2.0f, h);
            intensity = normalizedVelocity;
        }

        float alpha = 0.48f + intensity * 0.48f;
        auto col = velColorBase.withAlpha(alpha);
        if (n.selected) col = themeManager.getColor("accentSecondary").withAlpha(0.92f);

        renderer.fillRoundedRect(NUIRect(x - 2.0f, stemTop, 4.0f, stemH), 2.0f, col.withAlpha(alpha * 0.78f));

        const float handleSize = n.selected ? 7.0f : 6.0f;
        NUIRect handleRect(x - handleSize * 0.5f, y - handleSize * 0.5f, handleSize, handleSize);
        renderer.fillRoundedRect(handleRect, 2.0f, col);
        renderer.strokeRoundedRect(handleRect,
                                   2.0f,
                                   1.0f,
                                   NUIColor::white().withAlpha(n.selected ? 0.48f : 0.20f));

        float w = static_cast<float>(n.durationBeats * pixelsPerBeat_);
        if (w > 4.0f) {
            renderer.drawLine(NUIPoint(x, y), NUIPoint(x + w, y), n.selected ? 2.0f : 1.0f, col.withAlpha(0.64f));
        }

        if (isDragging_ && static_cast<int>(noteIndex) == hoveringNoteIndex_) {
            std::string value;
            if (panMode) {
                const int panSteps = static_cast<int>(std::lround(std::abs(n.pan) * 100.0f));
                if (panSteps == 0) value = "C";
                else value = (n.pan < 0.0f ? "L " : "R ") + std::to_string(panSteps);
            } else {
                value = std::to_string(static_cast<int>(std::lround(normalizedVelocity * 127.0f)));
            }
            const float fontSize = 9.0f;
            const auto textSize = renderer.measureText(value, fontSize);
            const float bubbleWidth = textSize.width + 10.0f;
            const float bubbleY = y - 18.0f >= contentRect.y ? y - 18.0f : y + 8.0f;
            const NUIRect bubble(x + 7.0f, bubbleY, bubbleWidth, 16.0f);
            renderer.fillRoundedRect(bubble, 4.0f, NUIColor::black().withAlpha(0.92f));
            renderer.strokeRoundedRect(bubble, 4.0f, 1.0f, col.withAlpha(0.82f));
            renderer.drawText(value,
                              NUIPoint(bubble.x + 5.0f, bubble.y + (bubble.height - textSize.height) * 0.5f),
                              fontSize,
                              NUIColor::white().withAlpha(0.92f));
        }
    }
    
    renderer.clearClipRect();
}
// ...






// =============================================================================
// PianoRollView
// =============================================================================
PianoRollView::PianoRollView()
    : m_keyLaneWidth(76.0f), m_rulerHeight(28.0f), m_pixelsPerBeat(80.0f), m_keyHeight(24.0f),
      m_scrollX(0.0f), m_targetScrollX(0.0f)
{
    // [FIX] Canonical default octave: C3 (MIDI 48).
    // scrollY = (127 - targetPitch) * keyHeight => (127 - 48) * 24 = 1896
    m_scrollY = 1896.0f;
    m_targetScrollY = 1896.0f;
    m_keys = std::make_shared<PianoRollKeyLane>();
    m_ruler = std::make_shared<PianoRollRuler>();
    m_grid = std::make_shared<PianoRollGrid>();
    m_notes = std::make_shared<PianoRollNoteLayer>();
    m_controls = std::make_shared<PianoRollControlPanel>();

    m_notes->setOnHoveredPitchChanged([this](int pitch) {
        if (m_grid) m_grid->setHoveredPitch(pitch);
        if (m_keys) m_keys->setHoveredKey(pitch);
    });
    m_keys->setOnHoveredKeyChanged([this](int pitch) {
        if (m_grid) m_grid->setHoveredPitch(pitch);
    });
    
    // Toolbar
    m_toolbar = std::make_shared<PianoRollToolbar>();
    m_toolbar->setGrid(m_grid);
    m_toolbar->setNoteLayer(m_notes);
    m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
    m_toolbar->setOnShowShortcutHelp([this]() {
        m_showShortcutHelp = !m_showShortcutHelp;
        repaint();
    });

    m_controls->setNoteLayer(m_notes);
    
    m_minimap = std::make_shared<PianoRollMinimap>(); // Local Minimap
    
    m_vScroll = std::make_shared<NUIScrollbar>(NUIScrollbar::Orientation::Vertical);
    m_vScroll->setOrientation(NUIScrollbar::Orientation::Vertical);
    {
        auto& theme = NUIThemeManager::getInstance();
        m_vScroll->setArrowSize(0.0f);
        m_vScroll->setBorderWidth(0.0f);
        m_vScroll->setBorderRadius(8.0f);
        m_vScroll->setTrackColor(theme.getColor("surfaceRaised").withAlpha(0.55f));
        m_vScroll->setThumbColor(theme.getColor("textPrimary").withAlpha(0.30f));
        m_vScroll->setThumbHoverColor(theme.getColor("textPrimary").withAlpha(0.48f));
        m_vScroll->setThumbPressedColor(theme.getColor("accentPrimary").withAlpha(0.68f));
        m_vScroll->setMinimumThumbSize(0.06);
    }

    // Initial default layout config
    m_minimap->setVisible(true);
    m_vScroll->setVisible(true);
    
    // Ruler Zoom Callback
    m_ruler->onZoomRequested = [this](float delta, float mouseX) {
        float oldPPB = m_pixelsPerBeat;
        float zoomFactor = (delta > 0) ? 1.15f : 0.85f;
        float newPPB = std::clamp(oldPPB * zoomFactor, 10.0f, 500.0f);
        
        // Anchor logic: Keep beat under mouse stationary
        float mouseBeat = (m_scrollX + mouseX) / oldPPB;
        float newWorldX = mouseBeat * newPPB;
        float newScrollX = newWorldX - mouseX;
        
        if (newScrollX < 0) newScrollX = 0;
        
        m_pixelsPerBeat = newPPB;
        m_scrollX = newScrollX;
        m_targetScrollX = newScrollX;
        
        updateScrollbars();
        syncChildren();
    };

    m_ruler->onPlayheadScrubbed = [this](double beat, bool active) {
        const double clampedBeat = std::clamp(beat, 0.0, m_totalDurationBeats);
        setPlayheadBeat(clampedBeat, false);
        if (m_onPlayheadScrubbed) {
            m_onPlayheadScrubbed(clampedBeat, active);
        }
    };

    m_minimap->onViewChanged = [this](double start, double duration) {
        m_scrollX = static_cast<float>(start * m_pixelsPerBeat);
        m_targetScrollX = m_scrollX;
        
        // ZOOM LOGIC: 
        // duration * ppb = visibleWidth
        // ppb = visibleWidth / duration
        float visibleW = m_grid->getWidth();
        if (duration > 0.001) {
             m_pixelsPerBeat = visibleW / static_cast<float>(duration);
        }
        
        syncChildren();
    };
    
    m_vScroll->setOnScroll([this](double val) {
        float totalH = 128 * m_keyHeight;
        float visibleH = m_grid->getHeight();
        float maxScroll = std::max(0.0f, totalH - visibleH);
        m_targetScrollY = safeClampRange(static_cast<float>(val), 0.0f, maxScroll);
    });
    
    addChild(m_keys);
    addChild(m_ruler);
    addChild(m_grid);
    addChild(m_notes);
    addChild(m_controls);
    addChild(m_minimap);
    addChild(m_vScroll);
    addChild(m_toolbar); // Top (Render Last)
}

void PianoRollView::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto bounds = getBounds();
    renderer.fillRect(bounds, theme.getColor("backgroundPrimary"));

    const float toolbarH = 50.0f;
    const float minimapH = m_showLocalMinimap ? 28.0f : 0.0f;
    const float rulerH = 28.0f;
    const NUIRect pitchHeader(bounds.x,
                              bounds.y + toolbarH,
                              m_keyLaneWidth,
                              minimapH + rulerH);
    renderer.fillRect(pitchHeader, theme.getColor("backgroundSecondary").darkened(0.025f));
    renderer.drawLine(NUIPoint(pitchHeader.right() - 0.5f, pitchHeader.y),
                      NUIPoint(pitchHeader.right() - 0.5f, pitchHeader.bottom()),
                      1.0f,
                      theme.getColor("border").withAlpha(0.54f));
    renderer.drawLine(NUIPoint(pitchHeader.x, pitchHeader.bottom() - 0.5f),
                      NUIPoint(pitchHeader.right(), pitchHeader.bottom() - 0.5f),
                      1.0f,
                      theme.getColor("border").withAlpha(0.54f));
    const auto pitchLabelSize = renderer.measureText("PITCH", 9.0f);
    renderer.drawText("PITCH",
                      NUIPoint(pitchHeader.x + (pitchHeader.width - pitchLabelSize.width) * 0.5f,
                               pitchHeader.bottom() - rulerH + (rulerH - pitchLabelSize.height) * 0.5f),
                      9.0f,
                      theme.getColor("textSecondary").withAlpha(0.62f));

    NUIComponent::onRender(renderer);

    // Draw playhead
    if (m_grid && m_ruler) {
        const auto gridBounds = m_grid->getBounds();
        const auto rulerBounds = m_ruler->getBounds();
        const auto accent = theme.getColor("accentPrimary");
        const double visibleStartBeat = getViewStartBeat();
        const double visibleEndBeat = visibleStartBeat + getViewDurationBeats();
        const float playheadX = snapVerticalLineX(
            beatToScreenX(m_playheadBeat, m_pixelsPerBeat, m_scrollX, gridBounds.x));

        if (m_playheadBeat >= visibleStartBeat && m_playheadBeat <= visibleEndBeat &&
            playheadX >= gridBounds.x && playheadX <= gridBounds.right()) {
            const float playheadStartY = rulerBounds.bottom();
            const float playheadEndY = m_controls ? m_controls->getBounds().bottom() : gridBounds.bottom();
            if (m_isPlayingCallback && m_isPlayingCallback()) {
                const float glowWidth = 4.0f;
                const float lineHeight = playheadEndY - playheadStartY;
                renderer.fillRectGradient(NUIRect(playheadX - glowWidth, playheadStartY, glowWidth, lineHeight),
                                          accent.withAlpha(0.0f),
                                          accent.withAlpha(0.14f),
                                          false);
                renderer.fillRectGradient(NUIRect(playheadX, playheadStartY, glowWidth, lineHeight),
                                          accent.withAlpha(0.14f),
                                          accent.withAlpha(0.0f),
                                          false);
            }
            renderer.drawLine(NUIPoint(playheadX, playheadStartY),
                              NUIPoint(playheadX, playheadEndY),
                              1.0f,
                              accent.withAlpha(0.55f));
        }
    }

    if (m_controls) {
        const auto controlBounds = m_controls->getBounds();
        const float gripWidth = 34.0f;
        const NUIRect splitterGrip(controlBounds.x + (controlBounds.width - gripWidth) * 0.5f,
                                   controlBounds.y - 2.0f,
                                   gripWidth,
                                   4.0f);
        renderer.fillRoundedRect(splitterGrip,
                                 2.0f,
                                 theme.getColor("accentPrimary").withAlpha(
                                     m_isResizingPanel ? 0.92f : (m_splitterHovered ? 0.52f : 0.22f)));
    }

    if (m_toolbar) {
        if (auto menu = m_toolbar->getActiveContextMenu(); menu && menu->isVisible()) {
            menu->onRender(renderer);
        }
    }

    if (m_showShortcutHelp) {
        renderShortcutHelp(renderer);
    }
}

void PianoRollView::renderShortcutHelp(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto bounds = getBounds();
    const float toolbarH = 50.0f;
    const NUIRect area(bounds.x, bounds.y + toolbarH, bounds.width, bounds.height - toolbarH);

    // Dim the editors underneath so the sheet reads as modal.
    renderer.fillRect(area, theme.getColor("backgroundPrimary").withAlpha(0.62f));

    struct Entry { const char* keys; const char* action; };
    static constexpr Entry kMouse[] = {
        {"Drag", "draw a note, keep dragging for length"},
        {"Shift+Drag", "paint a run of notes"},
        {"Ctrl+Drag", "marquee select (any tool)"},
        {"Alt+Drag", "clone the selection"},
        {"Alt mid-drag", "bypass snap for fine moves"},
        {"Wheel on note", "adjust velocity"},
        {"Right-Click", "erase"},
        {"Double-Click", "add note / note properties"},
        {"Lane sidebar", "switch velocity / pan lane"},
    };
    static constexpr Entry kKeys[] = {
        {"Q", "quantize note starts"},
        {"Ctrl+G", "glue same-pitch runs"},
        {"Ctrl+L", "connect notes (legato)"},
        {"Ctrl+Z / Y", "undo / redo"},
        {"Ctrl+C / V / D", "copy / paste / duplicate"},
        {"Ctrl+A", "select all"},
        {"Arrows", "nudge / transpose"},
        {"Shift+Left/Right", "resize by grid"},
        {"Shift+Up/Down", "octave jump"},
        {"Delete", "remove selection"},
    };
    constexpr size_t kMouseCount = sizeof(kMouse) / sizeof(kMouse[0]);
    constexpr size_t kKeysCount = sizeof(kKeys) / sizeof(kKeys[0]);
    constexpr size_t kRows = kMouseCount > kKeysCount ? kMouseCount : kKeysCount;

    const float rowH = 19.0f;
    const float padX = 24.0f;
    const float padY = 18.0f;
    const float titleH = 18.0f;
    const float sectionH = 16.0f;
    const float footerH = 14.0f;
    const float colGap = 28.0f;
    const float keyColW = 118.0f;
    const float panelW = std::min(660.0f, area.width - 32.0f);
    const float panelH = padY + titleH + 12.0f + sectionH + kRows * rowH + 14.0f + footerH + padY;
    const NUIRect panel(area.x + (area.width - panelW) * 0.5f,
                        area.y + std::max(8.0f, (area.height - panelH) * 0.5f),
                        panelW,
                        panelH);

    renderer.drawShadow(panel, 0.0f, 6.0f, 26.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.45f));
    renderer.fillRoundedRect(panel, 10.0f, theme.getColor("backgroundSecondary").withAlpha(0.98f));
    renderer.strokeRoundedRect(panel, 10.0f, 1.0f, theme.getColor("border").withAlpha(0.7f));

    const auto accent = theme.getColor("accentPrimary");
    const auto keyColor = theme.getColor("textPrimary").withAlpha(0.95f);
    const auto actionColor = theme.getColor("textSecondary").withAlpha(0.92f);

    float y = panel.y + padY;
    const char* title = "PIANO ROLL SHORTCUTS";
    const auto titleSize = renderer.measureText(title, 11.0f);
    renderer.drawText(title,
                      NUIPoint(panel.x + (panel.width - titleSize.width) * 0.5f, y),
                      11.0f,
                      accent.withAlpha(0.95f));
    y += titleH + 12.0f;

    const float colW = (panelW - padX * 2.0f - colGap) * 0.5f;
    const float leftX = panel.x + padX;
    const float rightX = leftX + colW + colGap;

    renderer.drawText("MOUSE", NUIPoint(leftX, y), 9.0f, actionColor.withAlpha(0.6f));
    renderer.drawText("KEYS", NUIPoint(rightX, y), 9.0f, actionColor.withAlpha(0.6f));
    y += sectionH;

    for (size_t i = 0; i < kRows; ++i) {
        const float rowY = y + i * rowH;
        if (i < kMouseCount) {
            renderer.drawText(kMouse[i].keys, NUIPoint(leftX, rowY), 10.5f, keyColor);
            renderer.drawText(kMouse[i].action, NUIPoint(leftX + keyColW, rowY), 10.5f, actionColor);
        }
        if (i < kKeysCount) {
            renderer.drawText(kKeys[i].keys, NUIPoint(rightX, rowY), 10.5f, keyColor);
            renderer.drawText(kKeys[i].action, NUIPoint(rightX + keyColW, rowY), 10.5f, actionColor);
        }
    }
    y += kRows * rowH + 14.0f;

    const char* footer = "Chord & Strum live in the toolbar menu. F1 toggles this sheet; any key or click closes it.";
    const auto footerSize = renderer.measureText(footer, 9.5f);
    renderer.drawText(footer,
                      NUIPoint(panel.x + (panel.width - footerSize.width) * 0.5f, y),
                      9.5f,
                      actionColor.withAlpha(0.7f));
}

void PianoRollView::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    layoutChildren();
}

void PianoRollView::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);

    const float totalH = 128 * m_keyHeight;
    const float visibleH = m_grid ? m_grid->getHeight() : 0.0f;
    const float maxScrollY = std::max(0.0f, totalH - visibleH);
    m_targetScrollY = safeClampRange(m_targetScrollY, 0.0f, maxScrollY);
    m_targetScrollX = std::max(0.0f, m_targetScrollX);

    bool changed = false;
    const float ease = 1.0f - std::exp(-static_cast<float>(deltaTime) * 18.0f);

    const float dx = m_targetScrollX - m_scrollX;
    if (std::abs(dx) > 0.1f) {
        m_scrollX += dx * ease;
        changed = true;
    } else if (std::abs(dx) > 0.0f) {
        m_scrollX = m_targetScrollX;
        changed = true;
    }

    const float dy = m_targetScrollY - m_scrollY;
    if (std::abs(dy) > 0.1f) {
        m_scrollY += dy * ease;
        changed = true;
    } else if (std::abs(dy) > 0.0f) {
        m_scrollY = m_targetScrollY;
        changed = true;
    }

    if (changed) {
        updateScrollbars();
        syncChildren();
    }
}

void PianoRollView::layoutChildren() {
    auto b = getBounds();
    float sbSize = 14.0f; 
    
    // 0. Toolbar (Standardized Aestra UI Height)
    float toolbarH = 50.0f;
    if (m_toolbar) m_toolbar->setBounds(NUIRect(b.x, b.y, b.width, toolbarH));
    
    // 1. Scrollbar/Minimap Section (Below Toolbar)
    float miniMapH = m_showLocalMinimap ? 28.0f : 0.0f;
    
    // 2. Ruler Section (Below Minimap if present)
    float rulerH = 28.0f;
    
    float topTotalH = toolbarH + miniMapH + rulerH;
    
    float keyW = std::max(40.0f, m_keyLaneWidth);
    float contentW = std::max(0.0f, b.width - keyW - sbSize);
    float contentH = std::max(0.0f, b.height - topTotalH - m_controlPanelHeight); // Subtract control panel
    
    // 1. Minimap (Top)
    m_minimap->setVisible(m_showLocalMinimap);
    if (m_showLocalMinimap) {
        m_minimap->setBounds(NUIRect(b.x + keyW, b.y + toolbarH, contentW, miniMapH));
    }
    
    // 2. Ruler
    m_ruler->setBounds(NUIRect(b.x + keyW, b.y + toolbarH + miniMapH, contentW, rulerH));
    
    // 3. Grid/Notes (Below Ruler)
    NUIRect contentRect(b.x + keyW, b.y + topTotalH, contentW, contentH);
    m_grid->setBounds(contentRect);
    m_notes->setBounds(contentRect);
    
    // 4. Keys (Left, spans Grid height)
    m_keys->setBounds(NUIRect(b.x, b.y + topTotalH, keyW, contentH));
    
    // 5. V-Scroll (Right, spans Grid height only)
    m_vScroll->setBounds(NUIRect(b.x + b.width - sbSize, b.y + topTotalH, sbSize, contentH));
    
    // 6. Control Panel (Bottom) - Spans Full Width (Keys + Content)
    // Ensures "Control" sidebar aligns with Keys
    m_controls->setBounds(NUIRect(b.x, b.y + topTotalH + contentH, b.width, m_controlPanelHeight));
    
    updateScrollbars();
    syncChildren();
}

void PianoRollView::updateScrollbars() {
    float totalBeats = static_cast<float>(std::max(4.0, m_totalDurationBeats));
    float visibleW = m_grid->getWidth();
    double viewDur = visibleW / m_pixelsPerBeat;
    double start = m_scrollX / m_pixelsPerBeat;
    
    if (m_minimap && m_showLocalMinimap) {
        m_minimap->setTotalDuration(totalBeats);
        m_minimap->setView(start, viewDur);
    }

    // Vertical
    float totalH = 128 * m_keyHeight;
    float visibleH = m_grid->getHeight();
    
    m_vScroll->setRangeLimit(0.0, totalH);
    m_vScroll->setCurrentRange(m_scrollY, visibleH);
}

void PianoRollView::syncChildren() {
    if (!m_keys) return;
    
    float x = m_scrollX;
    float y = m_scrollY; 
    
    m_keys->setScrollOffsetY(y);
    m_keys->setKeyHeight(m_keyHeight);
    
    m_ruler->setScrollX(x);
    m_ruler->setPixelsPerBeat(m_pixelsPerBeat);
    m_ruler->setPlayheadBeat(m_playheadBeat);

    m_grid->setPixelsPerBeat(m_pixelsPerBeat);
    m_grid->setKeyHeight(m_keyHeight);
    m_grid->setScrollOffsetX(x);
    m_grid->setScrollOffsetY(y);
    m_grid->setPlayheadBeat(m_playheadBeat);
    m_grid->setTotalDurationBeats(m_totalDurationBeats);
    
    m_notes->setPixelsPerBeat(m_pixelsPerBeat);
    m_notes->setKeyHeight(m_keyHeight);
    m_notes->setScrollOffsetX(m_scrollX);
    m_notes->setScrollOffsetY(m_scrollY);
    m_notes->setPlayheadBeat(m_playheadBeat);
    m_notes->setTotalDurationBeats(m_totalDurationBeats);
    m_notes->setPlaying(m_isPlayingCallback && m_isPlayingCallback());

    if (m_controls) {
        m_controls->setPixelsPerBeat(m_pixelsPerBeat);
        m_controls->setScrollX(m_scrollX);
    }

    if (m_minimap && m_showLocalMinimap) {
        m_minimap->setPlayheadBeat(m_playheadBeat);
    }
    
}

bool PianoRollView::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position) && !m_isResizingPanel) return false;

    // The shortcut sheet captures the pointer: any click or scroll dismisses it
    // instead of reaching the editors underneath.
    if (m_showShortcutHelp) {
        if (event.pressed || event.wheelDelta != 0.0f) {
            m_showShortcutHelp = false;
            repaint();
        }
        return true;
    }

    if (m_toolbar) {
        if (auto menu = m_toolbar->getActiveContextMenu(); menu && menu->isVisible()) {
            if (menu->onMouseEvent(event)) return true;

            if (event.pressed && event.button == NUIMouseButton::Left) {
                const auto menuBounds = menu->getGlobalBounds();
                const auto toolbarBounds = m_toolbar->getBounds();
                if (!menuBounds.contains(event.position) &&
                    !toolbarBounds.contains(event.position)) {
                    m_toolbar->dismissActiveContextMenu();
                    return true;
                }
            }
        }
    }

    auto b = getBounds();
    float splitterY = b.y + b.height - m_controlPanelHeight;
    float splitterZone = 7.0f;
    const bool splitterHovered = std::abs(event.position.y - splitterY) < splitterZone;
    if (m_splitterHovered != splitterHovered) {
        m_splitterHovered = splitterHovered;
        repaint();
    }
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (std::abs(event.position.y - splitterY) < splitterZone) {
            m_isResizingPanel = true;
            m_dragStartPos = event.position;
            m_dragStartPanelHeight = m_controlPanelHeight;
            return true;
        }
    }
    else if (m_isResizingPanel && !event.released) {
        float dy = event.position.y - m_dragStartPos.y;
        // Dragging UP increases height
        float newH = m_dragStartPanelHeight - dy;
        const float maxPanelHeight = std::max(20.0f, b.height * 0.5f);
        m_controlPanelHeight = std::min(std::max(newH, 20.0f), maxPanelHeight);
        layoutChildren();
        return true;
    }
    else if (event.released) {
        if (m_isResizingPanel) {
            m_isResizingPanel = false;
            return true;
        }
    }

    // 1. Give children priority (Ruler, Minimap, Grid, Notes)
    if (NUIComponent::onMouseEvent(event)) return true;

    // 2. View-level fallback for Grid Scrolling (if Grid didn't handle it)
    if (event.wheelDelta != 0.0f) {
        bool shift = (event.modifiers & NUIModifiers::Shift) || (event.modifiers & NUIModifiers::CapsLock);
        bool ctrl = (event.modifiers & NUIModifiers::Ctrl);
        
        if (ctrl) {
            // Zoom (Fallback)
            m_pixelsPerBeat = std::max(20.0f, m_pixelsPerBeat + event.wheelDelta * 5.0f);
        } else if (shift) {
            // H-Scroll
            m_targetScrollX = std::max(0.0f, m_targetScrollX - event.wheelDelta * 40.0f);
        } else {
            // V-Scroll
            float totalH = 128 * m_keyHeight;
            float visibleH = m_grid->getHeight();
            float maxScroll = std::max(0.0f, totalH - visibleH);
            
            float newY = m_targetScrollY - event.wheelDelta * 30.0f;
            m_targetScrollY = safeClampRange(newY, 0.0f, maxScroll);
        }
        
        if (ctrl) {
            updateScrollbars(); 
            syncChildren();
        }
        return true;
    }
    
    return false;
}

bool PianoRollView::onKeyEvent(const NUIKeyEvent& event) {
    if (event.pressed && event.keyCode == NUIKeyCode::F1) {
        m_showShortcutHelp = !m_showShortcutHelp;
        repaint();
        return true;
    }
    if (m_showShortcutHelp && event.pressed) {
        // While the sheet is up, any key dismisses it rather than editing the
        // notes it covers.
        m_showShortcutHelp = false;
        repaint();
        return true;
    }
    if (m_notes->onKeyEvent(event)) return true;
    return NUIComponent::onKeyEvent(event);
}

void PianoRollView::setNotes(const std::vector<MidiNote>& notes) {
    m_notes->setNotes(notes);
    if (m_minimap) {
        m_minimap->setNotes(notes);
    }
}

void PianoRollView::setGhostPatterns(const std::vector<PianoRollNoteLayer::GhostPattern>& ghosts) {
    m_notes->setGhostPatterns(ghosts);
}

const std::vector<MidiNote>& PianoRollView::getNotes() const {
    return m_notes->getNotes();
}

void PianoRollView::setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb) {
    m_notes->setOnNotesChanged([this, cb = std::move(cb)](const std::vector<MidiNote>& notes) {
        if (m_minimap) {
            m_minimap->setNotes(notes);
        }
        if (cb) cb(notes);
    });
}

void PianoRollView::setIsPlayingCallback(std::function<bool()> cb) {
    m_isPlayingCallback = std::move(cb);
}

void PianoRollView::setOnPreviewNote(std::function<void(int pitch, int velocity)> cb) {
    if (m_keys) {
        m_keys->setOnPreviewNote(cb);
        m_keys->setIsPlayingFromParent(m_isPlayingCallback);
    }
    if (m_notes) {
        // The note layer auditions pitches while notes are placed and dragged,
        // through the same synth path the key lane uses.
        m_notes->setOnPreviewNote(std::move(cb));
        m_notes->setIsPlayingCallback(m_isPlayingCallback);
    }
}

void PianoRollView::setDefaultUnitId(uint64_t unitId) {
    m_notes->setDefaultUnitId(unitId);
}

void PianoRollView::setPixelsPerBeat(float ppb) {
    m_pixelsPerBeat = ppb;
    updateScrollbars();
    syncChildren();
}

void PianoRollView::setBeatsPerBar(int bpb) {
    if (m_grid) m_grid->setBeatsPerBar(bpb);
    if (m_ruler) m_ruler->setBeatsPerBar(bpb);
    if (m_notes) m_notes->setBeatsPerBar(bpb);
}

void PianoRollView::setTool(GlobalTool tool) {
    if (m_notes) m_notes->setTool(tool);
}

void PianoRollView::setScale(int root, ScaleType type) {
    if (m_grid) {
        m_grid->setRootKey(root);
        m_grid->setScaleType(type);
    }
    if (m_notes) {
        m_notes->setRootKey(root);
        m_notes->setScaleType(type);
    }
}

void PianoRollView::setSnapToScale(bool enabled) {
    if (m_notes) {
        m_notes->setSnapToScale(enabled);
    }
}

void PianoRollView::setPlatformBridge(NUIPlatformBridge* bridge) {
    if (m_notes) m_notes->setPlatformBridge(bridge);
}

void PianoRollView::setPatternName(const std::string& name) {
    if (m_toolbar) m_toolbar->setPatternName(name);
}

void PianoRollView::setPatternChoices(const std::vector<PianoRollToolbar::PatternChoice>& choices, int selectedValue) {
    if (m_toolbar) {
        m_toolbar->setPatternChoices(choices, selectedValue);
    }
}

void PianoRollView::setPatternLengthBeats(double beats) {
    m_patternLengthBeats = std::max(8.0, beats);
    if (m_toolbar) {
        m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
    }
    if (std::abs(m_totalDurationBeats - m_patternLengthBeats) > 0.001) {
        setTotalDurationBeats(m_patternLengthBeats);
    } else {
        repaint();
    }
}

void PianoRollView::setPlayheadBeat(double beat, bool follow) {
    m_playheadBeat = std::max(0.0, beat);

    if (follow && m_grid) {
        const float visibleW = m_grid->getWidth();
        if (visibleW > 0.0f) {
            const double visibleStart = static_cast<double>(m_scrollX) / m_pixelsPerBeat;
            const double visibleDur = static_cast<double>(visibleW) / m_pixelsPerBeat;
            const double leftGuard = visibleStart + visibleDur * 0.15;
            const double rightGuard = visibleStart + visibleDur * 0.85;

            if (m_playheadBeat < leftGuard || m_playheadBeat > rightGuard) {
                const double targetStart = std::max(0.0, m_playheadBeat - visibleDur * 0.2);
                m_scrollX = static_cast<float>(targetStart * m_pixelsPerBeat);
                m_targetScrollX = m_scrollX;
                updateScrollbars();
            }
        }
    }

    syncChildren();
}

void PianoRollView::setTotalDurationBeats(double beats) {
    m_totalDurationBeats = std::max(8.0, beats);
    m_patternLengthBeats = m_totalDurationBeats;
    if (m_toolbar) {
        m_toolbar->setPatternLengthBeats(m_patternLengthBeats);
    }
    updateScrollbars();
    syncChildren();
}

void PianoRollView::setOnAdjustPatternLength(std::function<void(int barsDelta)> cb) {
    if (m_toolbar) {
        m_toolbar->setOnAdjustPatternLength(std::move(cb));
    }
}

void PianoRollView::setOnPatternChoiceSelected(std::function<void(int patternValue)> cb) {
    if (m_toolbar) {
        m_toolbar->setOnPatternChoiceSelected(std::move(cb));
    }
}

void PianoRollView::setOnPlayheadScrubbed(std::function<void(double beat, bool active)> cb) {
    m_onPlayheadScrubbed = std::move(cb);
}

void PianoRollView::setLocalMinimapVisible(bool visible) {
    if (m_showLocalMinimap == visible) return;
    m_showLocalMinimap = visible;
    layoutChildren();
}

void PianoRollView::applyEdgeAutoScroll(float scrollX, float scrollY) {
    const float totalH = 128.0f * m_keyHeight;
    const float visibleH = m_grid ? m_grid->getHeight() : 0.0f;
    const float maxScrollY = std::max(0.0f, totalH - visibleH);
    const float visibleW = m_grid ? m_grid->getWidth() : 0.0f;
    const double dynamicTotalBeats = std::max(std::max(4.0, m_totalDurationBeats), getViewDurationBeats() + 8.0);
    const float totalW = static_cast<float>(dynamicTotalBeats) * m_pixelsPerBeat;
    const float maxScrollX = std::max(0.0f, totalW - visibleW);

    m_scrollX = safeClampScroll(scrollX, maxScrollX);
    m_scrollY = safeClampScroll(scrollY, maxScrollY);
    m_targetScrollX = m_scrollX;
    m_targetScrollY = m_scrollY;
    updateScrollbars();
    syncChildren();
}

double PianoRollView::getViewStartBeat() const {
    return static_cast<double>(m_scrollX) / m_pixelsPerBeat;
}

double PianoRollView::getViewDurationBeats() const {
    if (!m_grid) return 4.0;
    const float visibleW = m_grid->getWidth();
    if (visibleW <= 0.0f) return 4.0;
    return static_cast<double>(visibleW) / m_pixelsPerBeat;
}

void PianoRollView::setViewWindow(double startBeat, double durationBeats) {
    const double clampedDuration = std::max(0.25, durationBeats);
    if (m_grid) {
        const float visibleW = m_grid->getWidth();
        if (visibleW > 0.0f) {
            m_pixelsPerBeat = std::clamp(visibleW / static_cast<float>(clampedDuration), 10.0f, 500.0f);
        }
    }
    m_scrollX = std::max(0.0f, static_cast<float>(startBeat * m_pixelsPerBeat));
    m_targetScrollX = m_scrollX;
    updateScrollbars();
    syncChildren();
}

} // namespace AestraUI
