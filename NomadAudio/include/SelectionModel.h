// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PlaylistClip.h"
#include "PlaylistModel.h"
#include "TimeTypes.h"
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Nomad {
namespace Audio {

// =============================================================================
// SelectionModel - Manages clip and lane selection state
// =============================================================================

/**
 * @brief Manages selection state for clips and lanes
 * 
 * This is separate from PlaylistModel to keep the data model clean.
 * Selection is a UI concern, not a playlist concern.
 * 
 * Features:
 * - Multi-selection of clips
 * - Lane selection
 * - Time range selection (for marquee/lasso)
 * - Selection change notifications
 * 
 * Thread safety:
 * - All public methods are thread-safe
 * - Typically called from UI thread
 */
class SelectionModel {
public:
    using SelectionChangeCallback = std::function<void()>;
    
    SelectionModel();
    ~SelectionModel();
    
    // === Clip Selection ===
    
    /// Select a single clip (clears other selections unless additive)
    void selectClip(PlaylistClipID clipId, bool additive = false);
    
    /// Deselect a clip
    void deselectClip(PlaylistClipID clipId);
    
    /// Toggle clip selection
    void toggleClipSelection(PlaylistClipID clipId);
    
    /// Select multiple clips
    void selectClips(const std::vector<PlaylistClipID>& clipIds, bool additive = false);
    
    /// Select all clips in a lane
    void selectAllClipsInLane(PlaylistLaneID laneId, const PlaylistModel& model, bool additive = false);
    
    /// Select all clips in a time range
    void selectClipsInRange(SampleIndex rangeStart, SampleIndex rangeEnd, 
                            const PlaylistModel& model, bool additive = false);
    
    /// Clear all clip selections
    void clearClipSelection();
    
    /// Check if a clip is selected
    bool isClipSelected(PlaylistClipID clipId) const;
    
    /// Get all selected clip IDs
    std::vector<PlaylistClipID> getSelectedClipIDs() const;
    
    /// Get number of selected clips
    size_t getSelectedClipCount() const;
    
    /// Check if any clips are selected
    bool hasClipSelection() const;
    
    // === Lane Selection ===
    
    /// Select a lane
    void selectLane(PlaylistLaneID laneId, bool additive = false);
    
    /// Deselect a lane
    void deselectLane(PlaylistLaneID laneId);
    
    /// Toggle lane selection
    void toggleLaneSelection(PlaylistLaneID laneId);
    
    /// Clear all lane selections
    void clearLaneSelection();
    
    /// Check if a lane is selected
    bool isLaneSelected(PlaylistLaneID laneId) const;
    
    /// Get all selected lane IDs
    std::vector<PlaylistLaneID> getSelectedLaneIDs() const;
    
    /// Get number of selected lanes
    size_t getSelectedLaneCount() const;
    
    /// Check if any lanes are selected
    bool hasLaneSelection() const;
    
    // === Time Range Selection ===
    
    /// Set the time range selection (for marquee select, loop region, etc.)
    void setTimeRangeSelection(SampleIndex start, SampleIndex end);
    
    /// Clear time range selection
    void clearTimeRangeSelection();
    
    /// Get the time range selection
    SampleRange getTimeRangeSelection() const;
    
    /// Check if there's an active time range selection
    bool hasTimeRangeSelection() const;
    
    // === Combined Operations ===
    
    /// Clear all selections (clips, lanes, time range)
    void clearAll();
    
    /// Check if anything is selected
    bool hasAnySelection() const;
    
    // === Focus ===
    
    /// Set the focused clip (for keyboard navigation)
    void setFocusedClip(PlaylistClipID clipId);
    
    /// Get the focused clip
    PlaylistClipID getFocusedClip() const;
    
    /// Clear focus
    void clearFocus();
    
    // === Observers ===
    
    /// Add callback for selection changes
    void addSelectionChangeObserver(SelectionChangeCallback callback);
    
    /// Clear all observers
    void clearObservers();
    
    // === Clipboard / Editing Helpers ===
    
    /// Get the bounding range of all selected clips
    SampleRange getSelectedClipsBounds(const PlaylistModel& model) const;
    
    /// Check if the selection can be moved (all clips exist and not locked)
    bool canMoveSelection(const PlaylistModel& model) const;
    
    /// Check if the selection can be deleted
    bool canDeleteSelection(const PlaylistModel& model) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    void notifySelectionChange();
};

} // namespace Audio
} // namespace Nomad
