// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PianoRollLook.h"
#include "PianoRollWidgetShared.h"
#include "../Helpers/TimelineGridRenderer.h"
#include "../Helpers/PianoRollInteraction.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

// =============================================================================
// PianoRollControlPanel (split from NUIPianoRollWidgets.cpp)
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

namespace {
// The lane picker in the sidebar (FD-48 board 6, FD-63 board A + C): Velocity, Chance, Ratchet and Pan stacked, one cell
// each, shrinking to fit a short lane.
constexpr int kLaneCount = 4;
const char* const kLaneNames[kLaneCount] = {"Velocity", "Chance", "Ratchet", "Pan"};

float laneCellHeight(const NUIRect& b) { return std::clamp((b.height - 16.0f) / kLaneCount, 12.0f, 18.0f); }

// FD-63 board A + C: the picker is a plain list of names, 8 px in, one row each.
NUIRect laneSegmentCell(const NUIRect& b, float sidebarW, int index) {
    const float h = laneCellHeight(b);
    return NUIRect(b.x + 4.0f, b.y + 6.0f + h * static_cast<float>(index), std::max(24.0f, sidebarW - 8.0f), h);
}
} // namespace

bool PianoRollControlPanel::onMouseEvent(const NUIMouseEvent& event) {
    // Note: If dropdown consumes event, we return true above.
    // Else check sidebar/velocity area.
    
    auto b = getBounds();
    
    // CRITICAL FIX: Ignore events outside bounds unless we are already dragging
    if (!b.contains(event.position) && !isDragging_) return false;

    const float sidebarW = sidebarWidth_; // the view's key-lane width: stems sit under their notes
    // Note Layer Interaction (Velocity)
    // We assume clicks in content area are for velocity
    // COPIED FROM OLD LOGIC
    auto layer = noteLayer_.lock();
    if (layer) {
        auto b = getBounds();
        // Sidebar: clicking a cell shows that lane.
        if (!isDragging_ && event.position.x < b.x + sidebarW) {
             if (event.pressed && event.button == NUIMouseButton::Left) {
                 for (int i = 0; i < kLaneCount; ++i)
                     if (laneSegmentCell(b, sidebarW, i).contains(event.position)) laneMode_ = static_cast<LaneMode>(i);
                 repaint();
                 return true;
             }
             return NUIComponent::onMouseEvent(event);
        }

        const bool panMode = laneMode_ == LaneMode::Pan;
        const LaneMode field = laneMode_;
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
                if (field == LaneMode::Ratchet) return static_cast<float>(ratchetFromPanelPosition(y, bottomY, availH));
                return velocityFromPanelPosition(y, bottomY, availH); // velocity and chance are both 0..1
            };

            if (foundIdx != -1) {
                isDragging_ = true;
                hoveringNoteIndex_ = foundIdx;
                dragStartPos_ = event.position;
                dragUndoSnapshot_ = layer->getNotes(); // baseline for one undo step

                // Set the value immediately based on click Y (Global)
                const float newValue = laneValueAtY(event.position.y);

                layer->setLaneValue(foundIdx, field, newValue);
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
                     layer->pushExternalEdit(dragUndoSnapshot_, kLaneNames[static_cast<int>(field)]);
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
            } else if (field == LaneMode::Ratchet) {
                newValue = static_cast<float>(ratchetFromPanelPosition(event.position.y, bottomY, availH));
            } else {
                newValue = velocityFromPanelPosition(event.position.y, bottomY, availH);
            }

            const int swept = noteUnderX(localX);
            if (swept != -1) hoveringNoteIndex_ = swept;

            if (layer->setLaneValue(hoveringNoteIndex_, field, newValue)) repaint();
            return true;
        }
    }
    
    return NUIComponent::onMouseEvent(event);
}

void PianoRollControlPanel::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    
    const auto panelBg = themeManager.getColor("backgroundPrimary");
    const auto border = themeManager.getColor("border");
    renderer.fillRect(b, panelBg);
    renderer.drawLine(NUIPoint(b.x, b.y), NUIPoint(b.right(), b.y), 1.0f, border);
    
    // Sidebar Area (Left)
    const float sidebarW = sidebarWidth_; // the view's key-lane width: stems sit under their notes
    NUIRect sidebarRect(b.x, b.y, sidebarW, b.height);
    
    renderer.drawLine(NUIPoint(sidebarRect.right() - 0.5f, sidebarRect.y), NUIPoint(sidebarRect.right() - 0.5f, sidebarRect.bottom()),
                      1.0f, themeManager.getColor("border"));

    // FD-63, board "A + C": the lanes as a list of names, the shown one bright.
    const bool panMode = laneMode_ == LaneMode::Pan;
    const LaneMode field = laneMode_;
    const float labelSize = 11.0f;
    for (int i = 0; i < kLaneCount; ++i) {
        const NUIRect cell = laneSegmentCell(b, sidebarW, i);
        const bool active = static_cast<int>(field) == i;
        renderer.drawText(kLaneNames[i], NUIPoint(cell.x + 4.0f, renderer.calculateTextY(cell, labelSize)), labelSize,
                          themeManager.getColor(active ? "textPrimary" : "textMuted"));
    }

    auto layer = noteLayer_.lock();
    if (!layer || !isVisible()) return;

    NUIRect contentRect(b.x + sidebarW, b.y, b.width - sidebarW, b.height);
    renderer.setClipRect(contentRect);
    const float startX = contentRect.x;
    const float availH = std::max(1.0f, b.height - 28.0f);
    const float bottomY = b.bottom() - 8.0f;
    const float centerY = bottomY - availH * 0.5f;
    if (panMode)
        renderer.drawLine(NUIPoint(contentRect.x, centerY), NUIPoint(contentRect.right(), centerY), 1.0f,
                          themeManager.getColor("border"));

    // Each note: a 2 px stem from the floor (from the centre line for pan) and an 8 px square at its value, in the unit's
    // colour at 70%; the selected ones in the text colour.
    const auto& notes = layer->getNotes();
    const NUIColor unit = layer->getNoteColor();
    const NUIColor stemColor(unit.r * 0.7f, unit.g * 0.7f, unit.b * 0.7f, 1.0f);
    const NUIColor selectedColor = themeManager.getColor("textPrimary");

    for (size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const auto& n = notes[noteIndex];
        if (n.isDeleted && n.animationScale < 0.01f) continue;
        const float x = startX + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
        if (x > b.right() || x < contentRect.x - 10.0f) continue;

        const float normalizedVelocity = std::clamp(n.velocity, 0.0f, 1.0f);
        float y;
        float stemTop;
        float stemH;
        if (panMode) {
            const float pv = std::clamp(n.pan, -1.0f, 1.0f);
            y = centerY - pv * availH * 0.5f;
            stemTop = std::min(y, centerY);
            stemH = std::max(2.0f, std::abs(y - centerY));
        } else {
            // Velocity and chance rise from the floor by their 0..1 value; a ratchet by its step, a quarter per hit.
            const float v = field == LaneMode::Chance ? std::clamp(n.chance, 0.0f, 1.0f) : normalizedVelocity;
            const float h = field == LaneMode::Ratchet ? ratchetToPanelHeight(n.ratchet, availH)
                                                       : velocityToPanelHeight(v, availH);
            y = bottomY - h;
            stemTop = y;
            stemH = std::max(2.0f, h);
        }
        const NUIColor col = n.selected ? selectedColor : stemColor;
        renderer.fillRect(NUIRect(std::round(x + 3.0f), stemTop, 2.0f, stemH), col);
        renderer.fillRect(NUIRect(std::round(x), std::round(y - 4.0f), 8.0f, 8.0f), col);

        if (isDragging_ && static_cast<int>(noteIndex) == hoveringNoteIndex_) {
            std::string value;
            if (panMode) {
                const int panSteps = static_cast<int>(std::lround(std::abs(n.pan) * 100.0f));
                if (panSteps == 0) value = "C";
                else value = (n.pan < 0.0f ? "L " : "R ") + std::to_string(panSteps);
            } else if (field == LaneMode::Chance || field == LaneMode::Ratchet) {
                value = PianoRollLook::expressionTag(field == LaneMode::Chance ? n.chance : 1.0f,
                                                     field == LaneMode::Ratchet ? n.ratchet : 1);
                if (value.empty()) value = field == LaneMode::Chance ? "100%" : "\xC3\x97" "1";
            } else {
                value = std::to_string(static_cast<int>(std::lround(normalizedVelocity * 127.0f)));
            }
            const float fontSize = 10.0f;
            const auto textSize = renderer.measureText(value, fontSize);
            const NUIRect bubble(x + 12.0f, (y - 18.0f >= contentRect.y ? y - 18.0f : y + 8.0f), textSize.width + 10.0f, 16.0f);
            renderer.fillRect(bubble, themeManager.getColor("backgroundSecondary"));
            renderer.strokeRect(bubble, 1.0f, themeManager.getColor("border"));
            renderer.drawText(value, NUIPoint(bubble.x + 5.0f, bubble.y + (bubble.height - textSize.height) * 0.5f), fontSize,
                              selectedColor);
        }
    }

    renderer.clearClipRect();
}

} // namespace AestraUI
