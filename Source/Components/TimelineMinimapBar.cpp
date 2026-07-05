// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "TimelineMinimapBar.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "TimelineSummaryCache.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace AestraUI {

namespace {

constexpr float kRadius = 6.0f;
constexpr float kToggleRadius = 4.0f;

constexpr float kToggleW = 20.0f;
constexpr float kToggleH = 16.0f;
constexpr float kToggleGap = 3.0f;

constexpr float kTooltipPadX = 6.0f;
constexpr float kTooltipPadY = 3.0f;
constexpr float kTooltipRadius = 4.0f;

constexpr float kDragThresholdPx = 2.0f;
constexpr float kEdgeHitMaxPx = 9.0f;

inline float dist2(const NUIPoint& a, const NUIPoint& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline float edgeHitPxForWidth(float viewRectWidthPx)
{
    const float scaled = viewRectWidthPx * 0.15f + 4.0f;
    return std::min(kEdgeHitMaxPx, std::max(4.0f, scaled));
}

} // namespace

// =============================================================================
// SECTION: Construction & Model
// =============================================================================

TimelineMinimapBar::TimelineMinimapBar()
{
    cacheThemeColors_();
}

namespace {
inline bool rangesEqual(const TimelineRange& a, const TimelineRange& b) {
    return a.start == b.start && a.end == b.end;
}
} // namespace

void TimelineMinimapBar::setModel(const TimelineMinimapModel& model)
{
    // Only repaint when the model actually changed. setModel is called every
    // frame from TrackManagerUI::onUpdate, and an unconditional repaint() kept
    // the root component permanently dirty — which would defeat idle frame
    // elision (labs/perf/idle-frame-elision-spec.md).
    // The snapshot is referenced by pointer and its version can be bumped
    // in place, so the last-seen version must be remembered by value —
    // comparing old->version against new->version through the same pointer
    // would always pass.
    const TimelineMinimapModel& o = model_;
    const uint64_t newSummaryVersion = model.summary ? model.summary->version : 0;
    const bool sameSummary = (o.summary == model.summary) && (lastSeenSummaryVersion_ == newSummaryVersion);
    const bool unchanged = sameSummary && rangesEqual(o.view, model.view) && o.playheadBeat == model.playheadBeat &&
                           rangesEqual(o.loop, model.loop) && rangesEqual(o.selection, model.selection) &&
                           o.marks == model.marks && o.markCount == model.markCount && o.mode == model.mode &&
                           o.aggregation == model.aggregation && o.beatsPerBar == model.beatsPerBar &&
                           o.showSelection == model.showSelection && o.showLoop == model.showLoop &&
                           o.showMarkers == model.showMarkers && o.showDiagnostics == model.showDiagnostics &&
                           o.showPlayhead == model.showPlayhead;

    model_ = model;
    lastSeenSummaryVersion_ = newSummaryVersion;
    if (!unchanged) {
        repaint();
    }
}

void TimelineMinimapBar::onUpdate(double deltaTime)
{
    NUIComponent::onUpdate(deltaTime);

    // Safety check: if we're dragging but the mouse button is actually up (e.g. released outside window),
    // we force end the drag to prevent "sticky" behavior.
    if (dragKind_ != DragKind::None) {
#ifdef _WIN32
        // Check if Left Mouse Button is currently UP
        if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
            endDrag_();
        }
#endif
    }
}

void TimelineMinimapBar::endDrag_()
{
    if (dragKind_ == DragKind::None) return;

    // Use current mouse pos? Can't rely on it being valid if outside. 
    // We should use the last known logical state or just finish the op.
    // Replicating the logic from onMouseEvent's release block:

    const auto layout = computeLayout_();
    
    // Recalculate mouseBeat based on last known handled position would be ideal, 
    // but dragMoved_ logic might need current pos.
    // However, for "safety release", using the stored drag intent is enough.

    // If we are forcing end, we assume the last update was the final state desired 
    // OR we just commit whatever we have.
    
    // Actually, simply calling the final callback with the *last computed state* is tricky 
    // without tracking "current drag beat".
    // A simpler approach for the safety check is to just commit using the stored drag parameters if possible,
    // or just reset if we can't accurately commit.
    
    // BUT looking at the original code (lines 454+): it uses `mouseBeat` from the event.
    // If we are in onUpdate, we don't have an event.
    // We can't easily guess `mouseBeat`.
    
    // Robust solution: The previous onMouseMove (drag) events would have already updated the view 
    // with isFinal=false.
    // So the model IS already at the dragged position visually.
    // We just need to signal "Final = true" to let the host know we are done.
    
    // For Resize:
    if ((dragKind_ == DragKind::ResizeLeft || dragKind_ == DragKind::ResizeRight) && dragMoved_ &&
        onRequestResizeViewEdge) {
        const auto edge = (dragKind_ == DragKind::ResizeLeft) ? TimelineMinimapResizeEdge::Left
                                                              : TimelineMinimapResizeEdge::Right;
        // We pass the *current model view* as the result, because we updated it progressively?
        // No, the callback `onRequestResizeViewEdge` usually *applies* the change.
        // If we don't pass the correct beat, it might snap back.
        
        // HOWEVER, if the drag sticky happens, the user likely stopped moving the mouse.
        // So the last `drag` update was likely correct.
        // We can just confirm it.
        
        // But we don't have the last `mouseBeat` stored.
        // Let's rely on the fact that if we just called with `isFinal=true` and the SAME anchor/edge, it works.
        // But we don't know the edge beat.
        
        // Actually, since we updated the view progressively (via isFinal=false calls),
        // we can just tell the system to "stop resizing".
        // But the API requires `edgeBeat`.
        
        // Let's reconstruct edgeBeat from the model's CURRENT view.
        double edgeBeat = (edge == TimelineMinimapResizeEdge::Left) ? model_.view.start : model_.view.end;
        onRequestResizeViewEdge(edge, dragAnchorBeat_, edgeBeat, true);
    } 
    else if (!dragMoved_ && (dragKind_ == DragKind::Viewport || dragKind_ == DragKind::Pan) && onRequestCenterView) {
        // This was a click that wasn't a drag. We can't really execute it without mouseBeat.
        // But safety check implies we dragged out of bounds -> so likely dragMoved_ is true or distance is large.
        // If !dragMoved_, we probably shouldn't trigger a click action blindly.
    } 
    else if (dragMoved_ && onRequestSetViewStart) {
        // Viewport/Pan drag commit
        // Just use current view start as final
        onRequestSetViewStart(model_.view.start, true);
    }

    dragKind_ = DragKind::None;
    dragMoved_ = false;
    repaint();
}

void TimelineMinimapBar::cacheThemeColors_()
{
    auto& theme = NUIThemeManager::getInstance();
    // Near-black shell matching the ruler material exactly (one continuous
    // band; the old surfaceRaised tint read blue against neighboring rows).
    colors_.glassFill = NUIColor(0.012f, 0.012f, 0.012f, 0.98f);
    colors_.glassBorder = theme.getColor("border").withAlpha(0.28f);
    colors_.cornerSeparator = theme.getColor("border").withAlpha(0.28f);

    colors_.audioTint = theme.getColor("accentAmber");
    colors_.midiTint = theme.getColor("accentCyan");
    colors_.automationTint = theme.getColor("accentPrimary");
    colors_.baseline = theme.getColor("textSecondary").withAlpha(0.10f);

    colors_.viewFill = theme.getColor("accentPrimary").withAlpha(0.035f);
    colors_.viewOutline = theme.getColor("accentPrimary").withAlpha(0.62f);
    colors_.selectionFill = theme.getColor("accentCyan").withAlpha(0.10f);
    colors_.loopFill = theme.getColor("accentPrimary").withAlpha(0.08f);

    colors_.playheadDark = NUIColor(0.0f, 0.0f, 0.0f, 0.75f);
    colors_.playheadBright = NUIColor(1.0f, 1.0f, 1.0f, 0.85f);

    colors_.text = theme.getColor("textPrimary");
}

// =============================================================================
// SECTION: Layout & Theme
// =============================================================================

TimelineMinimapLayout TimelineMinimapBar::computeLayout_() const
{
    const NUIRect b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    const auto& layout = theme.getLayoutDimensions();

    const float controlW = showModeToggles_
                               ? std::min(layout.trackControlsWidth, b.width)
                               : std::max(0.0f, std::min(leadingInsetPx_ >= 0.0f ? leadingInsetPx_ : 0.0f, b.width));
    const NUIRect corner(b.x, b.y, std::min(controlW, b.width), b.height);

    const float gridStartX = b.x + controlW + 5.0f;
    const float gridW = std::max(0.0f, b.width - controlW - 10.0f);
    const NUIRect map(gridStartX, b.y + 2.0f, gridW, std::max(0.0f, b.height - 4.0f));

    TimelineMinimapLayout out;
    out.bounds = b;
    out.cornerRect = corner;
    out.mapRect = map;
    return out;
}

NUIRect TimelineMinimapBar::toggleRect_(int index) const
{
    index = std::clamp(index, 0, 2);
    const auto layout = computeLayout_();

    const float totalW = kToggleW * 3.0f + kToggleGap * 2.0f;
    const float startX = std::round(layout.cornerRect.x + (layout.cornerRect.width - totalW) * 0.5f);
    const float y = std::round(layout.cornerRect.y + (layout.cornerRect.height - kToggleH) * 0.5f);
    const float x = startX + index * (kToggleW + kToggleGap);
    return NUIRect{x, y, kToggleW, kToggleH};
}

bool TimelineMinimapBar::hitToggle_(const NUIPoint& p, TimelineMinimapMode& outMode) const
{
    if (!showModeToggles_) return false;
    const NUIRect b = getBounds();
    if (!b.contains(p)) return false;

    for (int i = 0; i < 3; ++i) {
        if (toggleRect_(i).contains(p)) {
            outMode = (i == 0) ? TimelineMinimapMode::Clips : (i == 1) ? TimelineMinimapMode::Energy : TimelineMinimapMode::Automation;
            return true;
        }
    }
    return false;
}

void TimelineMinimapBar::renderToggles_(NUIRenderer& renderer, const TimelineMinimapLayout& layout)
{
    if (!showModeToggles_) return;
    (void)layout;

    static constexpr const char* kLabels[3] = {"C", "E", "A"};

    const auto active = model_.mode;
    for (int i = 0; i < 3; ++i) {
        toggleBounds_[i] = toggleRect_(i);

        const bool isActive = (active == ((i == 0) ? TimelineMinimapMode::Clips
                                                   : (i == 1) ? TimelineMinimapMode::Energy : TimelineMinimapMode::Automation));

        NUIColor fill = NUIColor(0.0f, 0.0f, 0.0f, 0.0f);
        NUIColor border = colors_.glassBorder.withAlpha(0.30f);
        NUIColor text = colors_.text.withAlpha(0.68f);

        if (isActive) {
            const NUIColor tint =
                (i == 0) ? colors_.audioTint : (i == 1) ? colors_.audioTint : colors_.automationTint;
            fill = tint.withAlpha(0.16f);
            border = tint.withAlpha(0.52f);
            text = colors_.text.withAlpha(0.86f);
        }

        renderer.fillRoundedRect(toggleBounds_[i], kToggleRadius, fill);
        renderer.strokeRoundedRect(toggleBounds_[i], kToggleRadius, 1.0f, border);
        renderer.drawTextCentered(kLabels[i], toggleBounds_[i],
                                  NUIThemeManager::getInstance().getFontSize("micro"), text);
    }
}

std::string TimelineMinimapBar::formatHoverText_(double hoverBeat) const
{
    const int bpb = std::max(1, model_.beatsPerBar);
    const double clamped = std::max(0.0, hoverBeat);
    const int barIndex = static_cast<int>(std::floor(clamped / static_cast<double>(bpb)));
    const double beatInBar0 = clamped - static_cast<double>(barIndex * bpb);
    const int beatNum = static_cast<int>(std::floor(beatInBar0)) + 1;

    std::string text = "Bar " + std::to_string(barIndex + 1) + "  Beat " + std::to_string(beatNum);

    // Bucket info (if available).
    if (model_.summary && model_.summary->summary) {
        const TimelineSummary* s = model_.summary->summary;
        const double denom = s->domainEndBeat - s->domainStartBeat;
        if (denom > 1e-9 && s->bucketCount > 0) {
            const double u = std::max(0.0, std::min(1.0, (clamped - s->domainStartBeat) / denom));
            const int idx = std::clamp(static_cast<int>(std::floor(u * static_cast<double>(s->bucketCount))),
                                       0, static_cast<int>(s->bucketCount) - 1);
            const auto& bk = s->buckets[static_cast<size_t>(idx)];
            const int audio = std::max(0, bk.audioCount);
            const int midi = std::max(0, bk.midiCount);
            const int autom = std::max(0, bk.automationCount);
            text += "   Clips " + std::to_string(audio + midi + autom);
        }
    }

    return text;
}

// =============================================================================
// SECTION: Rendering
// =============================================================================

void TimelineMinimapBar::onRender(NUIRenderer& renderer)
{
    cacheThemeColors_();

    const auto layout = computeLayout_();
    renderer_.render(renderer, layout, model_, colors_);
    renderToggles_(renderer, layout);

    // Active feedback: purple outline on click/drag + edge handles for resizing.
    const TimelineSummary* s = (model_.summary) ? model_.summary->summary : nullptr;
    if (s && model_.view.isValid()) {
        const double denom = s->domainEndBeat - s->domainStartBeat;
        if (denom > 1e-9 && !layout.mapRect.isEmpty()) {
            const float x0 =
                TimelineMinimapRenderer::timeToX(model_.view.start, layout.mapRect, s->domainStartBeat, s->domainEndBeat);
            const float x1 =
                TimelineMinimapRenderer::timeToX(model_.view.end, layout.mapRect, s->domainStartBeat, s->domainEndBeat);
            const float vx = std::min(x0, x1);
            const float vw = std::max(1.0f, std::abs(x1 - x0));
            const NUIRect vr(vx, layout.mapRect.y, vw, layout.mapRect.height);

            if (dragKind_ != DragKind::None || !showModeToggles_) {
                const NUIColor active = NUIThemeManager::getInstance().getColor("borderActive").withAlpha(0.85f);
                renderer.strokeRoundedRect(vr, 5.0f, 1.0f, active);
            }

            const bool leftHot = (hoverOnResizeEdge_ && hoverResizeEdge_ == TimelineMinimapResizeEdge::Left) ||
                                 (dragKind_ == DragKind::ResizeLeft);
            const bool rightHot = (hoverOnResizeEdge_ && hoverResizeEdge_ == TimelineMinimapResizeEdge::Right) ||
                                  (dragKind_ == DragKind::ResizeRight);
            if (leftHot || rightHot || !showModeToggles_ || model_.view.isValid()) {
                const NUIColor handleFill = NUIColor::white().withAlpha(0.30f);
                const NUIColor handleStroke = colors_.viewOutline.withAlpha(0.95f);
                constexpr float handleW = 8.0f;
                const float handleH = std::max(8.0f, vr.height - 8.0f);

                if (leftHot || !showModeToggles_ || model_.view.isValid()) {
                    NUIRect r(vr.x + 2.0f, vr.y + 4.0f, handleW, handleH);
                    renderer.fillRoundedRect(r, 3.0f, handleFill);
                    renderer.strokeRoundedRect(r, 3.0f, 1.0f, handleStroke);
                }
                if (rightHot || !showModeToggles_ || model_.view.isValid()) {
                    NUIRect r(vr.right() - handleW - 2.0f, vr.y + 4.0f, handleW, handleH);
                    renderer.fillRoundedRect(r, 3.0f, handleFill);
                    renderer.strokeRoundedRect(r, 3.0f, 1.0f, handleStroke);
                }
            }
        }
    }
}

void TimelineMinimapBar::onMouseLeave()
{
    hoverInMap_ = false;
    hoverToggleIndex_ = -1;
    hoverOnResizeEdge_ = false;
    AestraUI::NUIComponent::hideRemoteTooltip();
    if (dragKind_ == DragKind::ResizeLeft || dragKind_ == DragKind::ResizeRight) {
        cursorHint_ = TimelineMinimapCursorHint::ResizeHorizontal;
    } else {
        cursorHint_ = TimelineMinimapCursorHint::Default;
    }
    NUIComponent::onMouseLeave();
}

// =============================================================================
// SECTION: Event Handling
// =============================================================================

bool TimelineMinimapBar::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    const auto layout = computeLayout_();
    const bool inBounds = layout.bounds.contains(event.position);
    if (!inBounds && dragKind_ == DragKind::None) {
        hoverInMap_ = false;
        hoverToggleIndex_ = -1;
        hoverOnResizeEdge_ = false;
        cursorHint_ = TimelineMinimapCursorHint::Default;
        AestraUI::NUIComponent::hideRemoteTooltip();
        repaint();
        return false;
    }

    // Toggle hover (for tooltips).
    {
        const int prevHoverToggle = hoverToggleIndex_;
        if (inBounds) {
            hoverToggleIndex_ = -1;
            for (int i = 0; i < 3; ++i) {
                if (toggleRect_(i).contains(event.position)) {
                    hoverToggleIndex_ = i;
                    break;
                }
            }
        } else {
            hoverToggleIndex_ = -1;
        }
        if (hoverToggleIndex_ != prevHoverToggle) {
            repaint();
        }
    }

    // Hover tracking (for tooltip).
    if (inBounds) {
        const bool prevHoverInMap = hoverInMap_;
        hoverPos_ = event.position;
        if (layout.mapRect.contains(event.position) && model_.summary && model_.summary->summary) {
            const TimelineSummary* s = model_.summary->summary;
            hoverBeat_ = TimelineMinimapRenderer::xToTime(event.position.x, layout.mapRect, s->domainStartBeat, s->domainEndBeat);
            hoverInMap_ = true;
            repaint();
        } else {
            hoverInMap_ = false;
            if (prevHoverInMap) repaint();
        }
    }

    if (!event.cursorCaptured) {
        if (showModeToggles_ && hoverToggleIndex_ >= 0 && hoverToggleIndex_ < 3) {
            std::string text;
            if (hoverToggleIndex_ == 0) text = "C: Clips (where audio/MIDI exists)";
            else if (hoverToggleIndex_ == 1) text = "E: Energy (approx. loudness per region)";
            else text = "A: Automation (where automation exists)";
            AestraUI::NUIComponent::showRemoteTooltip(text, event.position);
        } else if (hoverInMap_) {
            AestraUI::NUIComponent::showRemoteTooltip(formatHoverText_(hoverBeat_), event.position);
        } else {
            AestraUI::NUIComponent::hideRemoteTooltip();
        }
    } else {
        // Clear any stale minimap tooltip shown before capture started
        // (won't clear component-owned tooltips like volume knob's)
        AestraUI::NUIComponent::hideRemoteTooltip();
    }

    // Mode toggles.
    if (event.pressed && event.button == NUIMouseButton::Left) {
        TimelineMinimapMode hitMode{};
        if (hitToggle_(event.position, hitMode)) {
            model_.mode = hitMode;
            repaint();
            if (onModeChanged) onModeChanged(hitMode);
            return true;
        }
    }

    const TimelineSummary* s = (model_.summary) ? model_.summary->summary : nullptr;
    if (!s) {
        hoverOnResizeEdge_ = false;
        cursorHint_ = (dragKind_ == DragKind::ResizeLeft || dragKind_ == DragKind::ResizeRight)
                          ? TimelineMinimapCursorHint::ResizeHorizontal
                          : TimelineMinimapCursorHint::Default;
        if (dragKind_ != DragKind::None && event.released && event.button == NUIMouseButton::Left) {
            dragKind_ = DragKind::None;
            dragMoved_ = false;
            return true;
        }
        return NUIComponent::onMouseEvent(event);
    }

    if (!layout.mapRect.contains(event.position) && dragKind_ == DragKind::None) {
        return NUIComponent::onMouseEvent(event);
    }

    const double domainStart = s->domainStartBeat;
    const double domainEnd = s->domainEndBeat;
    const double denom = domainEnd - domainStart;
    if (!(denom > 1e-9)) {
        hoverOnResizeEdge_ = false;
        cursorHint_ = TimelineMinimapCursorHint::Default;
        if (dragKind_ != DragKind::None && event.released && event.button == NUIMouseButton::Left) {
            dragKind_ = DragKind::None;
            dragMoved_ = false;
        }
        return true;
    }

    const double mouseBeat = TimelineMinimapRenderer::xToTime(event.position.x, layout.mapRect, domainStart, domainEnd);
    const double viewW = std::max(0.0, model_.view.end - model_.view.start);

    // Wheel: pan by default; Ctrl+wheel: zoom around mouse beat.
    if (event.wheelDelta != 0.0f) {
        const bool ctrlHeld = (event.modifiers & NUIModifiers::Ctrl);
        if (ctrlHeld) {
            const float factor = (event.wheelDelta > 0.0f) ? 1.15f : 0.87f;
            if (onRequestZoomAround) onRequestZoomAround(mouseBeat, factor);
            return true;
        }

        if (viewW > 0.0 && onRequestSetViewStart) {
            const double delta = -static_cast<double>(event.wheelDelta) * viewW * 0.12;
            double newStart = model_.view.start + delta;
            newStart = std::max(domainStart, std::min(newStart, domainEnd - viewW));
            onRequestSetViewStart(newStart, true);
            return true;
        }
    }

    // Compute viewport rect in minimap space (hit test for dragging).
    NUIRect viewRect{};
    if (model_.view.isValid()) {
        const float x0 = TimelineMinimapRenderer::timeToX(model_.view.start, layout.mapRect, domainStart, domainEnd);
        const float x1 = TimelineMinimapRenderer::timeToX(model_.view.end, layout.mapRect, domainStart, domainEnd);
        const float vx = std::min(x0, x1);
        const float vw = std::max(1.0f, std::abs(x1 - x0));
        viewRect = NUIRect(vx, layout.mapRect.y, vw, layout.mapRect.height);
    }

    // Edge hover (resize affordance + cursor hint).
    hoverOnResizeEdge_ = false;
    if (!viewRect.isEmpty() && layout.mapRect.contains(event.position)) {
        if (event.position.y >= viewRect.y && event.position.y <= viewRect.bottom()) {
            const float dxL = std::fabs(event.position.x - viewRect.x);
            const float dxR = std::fabs(event.position.x - viewRect.right());
            const float edgeHit = edgeHitPxForWidth(viewRect.width);
            if (dxL <= edgeHit || dxR <= edgeHit) {
                hoverOnResizeEdge_ = true;
                hoverResizeEdge_ =
                    (dxL <= dxR) ? TimelineMinimapResizeEdge::Left : TimelineMinimapResizeEdge::Right;
            }
        }
    }

    if (dragKind_ == DragKind::ResizeLeft || dragKind_ == DragKind::ResizeRight || hoverOnResizeEdge_) {
        cursorHint_ = TimelineMinimapCursorHint::ResizeHorizontal;
    } else {
        cursorHint_ = TimelineMinimapCursorHint::Default;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        dragStartPos_ = event.position;
        dragMoved_ = false;
        dragCtrlFast_ = (event.modifiers & NUIModifiers::Ctrl);

        if (hoverOnResizeEdge_ && model_.view.isValid() && onRequestResizeViewEdge) {
            if (hoverResizeEdge_ == TimelineMinimapResizeEdge::Left) {
                dragKind_ = DragKind::ResizeLeft;
                dragAnchorBeat_ = model_.view.end;
            } else {
                dragKind_ = DragKind::ResizeRight;
                dragAnchorBeat_ = model_.view.start;
            }
            cursorHint_ = TimelineMinimapCursorHint::ResizeHorizontal;
            repaint();
            return true;
        }

        if (viewRect.contains(event.position)) {
            dragKind_ = DragKind::Viewport;
            dragStartViewStartBeat_ = model_.view.start;
            dragGrabOffsetBeat_ = mouseBeat - model_.view.start;
            return true;
        }

        dragKind_ = DragKind::Pan;
        dragStartViewStartBeat_ = model_.view.start;
        dragGrabOffsetBeat_ = mouseBeat - model_.view.start;
        return true;
    }

    if (dragKind_ != DragKind::None) {
        const float d2 = dist2(event.position, dragStartPos_);
        if (!dragMoved_ && d2 >= (kDragThresholdPx * kDragThresholdPx)) {
            dragMoved_ = true;
        }

        if (event.released && event.button == NUIMouseButton::Left) {
            // Updated to use endDrag_() which handles logic for storage/clearing
            // But endDrag_ needs to know the specific target mouseBeat for accuracy?
            // The helper I wrote in onUpdate uses model state. 
            // Here we have exact mouseBeat.
            
            // To avoid code duplication and support both, let's just use the inline logic here 
            // (since we have better data) and clear state.
            // AND update endDrag_ to be a fallback that uses model state.
            
            // Actually, let's keep the inline logic for precision, but ensure we clear state.
             if ((dragKind_ == DragKind::ResizeLeft || dragKind_ == DragKind::ResizeRight) && dragMoved_ &&
                onRequestResizeViewEdge) {
                const auto edge = (dragKind_ == DragKind::ResizeLeft) ? TimelineMinimapResizeEdge::Left
                                                                      : TimelineMinimapResizeEdge::Right;
                onRequestResizeViewEdge(edge, dragAnchorBeat_, mouseBeat, true);
            } else if (!dragMoved_ && (dragKind_ == DragKind::Viewport || dragKind_ == DragKind::Pan) && onRequestCenterView) {
                // Click (no drag): center viewport.
                onRequestCenterView(mouseBeat);
            } else if (dragMoved_ && onRequestSetViewStart && viewW > 0.0) {
                double newStart = mouseBeat - dragGrabOffsetBeat_;
                if (dragCtrlFast_) {
                    const double extra =
                        (mouseBeat - TimelineMinimapRenderer::xToTime(dragStartPos_.x, layout.mapRect, domainStart, domainEnd));
                    newStart = dragStartViewStartBeat_ + extra * 2.0;
                }
                newStart = std::max(domainStart, std::min(newStart, domainEnd - viewW));
                onRequestSetViewStart(newStart, true);
            }

            dragKind_ = DragKind::None;
            dragMoved_ = false;
            repaint();
            return true;
        }

        if (dragKind_ == DragKind::ResizeLeft || dragKind_ == DragKind::ResizeRight) {
            if (onRequestResizeViewEdge) {
                const auto edge = (dragKind_ == DragKind::ResizeLeft) ? TimelineMinimapResizeEdge::Left
                                                                      : TimelineMinimapResizeEdge::Right;
                onRequestResizeViewEdge(edge, dragAnchorBeat_, mouseBeat, false);
                return true;
            }
        }

        if (onRequestSetViewStart && viewW > 0.0) {
            double newStart = mouseBeat - dragGrabOffsetBeat_;
            if (dragCtrlFast_) {
                const double startBeat = TimelineMinimapRenderer::xToTime(dragStartPos_.x, layout.mapRect, domainStart, domainEnd);
                const double delta = (mouseBeat - startBeat) * 2.0;
                newStart = dragStartViewStartBeat_ + delta;
            }
            newStart = std::max(domainStart, std::min(newStart, domainEnd - viewW));
            onRequestSetViewStart(newStart, false);
            return true;
        }
    }

    return true;
}

} // namespace AestraUI
