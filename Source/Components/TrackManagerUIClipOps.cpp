// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerUI — clip operations: instant drag, placement, clipboard, split/delete.
// Split out of the former monolithic TrackManagerUI.cpp — bodies moved verbatim.
#include "TrackManagerUI.h"

#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "AudioFileValidator.h"
#include "ClipSource.h"
#include "Commands/AddChannelCommand.h"
#include "Commands/AddClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "Commands/CreateLaneCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/MacroCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/SplitClipCommand.h"
#include "MiniAudioDecoder.h"
#include "MixerChannel.h"
#include "PluginManager.h"
#include "TrackManager.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "TrackManagerUIInternal.h"

namespace Aestra {
namespace Audio {

// =============================================================================
// SECTION: Clip Dragging & Operations
// =============================================================================

void TrackManagerUI::startInstantClipDrag(TrackUIComponent* trackComp, ClipInstanceID clipId,
                                          const AestraUI::NUIPoint& clickPos) {
    if (!trackComp || !clipId.isValid() || !m_trackManager)
        return;

    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(clipId);
    if (!clip) {
        return;
    }

    m_isDraggingClipInstant = true;
    m_draggedClipTrack = trackComp;
    m_draggedClipId = clipId;
    m_suppressPlaylistRefresh = true; // Suppress full rebuilds for smoothness
    m_clipOriginalStartTime = clip->startBeat;
    m_clipOriginalLaneId = playlist.findClipLane(clipId);

    // Calculate offset (Cursor Beat - Clip Start Beat)
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    const float gridStartX = timelineGridStartX(getBounds().x, controlAreaWidth);

    double cursorBeat = gridOffsetToBeat(clickPos.x - gridStartX);
    m_clipDragOffsetBeats = cursorBeat - clip->startBeat;

    if (m_window) {
        m_window->setMouseCapture(true);
    }

    Log::info("Started instant clip drag");
}

void TrackManagerUI::updateInstantClipDrag(const AestraUI::NUIPoint& currentPos) {
    if (!m_isDraggingClipInstant || !m_trackManager)
        return;

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    const AestraUI::NUIRect bounds = getBounds();
    const float gridStartX = timelineGridStartX(bounds.x, controlAreaWidth);
    AestraUI::NUIPoint latchedPos = currentPos;
    clampInstantClipDragPosition(latchedPos);

    // Track mouse position for edge-scrolling in onUpdate
    m_lastMousePos = latchedPos;

    double cursorBeat = gridOffsetToBeat(latchedPos.x - gridStartX);

    // Apply relative offset
    double newStartBeat = cursorBeat - m_clipDragOffsetBeats;

    // Snap (optional, or always on)
    newStartBeat = snapBeatToGrid(newStartBeat);
    newStartBeat = std::max(0.0, newStartBeat);

    // Live update model
    auto& playlist = m_trackManager->getPlaylistModel();

    // Determine target track from Y position
    int targetTrackIndex = getTrackAtPosition(latchedPos.y);
    int trackCount = static_cast<int>(m_trackUIComponents.size());

    // Clamp to valid tracks
    if (trackCount > 0) {
        targetTrackIndex = std::clamp(targetTrackIndex, 0, trackCount - 1);

        auto targetLaneId = m_trackUIComponents[targetTrackIndex]->getLaneId();
        if (targetLaneId.isValid()) {
            playlist.moveClip(m_draggedClipId, newStartBeat, targetLaneId);
        }
    } else {
        // Fallback if no tracks? (Unlikely)
        auto laneId = playlist.findClipLane(m_draggedClipId);
        if (laneId.isValid()) {
            playlist.moveClip(m_draggedClipId, newStartBeat, laneId);
        }
    }

    // Redraw immediately (GPU cache handles the rest)
    invalidateCache();
}

bool TrackManagerUI::clampInstantClipDragPosition(AestraUI::NUIPoint& position) const {
    if (!m_isDraggingClipInstant) {
        return false;
    }

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    const AestraUI::NUIRect bounds = getBounds();
    const float gridStartX = timelineGridStartX(bounds.x, controlAreaWidth);
    const float gridEndX = timelineGridEndX(bounds.x, bounds.width);
    const float trackAreaTop = timelineTrackAreaTopY(bounds.y);
    const float trackAreaBottom = bounds.y + bounds.height;

    const float clampedX = safeClampFloat(position.x, gridStartX, gridEndX);
    const float clampedY = safeClampFloat(position.y, trackAreaTop, trackAreaBottom);
    const bool changed = std::abs(position.x - clampedX) > 0.5f || std::abs(position.y - clampedY) > 0.5f;
    position.x = clampedX;
    position.y = clampedY;
    return changed;
}

bool TrackManagerUI::placeFileOnTimeline(const std::string& filePath, const std::string& displayName) {
    if (!m_trackManager || filePath.empty()) {
        return false;
    }

    int targetLane = 0;
    if (auto* selectedTrack = getSelectedTrackUI()) {
        for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
            if (m_trackUIComponents[i].get() == selectedTrack) {
                targetLane = static_cast<int>(i);
                break;
            }
        }
    }

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto bounds = getBounds();
    const float trackAreaStartY = timelineTrackAreaTopY(bounds.y);
    const float y = trackAreaStartY + (targetLane * (m_trackHeight + m_trackSpacing)) - m_scrollOffset + 2.0f;
    const float gridStartX = timelineGridStartX(0.0f, theme.getLayoutDimensions().trackControlsWidth);
    const double playheadBeats =
        snapBeatToGrid(m_trackManager->getPlaylistModel().secondsToBeats(std::max(0.0, m_trackManager->getPosition())));
    const float x =
        bounds.x + gridStartX + static_cast<float>(playheadBeats * m_pixelsPerBeat) - m_timelineScrollOffset + 2.0f;

    AestraUI::DragData dragData;
    dragData.type = AestraUI::DragDataType::File;
    dragData.filePath = filePath;
    dragData.displayName = displayName;
    const auto dropResult = onDrop(dragData, AestraUI::NUIPoint(x, y));
    return dropResult.accepted;
}

bool TrackManagerUI::placePatternOnTimeline(PatternID patternId) {
    if (!m_trackManager || !patternId.isValid()) {
        return false;
    }

    int targetLane = 0;
    if (auto* selectedTrack = getSelectedTrackUI()) {
        for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
            if (m_trackUIComponents[i].get() == selectedTrack) {
                targetLane = static_cast<int>(i);
                break;
            }
        }
    }

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto bounds = getBounds();
    const float trackAreaStartY = timelineTrackAreaTopY(bounds.y);
    const float y = trackAreaStartY + (targetLane * (m_trackHeight + m_trackSpacing)) - m_scrollOffset + 2.0f;
    const float gridStartX = timelineGridStartX(0.0f, theme.getLayoutDimensions().trackControlsWidth);
    const double playheadBeats =
        snapBeatToGrid(m_trackManager->getPlaylistModel().secondsToBeats(std::max(0.0, m_trackManager->getPosition())));
    const float x =
        bounds.x + gridStartX + static_cast<float>(playheadBeats * m_pixelsPerBeat) - m_timelineScrollOffset + 2.0f;

    AestraUI::DragData dragData;
    dragData.type = AestraUI::DragDataType::Pattern;
    dragData.customData = patternId;
    if (const auto* pattern = m_trackManager->getPatternManager().getPattern(patternId)) {
        dragData.displayName = pattern->name;
    }
    const auto dropResult = onDrop(dragData, AestraUI::NUIPoint(x, y));
    return dropResult.accepted;
}

void TrackManagerUI::finishInstantClipDrag() {
    if (!m_isDraggingClipInstant)
        return;

    Log::info("Finished instant clip drag");

    // Capture final position before clearing drag state
    double finalStartBeat = 0.0;
    PlaylistLaneID finalLaneId;
    if (m_trackManager && m_draggedClipId.isValid()) {
        auto& playlist = m_trackManager->getPlaylistModel();
        if (const auto* clip = playlist.getClip(m_draggedClipId)) {
            finalStartBeat = clip->startBeat;
            finalLaneId = playlist.findClipLane(m_draggedClipId);
        }

        // Create undoable command for the move if position actually changed
        if (finalStartBeat != m_clipOriginalStartTime || finalLaneId != m_clipOriginalLaneId) {
            auto cmd = std::make_shared<MoveClipCommand>(playlist, m_draggedClipId, m_clipOriginalStartTime,
                                                         m_clipOriginalLaneId, finalStartBeat, finalLaneId);
            m_trackManager->getCommandHistory().pushAndExecute(cmd);
        }
    }

    m_isDraggingClipInstant = false;
    m_draggedClipTrack = nullptr;
    m_draggedClipId = ClipInstanceID{};
    m_suppressPlaylistRefresh = false; // Restore normal behavior
    if (m_dragPatternPreviewActive && m_onStopPatternClipPreview) {
        m_onStopPatternClipPreview();
    }
    m_dragPatternPreviewActive = false;

    if (m_window) {
        m_window->setMouseCapture(false);
    }

    // Final refresh to ensure everything is consistent
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    // A move that lands while playing must reschedule too: the instance set
    // still points at the drag origin (per-motion model updates deliberately
    // do not reschedule — gesture endpoint only).
    if (m_trackManager) {
        m_trackManager->refreshTimelinePatternInstances();
    }
}

void TrackManagerUI::cancelInstantClipDrag() {
    if (!m_isDraggingClipInstant)
        return;

    Log::info("Cancelled instant clip drag");

    // Put the clip back where the drag started, WITHOUT recording history.
    //
    // User cancellation is not project history. This used to revert by pushing a
    // MoveClipCommand, which was undoable in exactly the wrong direction: the
    // command captures the clip's current — i.e. dragged — position as its
    // "original", so the next Ctrl+Z re-applied the movement the user had just
    // cancelled. It also cleared the redo stack, so cancelling a drag destroyed
    // whatever the user still had to redo.
    //
    // The live drag itself never went through the history either
    // (updateInstantClipDrag moves the model directly for smoothness), so there is
    // nothing on the stack to undo here — only a position to restore.
    if (m_trackManager && m_draggedClipId.isValid()) {
        auto& playlist = m_trackManager->getPlaylistModel();
        auto laneId = playlist.findClipLane(m_draggedClipId);
        if (laneId.isValid() && m_clipOriginalLaneId.isValid()) {
            playlist.moveClip(m_draggedClipId, m_clipOriginalStartTime, m_clipOriginalLaneId);
        }
    }

    m_isDraggingClipInstant = false;
    m_draggedClipTrack = nullptr;
    m_draggedClipId = ClipInstanceID{};
    m_suppressPlaylistRefresh = false;
    if (m_dragPatternPreviewActive && m_onStopPatternClipPreview) {
        m_onStopPatternClipPreview();
    }
    m_dragPatternPreviewActive = false;

    if (m_window) {
        m_window->setMouseCapture(false);
    }

    refreshTracks();
    invalidateCache();
}
void TrackManagerUI::copySelectedClip() {
    if (!m_selectedClipId.isValid())
        return;

    auto& playlist = m_trackManager->getPlaylistModel();
    if (const auto* clip = playlist.getClip(m_selectedClipId)) {
        m_clipboardClip = *clip;
        Log::info("Copied clip: " + m_clipboardClip.name);
    }
}

void TrackManagerUI::cutSelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid())
        return;

    auto& playlist = m_trackManager->getPlaylistModel();
    if (const auto* clip = playlist.getClip(m_selectedClipId)) {
        // Clipboard stays anchor-based: paste paints one stamp source.
        m_clipboardClip = *clip;
        Log::info("Cut clip: " + m_clipboardClip.name);
    } else {
        return;
    }

    std::vector<ClipInstanceID> targets;
    m_clipSelection.forEachClip([&](const ClipInstanceID& id) { targets.push_back(id); });
    if (targets.empty()) {
        targets.push_back(m_selectedClipId);
    }

    auto macro = std::make_shared<Aestra::Audio::MacroCommand>("Cut Clips");
    for (const auto& id : targets) {
        macro->addCommand(std::make_shared<RemoveClipCommand>(playlist, id));
    }
    m_trackManager->getCommandHistory().pushAndExecute(macro);
    clearClipSelection();
    m_selectedClipId = ClipInstanceID{};

    // Cut removes clips: reschedule, or the removed clips keep sounding
    // while playing (same staleness delete refreshes for).
    m_trackManager->refreshTimelinePatternInstances();

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();

    Log::info("Cut and removed " + std::to_string(targets.size()) + " selected clip(s) via PlaylistModel");
}

void TrackManagerUI::pasteClipboardAtCursor() {
    if (!m_clipboardClip.id.isValid())
        return;

    // Find target lane (currently selected track)
    TrackUIComponent* targetTrack = nullptr;
    for (auto& track : m_trackUIComponents) {
        if (track && track->isSelected()) {
            targetTrack = track.get();
            break;
        }
    }

    // Fallback: Use first track if none selected
    if (!targetTrack && !m_trackUIComponents.empty()) {
        targetTrack = m_trackUIComponents[0].get();
    }

    if (!targetTrack)
        return;

    // Paste at playhead position (normal snap to nearest)
    double playheadSeconds = m_trackManager->getPosition();
    double playhead = secondsToBeats(playheadSeconds);
    onPaintClip(targetTrack, playhead);
}

void TrackManagerUI::pasteClipToRight() {
    if (!m_clipboardClip.id.isValid())
        return;

    // Find the currently selected clip
    auto& playlist = m_trackManager->getPlaylistModel();
    const ClipInstance* selectedClip = (m_selectedClipId.isValid()) ? playlist.getClip(m_selectedClipId) : nullptr;

    // Find target lane
    PlaylistLaneID targetLaneId;
    double pastePosition = 0.0;

    if (selectedClip) {
        // Paste at end of selected clip
        pastePosition = selectedClip->startBeat + selectedClip->durationBeats;

        // Find the lane containing the selected clip using API
        targetLaneId = playlist.findClipLane(m_selectedClipId);
    } else {
        // Fallback: Use playhead and first selected/available track
        double playheadSeconds = m_trackManager->getPosition();
        pastePosition = snapBeatToGridForward(secondsToBeats(playheadSeconds));

        for (auto& track : m_trackUIComponents) {
            if (track && track->isSelected()) {
                targetLaneId = track->getLaneId();
                break;
            }
        }
        if (!targetLaneId.isValid() && !m_trackUIComponents.empty()) {
            targetLaneId = m_trackUIComponents[0]->getLaneId();
        }
    }

    if (!targetLaneId.isValid())
        return;

    // Create new clip
    ClipInstance newClip = m_clipboardClip;
    newClip.id = ClipInstanceID::generate();
    newClip.startBeat = pastePosition;

    // Create Command
    auto cmd =
        std::make_shared<Aestra::Audio::AddClipCommand>(m_trackManager->getPlaylistModel(), targetLaneId, newClip);
    m_trackManager->getCommandHistory().pushAndExecute(cmd);

    // Select the new clip so repeated Ctrl+B continues to the right
    m_selectedClipId = newClip.id;

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    Log::info("Paste-to-right at beat " + std::to_string(newClip.startBeat));
}

void TrackManagerUI::duplicateSelectedClip() {
    if (!m_trackManager || !m_selectedClipId.isValid())
        return;

    auto& playlist = m_trackManager->getPlaylistModel();

    // Duplicate the WHOLE selection as one block (#848): every clip shifts by
    // the same offset so internal layout survives, and the block starts where
    // the selection ends. Per-clip right-after placement overlapped whenever
    // the boxed clips were consecutive.
    std::vector<ClipInstanceID> targets;
    m_clipSelection.forEachClip([&](const ClipInstanceID& id) { targets.push_back(id); });
    if (targets.empty() && m_selectedClipId.isValid()) {
        targets.push_back(m_selectedClipId);
    }

    double blockMinStart = 0.0;
    double blockMaxEnd = 0.0;
    bool anyValid = false;
    for (const auto& id : targets) {
        const ClipInstance* clip = playlist.getClip(id);
        if (!clip)
            continue;
        const double end = clip->startBeat + clip->durationBeats;
        blockMinStart = anyValid ? std::min(blockMinStart, clip->startBeat) : clip->startBeat;
        blockMaxEnd = anyValid ? std::max(blockMaxEnd, end) : end;
        anyValid = true;
    }
    if (!anyValid) {
        Log::warning("[TrackManager] Nothing duplicated — no valid selected clips");
        return;
    }
    const double blockOffset = blockMaxEnd - blockMinStart;

    auto macro = std::make_shared<Aestra::Audio::MacroCommand>("Duplicate Clips");
    ClipInstanceID lastDuplicateId{};
    std::vector<ClipInstanceID> newIds;
    for (const auto& id : targets) {
        const ClipInstance* clip = playlist.getClip(id);
        if (!clip)
            continue;
        PlaylistLaneID targetLaneId = playlist.findClipLane(id);
        if (!targetLaneId.isValid())
            continue;
        const double targetStartBeat = clip->startBeat + blockOffset;
        auto cmd = std::make_shared<Aestra::Audio::DuplicateClipCommand>(playlist, id, targetStartBeat,
                                                                         targetLaneId);
        macro->addCommand(cmd);
        lastDuplicateId = cmd->getDuplicateId();
        newIds.push_back(lastDuplicateId);
    }
    if (macro->empty()) {
        Log::warning("[TrackManager] Nothing duplicated — no valid selected clips");
        return;
    }
    m_trackManager->getCommandHistory().pushAndExecute(macro);

    if (!newIds.empty()) {
        // The clones become the selection, so repeated Ctrl+B keeps appending
        // the block (and Delete acts on the copies, not the originals).
        selectClips(newIds, TrackSelectionIntent::Replace);
        if (lastDuplicateId.isValid()) {
            m_selectedClipId = lastDuplicateId;
            if (const auto* duplicateClip = playlist.getClip(lastDuplicateId)) {
                m_clipboardClip = *duplicateClip;
            }
        }
    }

    // While playing, the scheduler's instance set was built at play() time —
    // without a reschedule the duplicated pattern stays silent until loop wrap.
    m_trackManager->refreshTimelinePatternInstances();

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    Log::info("Duplicated selected clip");
}

void TrackManagerUI::onPaintClip(TrackUIComponent* trackComp, double beat) {
    if (!trackComp || !m_clipboardClip.id.isValid())
        return;

    ClipInstance newClip = m_clipboardClip;
    newClip.id = ClipInstanceID::generate();
    newClip.startBeat = snapBeatToGrid(beat);

    // Create Command
    auto cmd = std::make_shared<Aestra::Audio::AddClipCommand>(m_trackManager->getPlaylistModel(),
                                                               trackComp->getLaneId(), newClip);
    m_trackManager->getCommandHistory().pushAndExecute(cmd);

    // Painted clips must be audible immediately while playing (same scheduler
    // refresh as duplication).
    m_trackManager->refreshTimelinePatternInstances();

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    Log::info("Pasted clip via Paint/Paste");
}
// =============================================================================
// Clip Manipulation Methods
// =============================================================================

TrackUIComponent* TrackManagerUI::getSelectedTrackUI() const {
    for (const auto& trackUI : m_trackUIComponents) {
        if (trackUI && trackUI->isSelected()) {
            return trackUI.get();
        }
    }
    return nullptr;
}

void TrackManagerUI::splitSelectedClipAtPlayhead() {
    if (!m_trackManager || !m_selectedClipId.isValid()) {
        Log::warning("No clip selected for split");
        return;
    }

    // Get current playhead position from transport
    double currentPosSeconds = m_trackManager->getPosition();
    double bpm = std::max(m_trackManager->getPlaylistModel().getBPM(), 1.0);
    double secondsPerBeat = 60.0 / bpm;
    double splitBeat = currentPosSeconds / secondsPerBeat;

    auto& playlist = m_trackManager->getPlaylistModel();
    const auto* clip = playlist.getClip(m_selectedClipId);

    if (!clip || splitBeat <= clip->startBeat || splitBeat >= clip->startBeat + clip->durationBeats) {
        Log::warning("Playhead not within selected clip bounds for split");
        return;
    }

    auto cmd = std::make_shared<SplitClipCommand>(playlist, m_selectedClipId, splitBeat);
    m_trackManager->getCommandHistory().pushAndExecute(cmd);

    // While playing, the scheduler's instance set was built at play() time —
    // without a reschedule the old bounds keep sounding until loop wrap
    // (same staleness duplicate + paint already refresh for).
    m_trackManager->refreshTimelinePatternInstances();

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();

    Log::info("[TrackManagerUI] Clip split at playhead (beat " + std::to_string(splitBeat) + ")");
}

void TrackManagerUI::deleteSelectedClip() {
    if (!m_trackManager)
        return;

    // Whole selection set is the target (#848): marquee-selected clips all go
    // in one undoable step, not just the anchor.
    std::vector<ClipInstanceID> targets;
    m_clipSelection.forEachClip([&](const ClipInstanceID& id) { targets.push_back(id); });
    if (targets.empty() && m_selectedClipId.isValid()) {
        targets.push_back(m_selectedClipId);
    }
    if (targets.empty()) {
        Log::warning("No clip selected for delete");
        return;
    }

    auto& playlist = m_trackManager->getPlaylistModel();
    auto macro = std::make_shared<Aestra::Audio::MacroCommand>("Delete Clips");
    for (const auto& id : targets) {
        macro->addCommand(std::make_shared<RemoveClipCommand>(playlist, id));
    }
    m_trackManager->getCommandHistory().pushAndExecute(macro);
    clearClipSelection();
    m_selectedClipId = ClipInstanceID{};

    // While playing, a deleted clip keeps sounding until loop wrap unless the
    // scheduler set is rebuilt (same staleness split refreshes for).
    m_trackManager->refreshTimelinePatternInstances();

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();

    Log::info("Deleted " + std::to_string(targets.size()) + " selected clip(s) via PlaylistModel");
}

} // namespace Audio
} // namespace Aestra
