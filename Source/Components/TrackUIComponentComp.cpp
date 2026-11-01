// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Comping on the timeline (SPEC-022 CT-3, FD-70, FD-71 tab B). On a take lane of a track that has takes:
//   * drag across the lane       -> that span of this take plays on the comp (one drag = one SetCompRangeCommand = one undo)
//   * double-click               -> this take plays in place of the piece under the pointer
//   * Shift + double-click       -> this take plays for the whole take
//   * a single click             -> nothing (owner, 2026-11-01: a lone click must never change the comp; flow needs a guarantee)
// The live drag is drawn in violet; once committed, the part of a take the comp does not use is dimmed (the used part
// keeps the track's own colour, which the clips already paint). Kept out of TrackUIComponent.cpp, which is at its pin.
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "Commands/SetCompRangeCommand.h"
#include "TrackManagerUI.h"
#include "TrackUIComponent.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Aestra {
namespace Audio {

namespace {
constexpr float kDragThresholdPx = 4.0f;
constexpr long long kHintMs = 1400; // how long the single-click highlight lasts

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool laneHoldsClips(const PlaylistModel& playlist, PlaylistLaneID id) {
    const PlaylistLane* lane = const_cast<PlaylistModel&>(playlist).getLane(id);
    return lane && !lane->clips.empty();
}
} // namespace

/// A take lane of a recording with takes: not the comp lane, and at least two of the track's lanes hold audio.
bool TrackUIComponent::isCompTakeLane() const {
    if (!m_trackManager || m_isAutomationRow || m_playlistMode == PlaylistMode::Automation)
        return false;
    auto& playlist = m_trackManager->getPlaylistModel();
    const PlaylistLane* lane = playlist.getLane(m_laneId);
    const Track* track = lane ? m_trackManager->getTrack(lane->trackId) : nullptr;
    if (!track || m_laneId == track->compLaneId || lane->clips.empty())
        return false;
    size_t withClips = 0;
    for (const PlaylistLaneID& id : track->laneIds)
        if (id != track->compLaneId && laneHoldsClips(playlist, id))
            ++withClips;
    return withClips >= 2;
}

bool TrackUIComponent::handleCompSwipe(const AestraUI::NUIMouseEvent& event) {
    auto* manager = dynamic_cast<TrackManagerUI*>(getParent());
    if (!manager)
        return false;
    const PlaylistTool tool = manager->getCurrentTool();
    const bool leftPress = event.pressed && event.button == AestraUI::NUIMouseButton::Left;
    if (!m_compPressed) {
        if (!leftPress || (tool != PlaylistTool::Select && tool != PlaylistTool::Draw) || !isCompTakeLane())
            return false;
        if (event.modifiers & AestraUI::NUIModifiers::Alt)
            return false; // Alt+click is the Mute tool's, with any tool
        const float controls = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
        const AestraUI::NUIRect bounds = getBounds();
        if (!bounds.contains(event.position) || event.position.x < bounds.x + controls)
            return false; // the header keeps its buttons
        // A double-click is two presses close in time and place. The platform flag is not trusted (the clip code keeps
        // its own timer for the same reason), and it is read on the PRESS, not the release.
        const long long now = nowMs();
        m_compDouble = event.doubleClick || (now - m_compLastClickMs < 400 && std::fabs(event.position.x - m_compLastClickX) < 6.0f);
        m_compShift = event.modifiers & AestraUI::NUIModifiers::Shift;
        m_compLastClickMs = m_compDouble ? 0 : now; // a double-click uses the pair up: a third click starts afresh
        m_compLastClickX = event.position.x;
        m_compPressed = true;
        m_compDragging = false;
        m_compPressX = event.position.x;
        m_compAnchorBeat = m_compEndBeat = beatAtX(event.position.x);
        return true;
    }

    // A gesture is under way: this lane owns the pointer until the button comes up.
    m_compEndBeat = beatAtX(event.position.x);
    if (!event.released) {
        if (std::fabs(event.position.x - m_compPressX) > kDragThresholdPx && !m_compDragging)
            m_compDragging = true;
        if (m_compDragging) {
            repaint();
            if (m_onCacheInvalidationCallback)
                m_onCacheInvalidationCallback();
        }
        return true;
    }

    const bool dragged = m_compDragging;
    const double anchor = m_compAnchorBeat;
    m_compPressed = m_compDragging = false;
    if (event.synthetic) { // focus loss: stop, do not commit
        repaint();
        return true;
    }
    double start = anchor, end = m_compEndBeat;
    if (dragged) {
        start = snapBeatToGrid(std::min(anchor, m_compEndBeat));
        end = snapBeatToGrid(std::max(anchor, m_compEndBeat));
    } else if (!m_compDouble || !compSpanForClick(anchor, m_compShift, start, end)) {
        // A single click changes nothing; it only shows what a double-click here WOULD replace, fading out.
        if (!m_compDouble && compSpanForClick(anchor, false, m_compHintStart, m_compHintEnd)) m_compHintMs = nowMs();
        repaint();
        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
        return true;
    }
    m_compHintMs = 0;
    commitCompSpan(start, end);
    repaint();
    if (m_onCacheInvalidationCallback)
        m_onCacheInvalidationCallback();
    manager->refreshTracks();
    return true;
}

double TrackUIComponent::beatAtX(float x) const {
    const float controls = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
    const float gridStart = timelineGridStartX(getBounds().x, std::min(controls, getBounds().width));
    return std::max(0.0,
                    (static_cast<double>(x - gridStart) + m_timelineScrollOffset) / std::max(1.0f, m_pixelsPerBeat));
}

/// What a click means. A click plays this take where the comp already has a piece (swap that piece), else across the
/// stretch of this take under the pointer; a double-click plays this take for the whole take.
bool TrackUIComponent::compSpanForClick(double beat, bool wholeTake, double& start, double& end) const {
    auto& playlist = m_trackManager->getPlaylistModel();
    const PlaylistLane* lane = playlist.getLane(m_laneId);
    const Track* track = lane ? m_trackManager->getTrack(lane->trackId) : nullptr;
    if (!track || lane->clips.empty())
        return false;
    double first = lane->clips.front().startBeat, last = lane->clips.front().endBeat();
    for (const ClipInstance& c : lane->clips) {
        first = std::min(first, c.startBeat);
        last = std::max(last, c.endBeat());
    }
    if (wholeTake) {
        start = first;
        end = last;
        return end > start;
    }
    for (const CompRange& r : track->comp.ranges()) {
        if (beat >= r.startBeat && beat < r.endBeat) {
            start = std::max(r.startBeat, first);
            end = std::min(r.endBeat, last);
            return end > start;
        }
    }
    for (const ClipInstance& c : lane->clips) {
        if (beat >= c.startBeat && beat < c.endBeat()) {
            start = c.startBeat;
            end = c.endBeat();
            return true;
        }
    }
    return false;
}

void TrackUIComponent::commitCompSpan(double start, double end) {
    if (end - start <= kCompEpsilon)
        return;
    const PlaylistLane* lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
    if (!lane)
        return;
    auto cmd = std::make_shared<SetCompRangeCommand>(*m_trackManager, lane->trackId, start, end, m_laneId);
    m_trackManager->getCommandHistory().pushAndExecute(cmd);
}

/// A hole inside the comp (a range that lost its take) is silence, and says so: hatched and labelled, never blank.
void TrackUIComponent::drawCompGaps(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float gridStart,
                                    const Track& track) {
    const auto& ranges = track.comp.ranges();
    const AestraUI::NUIColor ink = AestraUI::NUIThemeManager::getInstance().getColor("textSecondary");
    for (size_t i = 0; i + 1 < ranges.size(); ++i) {
        if (ranges[i + 1].startBeat - ranges[i].endBeat <= kCompEpsilon)
            continue;
        const float x0 = std::max(gridStart + static_cast<float>(ranges[i].endBeat) * m_pixelsPerBeat - m_timelineScrollOffset, gridStart);
        const float x1 = std::min(gridStart + static_cast<float>(ranges[i + 1].startBeat) * m_pixelsPerBeat - m_timelineScrollOffset,
                                  bounds.right());
        if (x1 - x0 < 2.0f)
            continue;
        const float top = bounds.y + 3.0f, h = bounds.height - 6.0f;
        renderer.fillRect(AestraUI::NUIRect(x0, top, x1 - x0, h), AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.35f));
        for (float x = x0 - h; x < x1; x += 7.0f) { // diagonal hatch, clipped to the gap
            const float ax = std::max(x, x0), bx = std::min(x + h, x1);
            if (bx <= ax)
                continue;
            renderer.drawLine(AestraUI::NUIPoint(ax, top + h - (ax - x)), AestraUI::NUIPoint(bx, top + h - (bx - x)), 1.0f,
                              ink.withAlpha(0.28f));
        }
        if (x1 - x0 > 56.0f)
            renderer.drawTextCentered("Silent", AestraUI::NUIRect(x0, top, x1 - x0, h), 11.0f, ink);
    }
}

void TrackUIComponent::drawCompOverlay(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                                       float controlAreaWidth) {
    if (!m_trackManager)
        return;
    const float gridStart = timelineGridStartX(bounds.x, controlAreaWidth);
    auto xOf = [&](double beat) {
        return gridStart + static_cast<float>(beat) * m_pixelsPerBeat - m_timelineScrollOffset;
    };
    const PlaylistLane* lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
    const Track* track = lane ? m_trackManager->getTrack(lane->trackId) : nullptr;
    if (!track)
        return;
    if (m_laneId == track->compLaneId) { // the master: hatch the silent gaps where a deleted take used to play
        drawCompGaps(renderer, bounds, gridStart, *track);
        return;
    }
    if (!isCompTakeLane())
        return;

    // Once there is a comp, dim what of this take it does not use.
    if (track->compLaneId.isValid() && !track->comp.empty()) {
        const AestraUI::NUIColor dim(0.0f, 0.0f, 0.0f, 0.58f);
        for (const ClipInstance& clip : lane->clips) {
            double cursor = clip.startBeat;
            auto shade = [&](double a, double b) {
                const float x0 = std::max(xOf(a), gridStart), x1 = std::min(xOf(b), bounds.right());
                if (x1 > x0)
                    renderer.fillRect(AestraUI::NUIRect(x0, bounds.y, x1 - x0, bounds.height), dim);
            };
            for (const CompRange& r : track->comp.ranges()) {
                if (r.laneId != m_laneId || r.endBeat <= clip.startBeat || r.startBeat >= clip.endBeat())
                    continue;
                if (r.startBeat > cursor)
                    shade(cursor, std::min(r.startBeat, clip.endBeat()));
                cursor = std::max(cursor, r.endBeat);
            }
            if (cursor < clip.endBeat())
                shade(cursor, clip.endBeat());
        }
    }

    // After a single click: outline the stretch a double-click here would replace, then let it fade.
    if (m_compHintMs != 0 && !(m_compPressed && m_compDragging)) {
        const long long age = nowMs() - m_compHintMs;
        if (age >= kHintMs) {
            m_compHintMs = 0;
        } else {
            const float fade = 1.0f - static_cast<float>(age) / static_cast<float>(kHintMs);
            const AestraUI::NUIColor violet = AestraUI::NUIThemeManager::getInstance().getColor("primary");
            const float x0 = std::max(xOf(m_compHintStart), gridStart), x1 = std::min(xOf(m_compHintEnd), bounds.right());
            if (x1 > x0) {
                renderer.fillRect(AestraUI::NUIRect(x0, bounds.y, x1 - x0, bounds.height), violet.withAlpha(0.22f * fade));
                renderer.drawLine(AestraUI::NUIPoint(x0, bounds.y + 1), AestraUI::NUIPoint(x1, bounds.y + 1), 2.0f, violet.withAlpha(fade));
                renderer.drawLine(AestraUI::NUIPoint(x0, bounds.y + bounds.height - 1), AestraUI::NUIPoint(x1, bounds.y + bounds.height - 1), 2.0f, violet.withAlpha(fade));
            }
            repaint(); // keep fading
        }
    }

    // The span being dragged, in the live-gesture violet.
    if (m_compPressed && m_compDragging) {
        const double a = std::min(m_compAnchorBeat, m_compEndBeat), b = std::max(m_compAnchorBeat, m_compEndBeat);
        const AestraUI::NUIColor violet = AestraUI::NUIThemeManager::getInstance().getColor("primary");
        const float x0 = std::max(xOf(a), gridStart), x1 = std::min(xOf(b), bounds.right());
        if (x1 > x0) {
            renderer.fillRect(AestraUI::NUIRect(x0, bounds.y, x1 - x0, bounds.height), violet.withAlpha(0.32f));
            renderer.drawLine(AestraUI::NUIPoint(x0, bounds.y), AestraUI::NUIPoint(x0, bounds.y + bounds.height), 2.0f,
                              violet);
            renderer.drawLine(AestraUI::NUIPoint(x1, bounds.y), AestraUI::NUIPoint(x1, bounds.y + bounds.height), 2.0f,
                              violet);
        }
    }
}

} // namespace Audio
} // namespace Aestra
