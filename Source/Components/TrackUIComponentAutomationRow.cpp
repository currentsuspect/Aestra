// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-T1 (FD-50 board 8): automation as a lane of its own under its track. "Automate Volume" opens one row under
// that track; other tracks and their clips stay as they are. The row is a TrackUIComponent for the same playlist
// lane in automation mode: it draws no clips, its curve is the track's colour with small square points, and its
// header names the parameter (▾ switches it), shows the value at the playhead, and × hides the row without
// deleting the curve. A track whose automation row is hidden but holds points shows a small curve mark next to
// its name; clicking it brings the row back. Kept out of TrackUIComponent.cpp, which is at its size pin.
#include "TrackUIComponent.h"

#include "AutomationLaneEditing.h"
#include "NUIContextMenu.h"
#include "TimelineClipPaint.h"
#include "TrackManager.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

namespace {

constexpr float kHideBoxSize = 16.0f;

AestraUI::NUIComponent* rootOf(AestraUI::NUIComponent* component) {
    while (component && component->getParent()) component = component->getParent();
    return component;
}

} // namespace

void TrackUIComponent::setAutomationRow(AutomationTarget target, std::function<void(AutomationTarget)> onSwitch,
                                        std::function<void()> onHide) {
    m_isAutomationRow = true;
    m_playlistMode = PlaylistMode::Automation;
    m_onAutomationRowSwitch = std::move(onSwitch);
    m_onAutomationRowHide = std::move(onHide);
    setEditedAutomationTarget(target);
    hideTrackControlsForAutomationRow();
}

void TrackUIComponent::hideTrackControlsForAutomationRow() {
    for (const auto& widget : {std::static_pointer_cast<AestraUI::NUIComponent>(m_muteButton),
                               std::static_pointer_cast<AestraUI::NUIComponent>(m_soloButton),
                               std::static_pointer_cast<AestraUI::NUIComponent>(m_recordButton),
                               std::static_pointer_cast<AestraUI::NUIComponent>(m_nameLabel),
                               std::static_pointer_cast<AestraUI::NUIComponent>(m_expandButton),
                               std::static_pointer_cast<AestraUI::NUIComponent>(m_laneCountLabel),
                               std::static_pointer_cast<AestraUI::NUIComponent>(m_laneCountIcon)}) {
        if (widget) widget->setVisible(false);
    }
}

void TrackUIComponent::renderAutomationRowHeader(AestraUI::NUIRenderer& renderer,
                                                 const AestraUI::NUIRect& controlArea) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const PlaylistLane* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;
    if (!lane) return;
    const AestraUI::NUIColor ink = ClipPaint::identityFromARGB(lane->colorRGBA);

    // One step lighter than the track's chrome, with the track's colour bar, so it reads as part of the track.
    renderer.fillRect(controlArea, theme.getColor("trackChrome"));
    renderer.fillRect(controlArea, AestraUI::NUIColor::white().withAlpha(0.03f));
    renderer.fillRect({controlArea.x, controlArea.y, 3.0f, controlArea.height}, ink);
    renderer.drawLine({controlArea.right(), controlArea.y}, {controlArea.right(), controlArea.bottom()}, 1.0f,
                      theme.getColor("borderSubtle").withAlpha(0.24f));

    // "↳ Volume ▾" — the parameter, and the switch. Arrow and caret are drawn: the UI font has neither glyph.
    const float labelSize = theme.getFontSize("s");
    const std::string label = automationTargetLabel(m_editedAutomationTarget);
    const AestraUI::NUIColor text = theme.getColor("textPrimary").withAlpha(0.86f);
    const float cy = std::round(controlArea.y + controlArea.height * 0.5f);
    const float arrowX = controlArea.x + 18.0f;
    renderer.fillRect({arrowX, cy - 6.0f, 1.0f, 6.0f}, text.withAlpha(0.5f)); // ↳: down, then right to an arrowhead
    renderer.fillRect({arrowX, cy, 7.0f, 1.0f}, text.withAlpha(0.5f));
    renderer.fillRect({arrowX + 5.0f, cy - 2.0f, 1.0f, 5.0f}, text.withAlpha(0.5f));
    const float labelX = arrowX + 12.0f;
    const float labelWidth = renderer.measureText(label, labelSize).width;
    renderer.drawText(label, {labelX, std::round(renderer.calculateTextY(controlArea, labelSize))}, labelSize, text);
    const float caretX = labelX + labelWidth + 7.0f;
    for (int i = 0; i < 3; ++i) { // ▾, as the toolbar's SNAP caret
        renderer.fillRect({caretX - 2.0f + static_cast<float>(i), cy - 1.0f + static_cast<float>(i),
                           5.0f - 2.0f * static_cast<float>(i), 1.0f},
                          text.withAlpha(0.6f));
    }
    m_automationTargetRect = {arrowX - 4.0f, controlArea.y + 4.0f, caretX + 7.0f - (arrowX - 4.0f), controlArea.height - 8.0f};

    // × at the right edge, then the value at the playhead to its left, in the track's colour.
    m_automationHideRect = {controlArea.right() - kHideBoxSize - 6.0f,
                            std::round(controlArea.y + (controlArea.height - kHideBoxSize) * 0.5f), kHideBoxSize,
                            kHideBoxSize};
    renderer.drawTextCentered("\xC3\x97", m_automationHideRect, labelSize, theme.getColor("textSecondary").withAlpha(0.6f));

    const int curveIndex = automationCurveIndexFor(lane->automationCurves, m_editedAutomationTarget);
    const double beat = m_trackManager->getUIPosition() * std::max(1.0, m_trackManager->getPlaylistModel().getBPM()) / 60.0;
    // A selected node's level when one is picked (a click says "where is THIS one"), else the level at the playhead.
    const AutomationHeaderReadout readout = automationHeaderReadout(
        curveIndex >= 0 ? &lane->automationCurves[static_cast<size_t>(curveIndex)] : nullptr,
        makeAutomationCurve(m_editedAutomationTarget).getDefaultValue(), beat);
    const std::string valueText = automationValueLabel(m_editedAutomationTarget, readout.value);
    const float valueSize = theme.getFontSize("xs");
    const float markerWidth = readout.isNode ? 9.0f : 0.0f; // a node's value wears the node's square
    const float valueWidth = renderer.measureText(valueText, valueSize).width + markerWidth;
    const float valueX = std::max(m_automationTargetRect.right() + 6.0f, m_automationHideRect.x - 6.0f - valueWidth);
    if (readout.isNode) {
        renderer.fillRect({valueX, cy - 2.5f, 5.0f, 5.0f}, AestraUI::NUIColor::white());
    }
    renderer.drawText(valueText, {valueX + markerWidth, std::round(renderer.calculateTextY(controlArea, valueSize))},
                      valueSize, ink);
}

bool TrackUIComponent::handleAutomationRowHeaderMouse(const AestraUI::NUIMouseEvent& event) {
    const bool leftPress = event.pressed && event.button == AestraUI::NUIMouseButton::Left;
    if (!m_isAutomationRow) {
        // A track row's mark for hidden automation brings its row back.
        if (leftPress && m_hasHiddenAutomation && m_onShowAutomation &&
            m_hiddenAutomationMarkRect.contains(event.position)) {
            m_onShowAutomation();
            return true;
        }
        return false;
    }
    const auto& layout = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions();
    const AestraUI::NUIRect bounds = getBounds();
    // The header ends kAutomationEdgeSlop short of the lane: a point at beat 0 sits ON the lane's left edge, and its
    // left half belongs to the lane (it used to be swallowed here, so the first point could not be picked up).
    const AestraUI::NUIRect controlArea(bounds.x, bounds.y,
                                        std::max(0.0f, std::min(layout.trackControlsWidth, bounds.width) - kAutomationEdgeSlop),
                                        bounds.height);
    if (!controlArea.contains(event.position)) return false;
    if (leftPress && m_automationHideRect.contains(event.position)) {
        if (m_onAutomationRowHide) m_onAutomationRowHide();
        return true;
    }
    if (leftPress && m_automationTargetRect.contains(event.position)) {
        showAutomationTargetMenu({m_automationTargetRect.x, m_automationTargetRect.bottom()});
        return true;
    }
    return event.pressed || event.released; // the header is the row's own: no track selection or M/S/R under it
}

void TrackUIComponent::showAutomationTargetMenu(const AestraUI::NUIPoint& anchor) {
    if (m_automationTargetMenu) {
        if (auto* parent = m_automationTargetMenu->getParent()) parent->removeChild(m_automationTargetMenu);
    }
    m_automationTargetMenu = std::make_shared<AestraUI::NUIContextMenu>();
    for (const AutomationTarget target : {AutomationTarget::Volume, AutomationTarget::Pan}) {
        m_automationTargetMenu->addRadioItem(automationTargetLabel(target), "automationRowTarget",
                                             target == m_editedAutomationTarget,
                                             [this, weakSelf = weak_from_this(), target]() {
                                                 if (weakSelf.expired()) return;
                                                 setEditedAutomationTarget(target);
                                                 if (m_onAutomationRowSwitch) m_onAutomationRowSwitch(target);
                                             });
    }
    AestraUI::NUIComponent* root = rootOf(this);
    if (!root) return;
    root->addChild(m_automationTargetMenu);
    m_automationTargetMenu->setOwner(weak_from_this());
    m_automationTargetMenu->showAt(anchor);
    root->repaint();
}

void TrackUIComponent::renderAutomationLayer(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float gridStartX) {
    auto& playlist = m_trackManager->getPlaylistModel();
    auto lane = playlist.getLane(m_laneId);
    if (!lane) return;

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    // FD-50 board 8: the curve is the track's colour, points small squares.
    const AestraUI::NUIColor ink = ClipPaint::identityFromARGB(lane->colorRGBA);
    
    // Automation Area bounds (exclude controls)
    AestraUI::NUIRect gridArea(gridStartX, bounds.y, bounds.width - (gridStartX - bounds.x), bounds.height);

    // V8-A2: the edited curve draws at full strength, the others recede, so the
    // lane shows which curve a click edits while keeping every curve visible.
    int editedIndex = automationCurveIndexFor(lane->automationCurves, m_editedAutomationTarget);
    if (editedIndex < 0 && !m_automationTargetChosen && !lane->automationCurves.empty()) editedIndex = 0;
    renderAutomationClips(renderer, bounds, gridStartX, editedIndex);
    for (size_t ci = 0; ci < lane->automationCurves.size(); ++ci) {
        const auto& curve = lane->automationCurves[ci];
        if (!curve.isVisible()) continue;
        const bool edited = static_cast<int>(ci) == editedIndex;
        const AutomationTarget target = curve.getAutomationTarget();
        auto yOf = [&](double v) {
            return gridArea.y + (1.0f - static_cast<float>(automationHeightOfValue(target, v))) * gridArea.height;
        };

        const auto& points = curve.getPoints();
        if (points.empty()) {
            // Draw a flat line at default value if no points
            float y = yOf(curve.getDefaultValue());
            renderer.drawLine(AestraUI::NUIPoint(gridArea.x, y),
                              AestraUI::NUIPoint(gridArea.right(), y),
                              1.5f, ink.withAlpha(edited ? 0.55f : 0.2f));
            continue;
        }

        double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 120.0;
        double sampleRate = m_trackManager ? m_trackManager->getPlaylistModel().getProjectSampleRate() : 48000.0;
        double samplesPerBeat = (sampleRate * 60.0) / std::max(bpm, 1.0);

        AestraUI::NUIColor curveColor = ink.withAlpha(edited ? 1.0f : 0.35f);

        // Draw lines between points
        std::vector<AestraUI::NUIPoint> polyPoints;
        polyPoints.reserve(1024);

        for (size_t i = 1; i < points.size(); ++i) {
            const auto& p1 = points[i-1];
            const auto& p2 = points[i];
            
            const float sx1 = gridStartX + (static_cast<float>(p1.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            const float sy1 = yOf(p1.value);
            const float sx2 = gridStartX + (static_cast<float>(p2.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            const float sy2 = yOf(p2.value);

            // Only the part of the segment inside the lane is drawn, sampled at full density (visibleSegmentSpan).
            const SegmentSpan span = visibleSegmentSpan(sx1, sx2, gridArea.x, gridArea.right());
            if (span.empty()) continue; // wholly left or right of the lane
            const float t0 = span.t0, t1 = span.t1;

            // Adaptive subdivision based on the visible screen length: one vertex per pixel.
            const float dist = std::hypot((sx2 - sx1) * (t1 - t0), (sy2 - sy1) * (t1 - t0));
            const int steps = std::clamp(static_cast<int>(dist), 4, 512);

            for (int s = 0; s <= steps; ++s) {
                 const double t = static_cast<double>(t0) + static_cast<double>(t1 - t0) * (static_cast<double>(s) / static_cast<double>(steps));
                 const double beat = p1.beat + (p2.beat - p1.beat) * t;
                 const double val = curve.getValueAtBeat(beat, samplesPerBeat);

                 const float x = gridStartX + (static_cast<float>(beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                 const float y = yOf(val);

                 polyPoints.emplace_back(x, y);
            }
        }
        
        if (polyPoints.size() >= 2) {
            // Use thick 2px capsules - high subdivision minimizes angle between segments so joints are invisible
            renderer.drawPolyline(polyPoints.data(), static_cast<int>(polyPoints.size()), 2.0f, curveColor);
        }
            
        
        // Draw endpoints before/after first/last point if they are within view
        if (!points.empty()) {
            const auto& first = points.front();
            float fx = gridStartX + (static_cast<float>(first.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float fy = yOf(first.value);
            if (fx > gridArea.x) {
                renderer.drawLine(AestraUI::NUIPoint(gridArea.x, fy), AestraUI::NUIPoint(fx, fy), 1.5f, curveColor.withAlpha(0.5f));
            }
            
            const auto& last = points.back();
            float lx = gridStartX + (static_cast<float>(last.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float ly = yOf(last.value);
            if (lx < gridArea.right()) {
                renderer.drawLine(AestraUI::NUIPoint(lx, ly), AestraUI::NUIPoint(gridArea.right(), ly), 1.5f, curveColor.withAlpha(0.5f));
            }
        }

        // Draw point handles
        for (const auto& p : points) {
            float x = gridStartX + (static_cast<float>(p.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float y = yOf(p.value);

            if (x < gridArea.x || x > gridArea.right()) continue;
            
            const AestraUI::NUIColor ptColor = p.selected ? AestraUI::NUIColor::white() : curveColor;
            renderer.fillRect(AestraUI::NUIRect(std::round(x) - 2.5f, std::round(y) - 2.5f, 5.0f, 5.0f), ptColor);
        }
    }
}

} // namespace Audio
} // namespace Aestra
