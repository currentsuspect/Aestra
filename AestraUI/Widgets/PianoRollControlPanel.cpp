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

float laneCellHeight(const NUIRect& b) { return std::clamp((b.height - 10.0f) / kLaneCount, 12.0f, 20.0f); }

NUIRect laneSegmentCell(const NUIRect& b, float sidebarW, int index) {
    const float w = std::max(24.0f, sidebarW - 12.0f);
    const float h = laneCellHeight(b);
    return NUIRect(b.x + (sidebarW - w) * 0.5f, b.y + 6.0f + h * static_cast<float>(index), w, h);
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
    const auto border = themeManager.getColor("border").withAlpha(0.28f);
    renderer.fillRect(b, panelBg);
    renderer.drawLine(NUIPoint(b.x, b.y), NUIPoint(b.right(), b.y), 1.0f, border);
    
    // Sidebar Area (Left)
    const float sidebarW = sidebarWidth_; // the view's key-lane width: stems sit under their notes
    NUIRect sidebarRect(b.x, b.y, sidebarW, b.height);
    
    renderer.fillRect(sidebarRect, themeManager.getColor("backgroundPrimary").withAlpha(0.72f));
    renderer.drawLine(NUIPoint(sidebarRect.right(), sidebarRect.y),
                      NUIPoint(sidebarRect.right(), sidebarRect.bottom()),
                      1.0f,
                      border);

    // The lane picker: the active cell filled, the others quiet. Below it, the lane's scale at the guide lines.
    const bool panMode = laneMode_ == LaneMode::Pan;
    const LaneMode field = laneMode_;
    const float labelSize = themeManager.getFontSize("xs");
    const auto accent = themeManager.getColor("accentPrimary");
    const auto textColor = themeManager.getColor("textPrimary");
    const NUIRect first = laneSegmentCell(b, sidebarW, 0);
    const NUIRect segment(first.x, first.y, first.width, first.height * kLaneCount);
    renderer.strokeRoundedRect(segment, 5.0f, 1.0f, themeManager.getColor("border").withAlpha(0.4f));
    for (int i = 0; i < kLaneCount; ++i) {
        const NUIRect cell = laneSegmentCell(b, sidebarW, i);
        const bool active = static_cast<int>(field) == i;
        const char* const* names = kLaneNames;
        const NUIRect inner(cell.x + 2.0f, cell.y + 2.0f, cell.width - 4.0f, cell.height - 4.0f);
        if (active) renderer.fillRoundedRect(inner, 3.0f, accent.withAlpha(0.28f));
        const auto dim = renderer.measureText(names[i], labelSize);
        renderer.drawText(names[i], NUIPoint(cell.x + (cell.width - dim.width) * 0.5f, renderer.calculateTextY(cell, labelSize)),
                          labelSize, textColor.withAlpha(active ? 1.0f : 0.55f));
    }
    {
        // 127 / 64 / 0 for velocity, R / C / L for pan, each beside its guide line; skipped where the segment is in the way.
        const float availHScale = std::max(1.0f, b.height - 28.0f);
        const float bottom = b.bottom() - 8.0f;
        const char* const markSets[kLaneCount][3] = {{"127", "64", "0"}, {"100%", "50%", "0%"}, {"\xC3\x974", "", "\xC3\x971"},
                                                     {"R", "C", "L"}};
        const char* const* marks = markSets[static_cast<int>(field)];
        const float ys[3] = {bottom - availHScale, bottom - availHScale * 0.5f, bottom};
        for (int i = 0; i < 3; ++i) {
            if (ys[i] < segment.bottom() + 10.0f || marks[i][0] == '\0') continue;
            const auto dim = renderer.measureText(marks[i], 9.0f);
            renderer.drawText(marks[i], NUIPoint(sidebarRect.right() - 6.0f - dim.width, ys[i] - dim.height * 0.5f), 9.0f,
                              themeManager.getColor("textSecondary").withAlpha(0.6f));
        }
    }

    auto layer = noteLayer_.lock();
    if (!layer || !isVisible()) return;

    // Content Area Clip
    NUIRect contentRect(b.x + sidebarW, b.y, b.width - sidebarW, b.height);
    renderer.setClipRect(contentRect);

    const float startX = contentRect.x;
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
    renderTimelineGrid(renderer, contentRect, startX, contentRect.right(), scrollX_, pixelsPerBeat_, beatsPerBar_,
                       themeManager.getCurrentTheme().textPrimary, gridStyle);

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
    if (patternEndX >= contentRect.x && patternEndX <= contentRect.right()) {
        renderer.drawLine(NUIPoint(patternEndX, contentRect.y),
                          NUIPoint(patternEndX, contentRect.bottom()),
                          1.0f,
                          themeManager.getColor("accentPrimary").withAlpha(0.34f));
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
            // Velocity and chance rise from the floor by their 0..1 value; a ratchet by its step, a quarter per hit.
            const float v = field == LaneMode::Chance ? std::clamp(n.chance, 0.0f, 1.0f) : normalizedVelocity;
            const float h = field == LaneMode::Ratchet ? ratchetToPanelHeight(n.ratchet, availH)
                                                       : velocityToPanelHeight(v, availH);
            y = bottomY - h;
            stemTop = y;
            stemH = std::max(2.0f, h);
            intensity = field == LaneMode::Ratchet ? h / availH : v;
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
            } else if (field == LaneMode::Chance || field == LaneMode::Ratchet) {
                value = PianoRollLook::expressionTag(field == LaneMode::Chance ? n.chance : 1.0f,
                                                     field == LaneMode::Ratchet ? n.ratchet : 1);
                if (value.empty()) value = field == LaneMode::Chance ? "100%" : "\xC3\x97" "1";
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
