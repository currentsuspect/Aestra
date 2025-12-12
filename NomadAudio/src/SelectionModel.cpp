// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "SelectionModel.h"
#include "NomadLog.h"
#include <mutex>
#include <unordered_set>

namespace Nomad {
namespace Audio {

// =============================================================================
// SelectionModel Implementation
// =============================================================================

struct SelectionModel::Impl {
    std::unordered_set<PlaylistClipID, PlaylistClipID::Hash> selectedClips;
    std::unordered_set<PlaylistLaneID, PlaylistLaneID::Hash> selectedLanes;
    SampleRange timeRangeSelection;
    bool hasTimeRange = false;
    PlaylistClipID focusedClip;
    
    std::vector<SelectionChangeCallback> observers;
    mutable std::mutex mutex;
};

SelectionModel::SelectionModel()
    : m_impl(std::make_unique<Impl>())
{
}

SelectionModel::~SelectionModel() = default;

// === Clip Selection ===

void SelectionModel::selectClip(PlaylistClipID clipId, bool additive) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (!additive) {
        m_impl->selectedClips.clear();
    }
    
    m_impl->selectedClips.insert(clipId);
    m_impl->focusedClip = clipId;
    
    notifySelectionChange();
}

void SelectionModel::deselectClip(PlaylistClipID clipId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    m_impl->selectedClips.erase(clipId);
    
    if (m_impl->focusedClip == clipId) {
        m_impl->focusedClip = PlaylistClipID();
    }
    
    notifySelectionChange();
}

void SelectionModel::toggleClipSelection(PlaylistClipID clipId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->selectedClips.find(clipId);
    if (it != m_impl->selectedClips.end()) {
        m_impl->selectedClips.erase(it);
        if (m_impl->focusedClip == clipId) {
            m_impl->focusedClip = PlaylistClipID();
        }
    } else {
        m_impl->selectedClips.insert(clipId);
        m_impl->focusedClip = clipId;
    }
    
    notifySelectionChange();
}

void SelectionModel::selectClips(const std::vector<PlaylistClipID>& clipIds, bool additive) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (!additive) {
        m_impl->selectedClips.clear();
    }
    
    for (const auto& id : clipIds) {
        m_impl->selectedClips.insert(id);
    }
    
    if (!clipIds.empty()) {
        m_impl->focusedClip = clipIds.back();
    }
    
    notifySelectionChange();
}

void SelectionModel::selectAllClipsInLane(PlaylistLaneID laneId, const PlaylistModel& model, bool additive) {
    const PlaylistLane* lane = model.getLane(laneId);
    if (!lane) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (!additive) {
        m_impl->selectedClips.clear();
    }
    
    for (const auto& clip : lane->clips) {
        m_impl->selectedClips.insert(clip.id);
    }
    
    notifySelectionChange();
}

void SelectionModel::selectClipsInRange(SampleIndex rangeStart, SampleIndex rangeEnd,
                                         const PlaylistModel& model, bool additive) {
    auto clips = model.getAllClipsInRange(rangeStart, rangeEnd);
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (!additive) {
        m_impl->selectedClips.clear();
    }
    
    for (const auto& [laneId, clip] : clips) {
        m_impl->selectedClips.insert(clip->id);
    }
    
    notifySelectionChange();
}

void SelectionModel::clearClipSelection() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->selectedClips.clear();
    m_impl->focusedClip = PlaylistClipID();
    notifySelectionChange();
}

bool SelectionModel::isClipSelected(PlaylistClipID clipId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->selectedClips.count(clipId) > 0;
}

std::vector<PlaylistClipID> SelectionModel::getSelectedClipIDs() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return std::vector<PlaylistClipID>(m_impl->selectedClips.begin(), m_impl->selectedClips.end());
}

size_t SelectionModel::getSelectedClipCount() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->selectedClips.size();
}

bool SelectionModel::hasClipSelection() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return !m_impl->selectedClips.empty();
}

// === Lane Selection ===

void SelectionModel::selectLane(PlaylistLaneID laneId, bool additive) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (!additive) {
        m_impl->selectedLanes.clear();
    }
    
    m_impl->selectedLanes.insert(laneId);
    notifySelectionChange();
}

void SelectionModel::deselectLane(PlaylistLaneID laneId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->selectedLanes.erase(laneId);
    notifySelectionChange();
}

void SelectionModel::toggleLaneSelection(PlaylistLaneID laneId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->selectedLanes.find(laneId);
    if (it != m_impl->selectedLanes.end()) {
        m_impl->selectedLanes.erase(it);
    } else {
        m_impl->selectedLanes.insert(laneId);
    }
    
    notifySelectionChange();
}

void SelectionModel::clearLaneSelection() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->selectedLanes.clear();
    notifySelectionChange();
}

bool SelectionModel::isLaneSelected(PlaylistLaneID laneId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->selectedLanes.count(laneId) > 0;
}

std::vector<PlaylistLaneID> SelectionModel::getSelectedLaneIDs() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return std::vector<PlaylistLaneID>(m_impl->selectedLanes.begin(), m_impl->selectedLanes.end());
}

size_t SelectionModel::getSelectedLaneCount() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->selectedLanes.size();
}

bool SelectionModel::hasLaneSelection() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return !m_impl->selectedLanes.empty();
}

// === Time Range Selection ===

void SelectionModel::setTimeRangeSelection(SampleIndex start, SampleIndex end) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->timeRangeSelection = SampleRange(start, end);
    m_impl->hasTimeRange = true;
    notifySelectionChange();
}

void SelectionModel::clearTimeRangeSelection() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->timeRangeSelection = SampleRange();
    m_impl->hasTimeRange = false;
    notifySelectionChange();
}

SampleRange SelectionModel::getTimeRangeSelection() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->timeRangeSelection;
}

bool SelectionModel::hasTimeRangeSelection() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->hasTimeRange && m_impl->timeRangeSelection.isValid();
}

// === Combined Operations ===

void SelectionModel::clearAll() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->selectedClips.clear();
    m_impl->selectedLanes.clear();
    m_impl->timeRangeSelection = SampleRange();
    m_impl->hasTimeRange = false;
    m_impl->focusedClip = PlaylistClipID();
    notifySelectionChange();
}

bool SelectionModel::hasAnySelection() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return !m_impl->selectedClips.empty() || 
           !m_impl->selectedLanes.empty() || 
           m_impl->hasTimeRange;
}

// === Focus ===

void SelectionModel::setFocusedClip(PlaylistClipID clipId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->focusedClip = clipId;
}

PlaylistClipID SelectionModel::getFocusedClip() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->focusedClip;
}

void SelectionModel::clearFocus() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->focusedClip = PlaylistClipID();
}

// === Observers ===

void SelectionModel::addSelectionChangeObserver(SelectionChangeCallback callback) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->observers.push_back(std::move(callback));
}

void SelectionModel::clearObservers() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->observers.clear();
}

// === Clipboard / Editing Helpers ===

SampleRange SelectionModel::getSelectedClipsBounds(const PlaylistModel& model) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (m_impl->selectedClips.empty()) {
        return SampleRange();
    }
    
    SampleIndex minStart = INT64_MAX;
    SampleIndex maxEnd = 0;
    
    for (const auto& clipId : m_impl->selectedClips) {
        const PlaylistClip* clip = model.getClip(clipId);
        if (clip) {
            minStart = std::min(minStart, clip->startTime);
            maxEnd = std::max(maxEnd, clip->getEndTime());
        }
    }
    
    if (minStart == INT64_MAX) {
        return SampleRange();
    }
    
    return SampleRange(minStart, maxEnd);
}

bool SelectionModel::canMoveSelection(const PlaylistModel& model) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    for (const auto& clipId : m_impl->selectedClips) {
        const PlaylistClip* clip = model.getClip(clipId);
        if (!clip || clip->isLocked()) {
            return false;
        }
    }
    
    return !m_impl->selectedClips.empty();
}

bool SelectionModel::canDeleteSelection(const PlaylistModel& model) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    for (const auto& clipId : m_impl->selectedClips) {
        const PlaylistClip* clip = model.getClip(clipId);
        if (!clip || clip->isLocked()) {
            return false;
        }
    }
    
    return !m_impl->selectedClips.empty();
}

// === Internal ===

void SelectionModel::notifySelectionChange() {
    for (const auto& callback : m_impl->observers) {
        if (callback) callback();
    }
}

} // namespace Audio
} // namespace Nomad
