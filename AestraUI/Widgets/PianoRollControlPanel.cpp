// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIPianoRollWidgets.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
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

    const float startX = contentRect.x;
    renderTimelineGrid(renderer, contentRect, startX, contentRect.right(), scrollX_, pixelsPerBeat_, beatsPerBar_,
                       themeManager.getCurrentTheme().textPrimary);

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
                          NUIThemeManager::getInstance().getCurrentTheme().textPrimary.withAlpha(0.14f));
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

} // namespace AestraUI
