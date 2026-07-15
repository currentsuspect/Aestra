// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPianoRollWidgets.h"
#include "../Platform/NUIPlatformBridge.h"
#include "../Common/MusicHelpers.h"
#include "../Helpers/PianoRollInteraction.h"
#include "NUIDropdown.h"
#include "NUIButton.h"
#include "NUIContextMenu.h"
#include "NUIIcon.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <iomanip> // For string formatting

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
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    
    // CLIP: Prevent left bleeding
    renderer.setClipRect(b);
    
    auto bg = themeManager.getColor("backgroundSecondary").darkened(0.025f);
    auto textCol = themeManager.getColor("textPrimary").withAlpha(0.86f);
    auto tickCol = themeManager.getColor("textSecondary").withAlpha(0.52f);
    auto borderCol = themeManager.getColor("border").withAlpha(0.66f);
    
    renderer.fillRect(b, bg);
    
    // Bottom border
    renderer.drawLine(NUIPoint(b.x, b.y + b.height), NUIPoint(b.x + b.width, b.y + b.height), 1.0f, borderCol);

    float startBeat = scrollX_ / pixelsPerBeat_;
    float endBeat = (scrollX_ + b.width) / pixelsPerBeat_;
    
    int iStart = static_cast<int>(startBeat);
    int iEnd = static_cast<int>(endBeat) + 1;

    for (int i = iStart; i <= iEnd; ++i) {
        float x = snapVerticalLineX(beatToScreenX(static_cast<double>(i), pixelsPerBeat_, scrollX_, b.x));
        
        // Prevent drawing text partially off-screen left if we can help it, 
        // but setClipRect handles the actual pixels.
        
        bool isBar = (beatsPerBar_ > 0 && i % beatsPerBar_ == 0);
        
        if (isBar) {
            // Bar: Taller tick, Label
            int barNum = (i / beatsPerBar_) + 1;
            
            // Modern Look: Line doesn't go all the way, just top portion or bottom tick
            // Playlist usually has numbers at the start of the bar.
            
            // Major Tick (Bottom up)
            renderer.fillRect(NUIRect(x, b.y, 1.0f, b.height), borderCol.withAlpha(0.48f));
            renderer.drawLine(NUIPoint(x, b.y + b.height * 0.45f), NUIPoint(x, b.y + b.height), 1.0f, tickCol);
            
            // Label
            renderer.drawText(std::to_string(barNum), NUIPoint(x + 6, b.y + 5), 11.0f, textCol);
        } else {
            // Beat: Short Tick
            renderer.drawLine(NUIPoint(x, b.y + b.height * 0.75f), NUIPoint(x, b.y + b.height), 1.0f, tickCol.withAlpha(0.6f));
        }
    }

    const float playheadX = snapVerticalLineX(
        beatToScreenX(playheadBeat_, pixelsPerBeat_, scrollX_, b.x));
    if (playheadX >= b.x && playheadX <= b.right()) {
        const auto accent = themeManager.getColor("accentPrimary");
        const NUIRect handle(playheadX - 5.0f, b.y + 3.0f, 10.0f, 16.0f);
        renderer.fillRoundedRect(handle, 4.0f, accent.withAlpha(isScrubbing_ ? 1.0f : 0.90f));
        renderer.strokeRoundedRect(handle, 4.0f, 1.0f, NUIColor::white().withAlpha(0.46f));
        renderer.fillRoundedRect(NUIRect(playheadX - 1.0f, handle.y + 4.0f, 2.0f, 8.0f),
                                 1.0f,
                                 NUIColor::white().withAlpha(0.72f));
        renderer.drawLine(NUIPoint(playheadX, handle.bottom()),
                          NUIPoint(playheadX, b.bottom()),
                          2.0f,
                          accent.withAlpha(0.92f));
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
            bool isSelected = false; // Need to track this
            if (auto g = grid_.lock()) {
                // We'll need a getter for snap in PianoRollGrid or track it here
            }
            snapMenu->addItem(MusicTheory::getSnapName(snap), [this, snap]() {
                if (auto g = grid_.lock()) g->setSnap(snap);
                if (auto n = notes_.lock()) n->setSnap(snap);
            });
        }
        menu->addSubmenu("Snap", snapMenu);
        
        // --- ROOT KEY SUBMENU ---
        auto rootMenu = std::make_shared<NUIContextMenu>();
        auto roots = MusicTheory::getRootNames();
        for (int i = 0; i < roots.size(); ++i) {
            rootMenu->addItem(roots[i], [this, i]() {
                if (auto g = grid_.lock()) g->setRootKey(i);
                if (auto n = notes_.lock()) n->setRootKey(i);
            });
        }
        menu->addSubmenu("Root Key", rootMenu);
        
        // --- SCALE SUBMENU ---
        auto scaleMenu = std::make_shared<NUIContextMenu>();
        auto scales = MusicTheory::getScales();
        for (int i = 0; i < scales.size(); ++i) {
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
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    
    // CLIP TO BOUNDS to prevent bleeding
    renderer.setClipRect(b);

    renderer.fillRect(b, themeManager.getColor("backgroundPrimary").darkened(0.01f));

    const auto rowAccidental = themeManager.getColor("surfaceRaised").withAlpha(0.11f);
    const auto rowRoot = themeManager.getColor("accentPrimary").withAlpha(0.075f);
    const auto rowOutOfScale = themeManager.getColor("backgroundPrimary").darkened(0.05f).withAlpha(0.45f);
    const auto rowLine = themeManager.getColor("border").withAlpha(0.075f);
    const auto gridSubdivision = themeManager.getColor("border").withAlpha(0.065f);
    const auto gridBeat = themeManager.getColor("border").withAlpha(0.15f);
    const auto gridBar = themeManager.getColor("textSecondary").withAlpha(0.24f);

    const double visibleStartBeat = static_cast<double>(scrollX_) / pixelsPerBeat_;
    const double visibleEndBeat = static_cast<double>(scrollX_ + b.width) / pixelsPerBeat_;
    const int firstVisibleBar = static_cast<int>(std::floor(visibleStartBeat / beatsPerBar_));
    const int lastVisibleBar = static_cast<int>(std::ceil(visibleEndBeat / beatsPerBar_));
    const auto zebraColor = themeManager.getColor("surfaceRaised").withAlpha(0.035f);
    for (int bar = firstVisibleBar; bar <= lastVisibleBar; ++bar) {
        if ((bar & 1) == 0) continue;
        const double barStartBeat = static_cast<double>(bar * beatsPerBar_);
        const float barX = beatToScreenX(barStartBeat, pixelsPerBeat_, scrollX_, b.x);
        const float barWidth = pixelsPerBeat_ * static_cast<float>(beatsPerBar_);
        renderer.fillRect(NUIRect(barX, b.y, barWidth, b.height), zebraColor);
    }

    // 1. Draw Rows (Matching Keys)
    int startPitch = 127 - static_cast<int>((scrollY_) / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + b.height) / keyHeight_);
    
    // Expand range for safety margin (2 extra rows on each end)
    startPitch = std::clamp(startPitch + 2, 0, 127);
    endPitch = std::clamp(endPitch - 2, 0, 127);
    
    for (int p = startPitch; p >= endPitch; --p) {
        // Calculate screen Y position
        float worldY = (127 - p) * keyHeight_;
        float y = b.y + worldY - scrollY_;
        
        NUIRect rowRect(b.x, y, b.width, keyHeight_);
        
        const int pitchClass = ((p % 12) + 12) % 12;
        const bool isRoot = pitchClass == rootKey_;
        const bool isInScale = scaleType_ == ScaleType::Chromatic || MusicTheory::isNoteInScale(p, rootKey_, scaleType_);

        if (isRoot) {
            renderer.fillRect(rowRect, rowRoot);
        } else if (isBlackKey(p)) {
            renderer.fillRect(rowRect, rowAccidental);
        }
        if (!isInScale) {
            renderer.fillRect(rowRect, rowOutOfScale);
        }

        renderer.drawLine(NUIPoint(b.x, y), NUIPoint(b.right(), y), 1.0f, rowLine);
    }

    // Vertical Lines (Snap Grid)
    double snapDur = MusicTheory::getSnapDuration(snap_);
    if (snapDur <= 0.0001) snapDur = 1.0;
    if (snap_ == SnapGrid::None) snapDur = 1.0; 
    
    // Dynamic Density: If too dense, double interval
    while ((pixelsPerBeat_ * snapDur) < 12.0f) {
        snapDur *= 2.0;
    }

    double current = std::floor(visibleStartBeat / snapDur) * snapDur;
    
    for (; current <= visibleEndBeat + snapDur; current += snapDur) {
        // Calculate X in double precision relative to scrollX_ BEFORE casting to float
        float x = snapVerticalLineX(beatToScreenX(current, pixelsPerBeat_, scrollX_, b.x));
        
        const bool isBar = std::fmod(std::abs(current), static_cast<double>(beatsPerBar_)) < 0.001;
        const bool isBeat = std::fmod(std::abs(current), 1.0) < 0.001;
        const float lineWidth = isBar ? 1.5f : 1.0f;
        const NUIColor col = isBar ? gridBar : (isBeat ? gridBeat : gridSubdivision);
        renderer.drawLine(NUIPoint(x, b.y), NUIPoint(x, b.y + b.height), lineWidth, col);
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
        const NUIColor baseColor = n.selected ? noteColorSelected : noteColor;
        const NUIColor coreColor = baseColor.withAlpha(0.68f + normalizedVelocity * 0.28f);
        const NUIColor edgeColor = n.selected
                                       ? NUIColor::white().withAlpha(0.72f)
                                       : baseColor.lightened(0.16f).withAlpha(0.74f);

        renderer.drawShadow(NUIRect(r.x, r.y + 1.0f, r.width, r.height),
                            0.0f,
                            2.0f,
                            5.0f,
                            NUIColor(0, 0, 0, n.selected ? 0.22f : 0.14f));
        renderer.fillRoundedRect(r, 3.0f, coreColor);
        renderer.strokeRoundedRect(r, 3.0f, n.selected ? 1.5f : 1.0f, edgeColor);
        renderer.fillRoundedRect(NUIRect(r.x + 1.0f, r.y + 2.0f, 2.5f, std::max(2.0f, r.height - 4.0f)),
                                 1.0f,
                                 NUIColor::white().withAlpha(n.selected ? 0.68f : 0.42f));

        // Edge resize affordance on hover
        if (static_cast<int>(noteIndex) == hoveredNoteIndex_ && (hoverOnRightEdge_ || hoverOnLeftEdge_)) {
            const NUIColor affordanceColor = NUIColor::white().withAlpha(0.72f);
            if (hoverOnRightEdge_) {
                renderer.fillRoundedRect(NUIRect(r.right() - 3.0f, r.y + 3.0f, 2.0f, r.height - 6.0f), 1.0f, affordanceColor);
            }
            if (hoverOnLeftEdge_) {
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

    // Rubber-band selection rectangle (already normalized during drag)
    if (state_ == State::SelectingBox && selectionRect_.width > 0 && selectionRect_.height > 0) {
        renderer.fillRoundedRect(selectionRect_, 2.0f, NUIColor(0.545f, 0.498f, 1.0f, 0.15f));
        renderer.strokeRoundedRect(selectionRect_, 2.0f, 1.0f, NUIColor(0.545f, 0.498f, 1.0f, 0.55f));
    }

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
        return false;
    }

    auto b = getBounds();
    float localX = event.position.x - b.x + scrollX_;
    float localY = event.position.y - b.y + scrollY_;

    // --- HOVER / SMART CURSOR (no button activity) ---
    if (state_ == State::None && !event.pressed && !event.released) {
        int hitIdx = findNoteAt(localX, localY);
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
        
        // DOUBLE CLICK: Add / Delete
        if (event.doubleClick) {
            auto oldNotes = notes_;
            if (clickedIndex != -1) {
                 // Delete existing note
                 notes_.erase(notes_.begin() + clickedIndex);
                 pushUndo("Delete Note", oldNotes, notes_);
                 commitNotes();
                 repaint();
            } else {
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
                            if (snapToScale_ && scaleType_ != ScaleType::Chromatic) {
                                n.pitch = MusicTheory::nextPitchInScale(n.pitch, rootKey_, scaleType_);
                            } else {
                                n.pitch = std::min(127, n.pitch + 1);
                            }
                        }
                    }
                    pushUndo("Transpose Up", oldNotes, notes_);
                    commitNotes();
                    repaint();
                    return true;
                }
                else if (event.keyCode == NUIKeyCode::Down) {
                    auto oldNotes = notes_;
                    for (auto& n : notes_) {
                        if (n.selected && !n.isDeleted) {
                            if (snapToScale_ && scaleType_ != ScaleType::Chromatic) {
                                n.pitch = MusicTheory::previousPitchInScale(n.pitch, rootKey_, scaleType_);
                            } else {
                                n.pitch = std::max(0, n.pitch - 1);
                            }
                        }
                    }
                    pushUndo("Transpose Down", oldNotes, notes_);
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
        // Ignore sidebar clicks for velocity (except dragging started inside)
        // If not dragging and in sidebar, let base handle it (or return true if we want to block)
        if (!isDragging_ && event.position.x < b.x + sidebarW) {
             return NUIComponent::onMouseEvent(event);
        }

        float localX = event.position.x - b.x + scrollX_ - sidebarW;
        
        if (event.pressed && event.button == NUIMouseButton::Left) {
            float minDist = 10.0f; // Pixel threshold
            int foundIdx = -1;
            
            const auto& notes = layer->getNotes();
            std::vector<int> candidates;
            for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
                if (notes[i].isDeleted) continue;
                
                float nStart = static_cast<float>(notes[i].startBeat * pixelsPerBeat_);
                float nEnd = static_cast<float>((notes[i].startBeat + notes[i].durationBeats) * pixelsPerBeat_);
                float minW = 6.0f; // Lollipop head radius
                if (nEnd < nStart + minW) nEnd = nStart + minW; 
                
                // Hit Test: Head (Start) OR Body (Length Line)
                // Relaxed tolerance for Head
                bool hitHead = std::abs(nStart - localX) < 10.0f;
                bool hitBody = (localX >= nStart - 2.0f && localX <= nEnd + 2.0f);
                
                if (hitHead || hitBody) {
                    candidates.push_back(i);
                }
            }
            
            if (!candidates.empty()) {
                // Default heuristic: Last one (usually rendered last = on top)
                foundIdx = candidates.back();
                
                // Priority: Selected Note
                for (int idx : candidates) {
                    if (notes[idx].selected) {
                        foundIdx = idx;
                        break;
                    }
                }
            }
            
            if (foundIdx != -1) {
                isDragging_ = true;
                hoveringNoteIndex_ = foundIdx;
                dragStartPos_ = event.position;
                
                // Set velocity immediately based on click Y (Global)
                const float availH = std::max(1.0f, b.height - 28.0f);
                const float bottomY = b.bottom() - 8.0f;
                const float newVelocity = velocityFromPanelPosition(event.position.y, bottomY, availH);
                
                auto modNotes = notes;
                modNotes[foundIdx].velocity = newVelocity;
                
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
                 return true;
            }

            // Move
            const float availH = std::max(1.0f, b.height - 28.0f);
            const float bottomY = b.bottom() - 8.0f;
            const float newVelocity = velocityFromPanelPosition(event.position.y, bottomY, availH);
            
            auto modNotes = layer->getNotes();
            if (hoveringNoteIndex_ >= 0 && static_cast<size_t>(hoveringNoteIndex_) < modNotes.size()) {
                 modNotes[hoveringNoteIndex_].velocity = newVelocity;
                 // Single Edit Only (Batch removed)
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

    const float labelSize = themeManager.getFontSize("xs");
    const auto textDim = renderer.measureText("VELOCITY", labelSize);
    renderer.drawText("VELOCITY",
                      NUIPoint(b.x + (sidebarW - textDim.width) * 0.5f, b.y + 13.0f),
                      labelSize,
                      themeManager.getColor("textPrimary").withAlpha(0.78f));
    const auto rangeDim = renderer.measureText("MIDI 0 - 127", 8.0f);
    renderer.drawText("MIDI 0 - 127",
                      NUIPoint(b.x + (sidebarW - rangeDim.width) * 0.5f, b.y + 31.0f),
                      8.0f,
                      themeManager.getColor("textSecondary").withAlpha(0.48f));
    
    auto layer = noteLayer_.lock();
    if (!layer || !isVisible()) return;

    // Content Area Clip
    NUIRect contentRect(b.x + sidebarW, b.y, b.width - sidebarW, b.height);
    renderer.setClipRect(contentRect);

    constexpr int beatsPerBar = 4;
    const double visibleStartBeat = scrollX_ / pixelsPerBeat_;
    const double visibleEndBeat = (scrollX_ + contentRect.width) / pixelsPerBeat_;
    const int firstVisibleBar = static_cast<int>(std::floor(visibleStartBeat / beatsPerBar));
    const int lastVisibleBar = static_cast<int>(std::ceil(visibleEndBeat / beatsPerBar));
    const auto zebraColor = themeManager.getColor("surfaceRaised").withAlpha(0.035f);
    for (int bar = firstVisibleBar; bar <= lastVisibleBar; ++bar) {
        if ((bar & 1) == 0) continue;
        const float x = contentRect.x + static_cast<float>(bar * beatsPerBar) * pixelsPerBeat_ - scrollX_;
        renderer.fillRect(NUIRect(x, contentRect.y, pixelsPerBeat_ * beatsPerBar, contentRect.height), zebraColor);
    }

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

    // 1. Draw Grid Background (Sync with PianoRollGrid)
    auto snap = layer->getSnap();
    double snapDur = MusicTheory::getSnapDuration(snap);
    if (snapDur <= 0.0001) snapDur = 1.0;
    if (snap == SnapGrid::None) snapDur = 1.0; 

    // Dynamic Density
    while ((pixelsPerBeat_ * snapDur) < 12.0f) {
        snapDur *= 2.0;
    }

    float startX = b.x + sidebarW;
    // Align to snap
    double current = std::floor(visibleStartBeat / snapDur) * snapDur;

    auto gridCol = themeManager.getColor("border").withAlpha(0.07f);
    auto barCol = themeManager.getColor("border").withAlpha(0.14f);
    for (; current <= visibleEndBeat + snapDur; current += snapDur) {
        // Double precision relative subtraction
        double relX = (current * pixelsPerBeat_) - static_cast<double>(scrollX_);
        float x = startX + static_cast<float>(relX);
        
        bool isBar = (std::fmod(std::abs(current), (double)beatsPerBar) < 0.001);
        
        // Draw line
        renderer.drawLine(NUIPoint(x, b.y), NUIPoint(x, b.y + b.height), 1.0f, isBar ? barCol : gridCol);
    }
    
    // 2. Render velocity stems using MidiNote's normalized 0..1 representation.
    const auto& notes = layer->getNotes();
    auto velColorBase = themeManager.getColor("accentPrimary").lightened(0.05f);

    for (const auto& n : notes) {
        if (n.isDeleted && n.animationScale < 0.01f) continue;
        
        float x = startX + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
        
        // Skip if out of view
        if (x > b.x + b.width) continue;
        
        const float normalizedVelocity = std::clamp(n.velocity, 0.0f, 1.0f);
        float h = velocityToPanelHeight(normalizedVelocity, availH);
        float y = bottomY - h;

        float alpha = 0.48f + normalizedVelocity * 0.48f;
        auto col = velColorBase.withAlpha(alpha);
        if (n.selected) col = themeManager.getColor("accentSecondary").withAlpha(0.92f);

        renderer.fillRoundedRect(NUIRect(x - 2.0f, y, 4.0f, std::max(2.0f, h)), 2.0f, col.withAlpha(alpha * 0.78f));

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
    
    // Toolbar
    m_toolbar = std::make_shared<PianoRollToolbar>();
    m_toolbar->setGrid(m_grid);
    m_toolbar->setNoteLayer(m_notes);
    m_toolbar->setPatternLengthBeats(m_patternLengthBeats);

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
            renderer.drawLine(NUIPoint(playheadX, playheadStartY),
                              NUIPoint(playheadX, playheadEndY),
                              5.0f,
                              accent.withAlpha(0.10f));
            renderer.drawLine(NUIPoint(playheadX, playheadStartY),
                              NUIPoint(playheadX, playheadEndY),
                              2.0f,
                              accent.withAlpha(0.92f));

            const float triangleH = 8.0f;
            const float triangleW = 5.0f;
            for (float dx = 0.0f; dx <= triangleW; dx += 1.0f) {
                renderer.drawLine(NUIPoint(playheadX + dx, playheadStartY),
                                  NUIPoint(playheadX + dx,
                                           playheadStartY + triangleH - dx * (triangleH / triangleW)),
                                  1.0f,
                                  accent);
                renderer.drawLine(NUIPoint(playheadX - dx, playheadStartY),
                                  NUIPoint(playheadX - dx,
                                           playheadStartY + triangleH - dx * (triangleH / triangleW)),
                                  1.0f,
                                  accent);
            }
        }
    }

    if (m_toolbar) {
        if (auto menu = m_toolbar->getActiveContextMenu(); menu && menu->isVisible()) {
            menu->onRender(renderer);
        }
    }

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
    
    m_notes->setPixelsPerBeat(m_pixelsPerBeat);
    m_notes->setKeyHeight(m_keyHeight);
    m_notes->setScrollOffsetX(m_scrollX);
    m_notes->setScrollOffsetY(m_scrollY);
    m_notes->setPlayheadBeat(m_playheadBeat);
    
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
    float splitterZone = 5.0f;
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (std::abs(event.position.y - splitterY) < splitterZone) {
            m_isResizingPanel = true;
            m_dragStartPos = event.position;
            m_dragStartPanelHeight = m_controlPanelHeight;
            return true;
        }
    }
    else if (m_isResizingPanel && event.pressed) {
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
        m_keys->setOnPreviewNote(std::move(cb));
        m_keys->setIsPlayingFromParent(m_isPlayingCallback);
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
