// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-T1 (FD-50 board 8): automation lanes, one row under its own track. "Automate Volume" used to switch the whole
// timeline into an automation view that dimmed every track's clips and drew curves over all of them; now it opens
// one row under that track and the rest of the timeline is untouched. The row is built in refreshTracks() from the
// per-lane state kept here (shown or hidden, which curve); the row itself is TrackUIComponentAutomationRow.cpp.
#include "TrackManagerUI.h"

#include "TrackManager.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>

namespace Aestra {
namespace Audio {

TrackManagerUI::AutomationLaneView& TrackManagerUI::automationLaneView(PlaylistLaneID laneId) {
    for (auto& view : m_automationLanes) {
        if (view.laneId == laneId) return view;
    }
    AutomationLaneView view;
    view.laneId = laneId;
    m_automationLanes.push_back(view);
    return m_automationLanes.back();
}

const TrackManagerUI::AutomationLaneView* TrackManagerUI::findAutomationLaneView(PlaylistLaneID laneId) const {
    for (const auto& view : m_automationLanes) {
        if (view.laneId == laneId) return &view;
    }
    return nullptr;
}

bool TrackManagerUI::isAutomationLaneShown(PlaylistLaneID laneId) const {
    const auto* view = findAutomationLaneView(laneId);
    return view && view->shown;
}

AutomationTarget TrackManagerUI::automationLaneTarget(PlaylistLaneID laneId) const {
    const auto* view = findAutomationLaneView(laneId);
    if (view && view->chosen) return view->target;
    // Nothing picked yet: the curve the lane already has (a pan-only lane edits its pan), else Volume.
    const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(laneId) : nullptr;
    if (lane && !lane->automationCurves.empty()) return lane->automationCurves.front().getAutomationTarget();
    return AutomationTarget::Volume;
}

void TrackManagerUI::showAutomationLane(PlaylistLaneID laneId, AutomationTarget target, bool deferred) {
    auto& view = automationLaneView(laneId);
    const bool changed = !view.shown || view.target != target || !view.chosen;
    view.shown = true;
    view.target = target;
    view.chosen = true;
    if (!changed) return;
    if (deferred) {
        m_needsTrackRefresh = true; // rebuilt on the next paint (TrackManagerUIRender)
        invalidateCache();
        setDirty(true);
        return;
    }
    refreshTracks();
}

void TrackManagerUI::hideAutomationLane(PlaylistLaneID laneId, bool deferred) {
    auto& view = automationLaneView(laneId);
    if (!view.shown) return;
    view.shown = false; // the curve stays on the lane; the track row marks it (setHasHiddenAutomation)
    if (deferred) {
        m_needsTrackRefresh = true;
        invalidateCache();
        setDirty(true);
        return;
    }
    refreshTracks();
}

void TrackManagerUI::toggleAutomationForSelectedTracks() {
    std::vector<PlaylistLaneID> lanes;
    for (const auto& row : m_trackUIComponents) {
        if (row && !row->isAutomationRow() && m_trackSelection.contains(row->getLaneId())) {
            lanes.push_back(row->getLaneId());
        }
    }
    if (lanes.empty()) return;
    // One press opens them all, or closes them all when every selected track already shows its lane.
    const bool allShown = std::all_of(lanes.begin(), lanes.end(),
                                      [this](const PlaylistLaneID& id) { return isAutomationLaneShown(id); });
    for (const auto& id : lanes) automationLaneView(id).shown = !allShown;
    refreshTracks();
}

void TrackManagerUI::setPlaylistMode(PlaylistMode mode) {
    m_playlistMode = mode;
    if (!m_trackManager) return;
    for (const auto& id : m_trackManager->getPlaylistModel().getLaneIDs()) {
        automationLaneView(id).shown = mode == PlaylistMode::Automation;
    }
    refreshTracks();
    Log::info(std::string("[TrackManagerUI] Automation lanes ") +
              (mode == PlaylistMode::Automation ? "shown on every track" : "hidden"));
}

std::shared_ptr<TrackUIComponent> TrackManagerUI::makeAutomationRow(const std::shared_ptr<TrackUIComponent>& trackRow,
                                                                    AutomationTarget target) {
    const PlaylistLaneID laneId = trackRow->getLaneId();
    auto row = std::make_shared<TrackUIComponent>(laneId, nullptr, m_trackManager.get());
    row->setOnCacheInvalidationNeeded([this]() { this->invalidateCache(); });
    row->setOnTrackSelected([this](TrackUIComponent* comp, TrackSelectionIntent intent) { selectTrack(comp, intent); });
    row->setPixelsPerBeat(m_pixelsPerBeat);
    row->setBeatsPerBar(m_beatsPerBar);
    row->setTimelineScrollOffset(m_timelineScrollOffset);
    row->setSnapSetting(m_snapSetting);
    row->setSnapEnabled(m_snapEnabled);
    row->setPlatformBridge(m_window);
    row->setOnEditedAutomationTargetChanged([this, laneId](AutomationTarget changed) {
        auto& view = automationLaneView(laneId);
        view.target = changed;
        view.chosen = true;
    });
    // Both arrive from inside the row's own handlers, so the rebuild waits for the next paint.
    row->setAutomationRow(
        target, [this, laneId](AutomationTarget picked) { showAutomationLane(laneId, picked, true); },
        [this, laneId]() { hideAutomationLane(laneId, true); });
    row->updateUI();
    return row;
}

} // namespace Audio
} // namespace Aestra
