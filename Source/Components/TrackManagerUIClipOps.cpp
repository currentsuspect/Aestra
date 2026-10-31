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
#include "Commands/SetClipEditsCommand.h"
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

    // Dragging one clip of a multi-selection drags the whole selection: remember where each one started.
    m_groupDrag.clear();
    m_pendingGroupCollapse = ClipInstanceID{}; // a drag began, so the press is not a plain click any more
    if (m_clipSelection.size() > 1 && m_clipSelection.contains(clipId)) {
        m_clipSelection.forEachClip([&](const ClipInstanceID& id) {
            if (const auto* member = playlist.getClip(id)) {
                GroupDragEntry entry;
                entry.id = id;
                entry.startBeat = member->startBeat;
                entry.lane = playlist.findClipLane(id);
                entry.laneIndex = laneIndexOf(entry.lane);
                m_groupDrag.push_back(entry);
            }
        });
        if (m_groupDrag.size() < 2) {
            m_groupDrag.clear();
        }
        m_groupDragAnchorStart = clip->startBeat;
        m_groupDragAnchorLaneIndex = laneIndexOf(m_clipOriginalLaneId);
    }

    if (m_window) {
        m_window->setMouseCapture(true);
    }

    Log::info("Started instant clip drag");
}

int TrackManagerUI::laneIndexOf(const PlaylistLaneID& laneId) const {
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        if (m_trackUIComponents[i] && m_trackUIComponents[i]->getLaneId() == laneId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Moves the snapshot @p origin by (beatDelta, laneDelta), clamped as ONE block so the clips keep their layout:
// no clip crosses beat 0 or leaves the first/last lane. The model is moved live (no history); the caller pushes
// the undo step. Returns whether anything was moved.
bool TrackManagerUI::applyGroupMove(const std::vector<GroupDragEntry>& origin, double beatDelta, int laneDelta) {
    if (origin.empty() || !m_trackManager) {
        return false;
    }
    const int laneCount = static_cast<int>(m_trackUIComponents.size());
    double minStart = origin.front().startBeat;
    int minLane = origin.front().laneIndex;
    int maxLane = origin.front().laneIndex;
    for (const auto& e : origin) {
        minStart = std::min(minStart, e.startBeat);
        if (e.laneIndex >= 0) {
            minLane = (maxLane < 0 || minLane < 0) ? e.laneIndex : std::min(minLane, e.laneIndex);
            maxLane = std::max(maxLane, e.laneIndex);
        }
    }
    beatDelta = std::max(beatDelta, -minStart);
    laneDelta = (laneCount > 0 && minLane >= 0) ? std::clamp(laneDelta, -minLane, (laneCount - 1) - maxLane) : 0;

    auto& playlist = m_trackManager->getPlaylistModel();
    for (const auto& e : origin) {
        const int li = e.laneIndex + laneDelta;
        PlaylistLaneID target = e.lane;
        if (e.laneIndex >= 0 && li >= 0 && li < laneCount && m_trackUIComponents[static_cast<size_t>(li)]) {
            target = m_trackUIComponents[static_cast<size_t>(li)]->getLaneId();
        }
        playlist.moveClip(e.id, e.startBeat + beatDelta, target);
    }
    return true;
}

void TrackManagerUI::resolvePendingGroupClick(ClipInstanceID clickedClip) {
    if (!m_pendingGroupCollapse.isValid() || m_pendingGroupCollapse != clickedClip) {
        return;
    }
    m_pendingGroupCollapse = ClipInstanceID{};
    clearClipSelection();
    selectClip(clickedClip); // a click that never dragged selects just that clip
}

bool TrackManagerUI::nudgeSelectedClips(double beats, int laneDelta) {
    if (!m_trackManager) {
        return false;
    }
    auto& playlist = m_trackManager->getPlaylistModel();
    std::vector<GroupDragEntry> origin;
    std::vector<ClipInstanceID> targets;
    m_clipSelection.forEachClip([&](const ClipInstanceID& id) { targets.push_back(id); });
    if (targets.empty() && m_selectedClipId.isValid()) {
        targets.push_back(m_selectedClipId);
    }
    for (const auto& id : targets) {
        if (const auto* clip = playlist.getClip(id)) {
            GroupDragEntry entry;
            entry.id = id;
            entry.startBeat = clip->startBeat;
            entry.lane = playlist.findClipLane(id);
            entry.laneIndex = laneIndexOf(entry.lane);
            origin.push_back(entry);
        }
    }
    if (origin.empty() || (beats == 0.0 && laneDelta == 0)) {
        return false;
    }

    applyGroupMove(origin, beats, laneDelta);

    // One undo step for the whole nudge; the model is already there, so only clips that actually moved get a command.
    auto macro = std::make_shared<Aestra::Audio::MacroCommand>("Nudge Clips");
    for (const auto& e : origin) {
        const auto* now = playlist.getClip(e.id);
        if (!now) {
            continue;
        }
        const PlaylistLaneID nowLane = playlist.findClipLane(e.id);
        if (now->startBeat != e.startBeat || nowLane != e.lane) {
            macro->addCommand(std::make_shared<MoveClipCommand>(playlist, e.id, e.startBeat, e.lane, now->startBeat,
                                                                nowLane));
        }
    }
    if (macro->empty()) {
        return true; // clamped at an edge: the key is consumed, nothing moved
    }
    m_trackManager->getCommandHistory().pushAndExecute(macro);
    m_trackManager->refreshTimelinePatternInstances();
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    return true;
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

    // A group drag moves every selected clip by the anchor's delta (clamped as a block).
    if (!m_groupDrag.empty()) {
        const int anchorLane = m_groupDragAnchorLaneIndex;
        const int clampedTarget = trackCount > 0 ? std::clamp(targetTrackIndex, 0, trackCount - 1) : anchorLane;
        applyGroupMove(m_groupDrag, newStartBeat - m_groupDragAnchorStart, anchorLane >= 0 ? clampedTarget - anchorLane : 0);
        invalidateCache();
        return;
    }

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
    const float y = bounds.y +
                    timelineRowTopY(currentTimelineLayout(), static_cast<float>(m_trackHeight),
                                    static_cast<float>(m_trackSpacing), targetLane, m_scrollOffset) +
                    2.0f;
    const float gridStartX = timelineGridStartX(0.0f, theme.getLayoutDimensions().trackControlsWidth);
    const double playheadBeats =
        snapBeatToGrid(m_trackManager->getPlaylistModel().secondsToBeats(std::max(0.0, m_trackManager->getPosition())));
    const float x =
        bounds.x + gridStartX + static_cast<float>(playheadBeats * m_pixelsPerBeat) - m_timelineScrollOffset + 2.0f;

    AestraUI::DragData dragData;
    dragData.type = AestraUI::DragDataType::File;
    dragData.filePath = filePath;
    dragData.displayName = displayName;
    const auto dropResult = placeDrop(dragData, AestraUI::NUIPoint(x, y)); // placed from the library: focus stays there
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
    const float y = bounds.y +
                    timelineRowTopY(currentTimelineLayout(), static_cast<float>(m_trackHeight),
                                    static_cast<float>(m_trackSpacing), targetLane, m_scrollOffset) +
                    2.0f;
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
    const auto dropResult = placeDrop(dragData, AestraUI::NUIPoint(x, y)); // placed from the library: focus stays there
    return dropResult.accepted;
}

void TrackManagerUI::finishInstantClipDrag() {
    if (!m_isDraggingClipInstant)
        return;

    Log::info("Finished instant clip drag");

    // Capture final position before clearing drag state
    double finalStartBeat = 0.0;
    PlaylistLaneID finalLaneId;
    bool moved = true; // unknown state (clip gone mid-drag) takes the full refresh
    if (m_trackManager && !m_groupDrag.empty()) {
        // The whole selection was dragged: one undo step holding a move per clip that actually moved.
        auto& playlist = m_trackManager->getPlaylistModel();
        auto macro = std::make_shared<Aestra::Audio::MacroCommand>("Move Clips");
        for (const auto& e : m_groupDrag) {
            const auto* now = playlist.getClip(e.id);
            if (!now) {
                continue;
            }
            const PlaylistLaneID nowLane = playlist.findClipLane(e.id);
            if (now->startBeat != e.startBeat || nowLane != e.lane) {
                macro->addCommand(std::make_shared<MoveClipCommand>(playlist, e.id, e.startBeat, e.lane,
                                                                    now->startBeat, nowLane));
            }
        }
        moved = !macro->empty();
        if (moved) {
            m_trackManager->getCommandHistory().pushAndExecute(macro);
        }
        m_groupDrag.clear();
    } else if (m_trackManager && m_draggedClipId.isValid()) {
        auto& playlist = m_trackManager->getPlaylistModel();
        if (const auto* clip = playlist.getClip(m_draggedClipId)) {
            finalStartBeat = clip->startBeat;
            finalLaneId = playlist.findClipLane(m_draggedClipId);
        }

        // Create undoable command for the move if position actually changed
        moved = finalStartBeat != m_clipOriginalStartTime || finalLaneId != m_clipOriginalLaneId;
        if (moved) {
            auto cmd = std::make_shared<MoveClipCommand>(playlist, m_draggedClipId, m_clipOriginalStartTime,
                                                         m_clipOriginalLaneId, finalStartBeat, finalLaneId);
            m_trackManager->getCommandHistory().pushAndExecute(cmd);
        }
    }

    m_isDraggingClipInstant = false;
    m_draggedClipTrack = nullptr;
    m_draggedClipId = ClipInstanceID{};
    m_suppressPlaylistRefresh = false; // Restore normal behavior

    if (m_window) {
        m_window->setMouseCapture(false);
    }

    // A release that changed nothing does nothing. The drag moved the model live with
    // playlist refreshes suppressed, so landing where it started leaves the model — and
    // the rows built before the drag — exactly as they were. The full path below rebuilt
    // every track row (a hitch on release) and cleared the MIDI scheduler, cutting every
    // sounding note and re-entering them late from the UI's lagging playhead: a click on
    // a clip during playback sounded like an edit.
    if (!moved) {
        invalidateCache();
        return;
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
    if (m_trackManager && !m_groupDrag.empty()) {
        auto& playlist = m_trackManager->getPlaylistModel();
        for (const auto& e : m_groupDrag) {
            playlist.moveClip(e.id, e.startBeat, e.lane);
        }
        m_groupDrag.clear();
    } else if (m_trackManager && m_draggedClipId.isValid()) {
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

    if (m_window) {
        m_window->setMouseCapture(false);
    }

    refreshTracks();
    invalidateCache();
}
void TrackManagerUI::copySelectedClip() {
    if (!m_trackManager || (!m_selectedClipId.isValid() && m_clipSelection.empty()))
        return;

    auto& playlist = m_trackManager->getPlaylistModel();
    const ClipInstanceID anchor = m_selectedClipId.isValid() ? m_selectedClipId : m_clipSelection.anchor();
    if (const auto* clip = playlist.getClip(anchor)) {
        m_clipboardClip = *clip; // the anchor stays the paint tool's stamp source and the single-clip paste
        Log::info("Copied clip: " + m_clipboardClip.name);
    }
    std::vector<ClipInstanceID> targets;
    m_clipSelection.forEachClip([&](const ClipInstanceID& id) { targets.push_back(id); });
    captureClipboardBlock(targets);
}

// A selection of several clips is copied as a block: every clip with its lane, so a paste keeps their layout.
void TrackManagerUI::captureClipboardBlock(const std::vector<ClipInstanceID>& targets) {
    m_clipboardBlock.clear();
    if (targets.size() < 2 || !m_trackManager)
        return;
    auto& playlist = m_trackManager->getPlaylistModel();
    for (const auto& id : targets) {
        if (const auto* clip = playlist.getClip(id)) {
            m_clipboardBlock.push_back({*clip, playlist.findClipLane(id)});
        }
    }
    if (m_clipboardBlock.size() < 2) {
        m_clipboardBlock.clear();
    }
}

// Pastes the clipboard block with its earliest clip at the playhead (snapped), each clip on the lane it came from.
// One undo step; the pasted clips become the selection.
void TrackManagerUI::pasteClipboardBlock() {
    if (!m_trackManager || m_clipboardBlock.empty())
        return;
    auto& playlist = m_trackManager->getPlaylistModel();
    double minStart = m_clipboardBlock.front().clip.startBeat;
    for (const auto& entry : m_clipboardBlock) {
        minStart = std::min(minStart, entry.clip.startBeat);
    }
    const double shift = snapBeatToGrid(secondsToBeats(m_trackManager->getPosition())) - minStart;

    auto macro = std::make_shared<Aestra::Audio::MacroCommand>("Paste Clips");
    std::vector<ClipInstanceID> newIds;
    for (const auto& entry : m_clipboardBlock) {
        if (laneIndexOf(entry.lane) < 0)
            continue; // its lane is gone
        ClipInstance pasted = entry.clip;
        pasted.id = ClipInstanceID::generate();
        pasted.startBeat = std::max(0.0, entry.clip.startBeat + shift);
        macro->addCommand(std::make_shared<Aestra::Audio::AddClipCommand>(playlist, entry.lane, pasted));
        newIds.push_back(pasted.id);
    }
    if (macro->empty())
        return;
    m_trackManager->getCommandHistory().pushAndExecute(macro);
    m_trackManager->refreshTimelinePatternInstances();
    refreshTracks();
    selectClips(newIds, TrackSelectionIntent::Replace);
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    Log::info("Pasted " + std::to_string(newIds.size()) + " clips as a block");
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
    captureClipboardBlock(targets); // before the clips go: the block is what a paste brings back

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
    if (!m_clipboardBlock.empty()) {
        pasteClipboardBlock();
        return;
    }

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

    // Select the new clip so repeated Ctrl+D continues to the right
    m_selectedClipId = newClip.id;

    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    Log::info("Paste-to-right at beat " + std::to_string(newClip.startBeat));
}

void TrackManagerUI::bounceSelectedClipsInPlace() {
    if (!m_trackManager || !m_onBounceInPlace)
        return;
    std::vector<ClipInstanceID> targets;
    m_clipSelection.forEachClip([&](const ClipInstanceID& id) { targets.push_back(id); });
    if (targets.empty() && m_selectedClipId.isValid())
        targets.push_back(m_selectedClipId);
    m_onBounceInPlace(targets);
    // The bounce replaced clips: the selection and the cached lanes both point at the old ones.
    clearClipSelection();
    m_trackManager->refreshTimelinePatternInstances();
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
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
    std::vector<std::shared_ptr<Aestra::Audio::DuplicateClipCommand>> duplicates;
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
        duplicates.push_back(cmd);
    }
    if (macro->empty()) {
        Log::warning("[TrackManager] Nothing duplicated — no valid selected clips");
        return;
    }
    m_trackManager->getCommandHistory().pushAndExecute(macro);

    // A clone's id is made when its command executes, so it can only be read now. (It was read before, every id came
    // back invalid, and "the clones become the selection" never happened: a second Ctrl+D had nothing selected.)
    ClipInstanceID lastDuplicateId{};
    std::vector<ClipInstanceID> newIds;
    for (const auto& cmd : duplicates) {
        if (cmd->getDuplicateId().isValid() && playlist.getClip(cmd->getDuplicateId())) {
            lastDuplicateId = cmd->getDuplicateId();
            newIds.push_back(lastDuplicateId);
        }
    }

    if (!newIds.empty()) {
        // The clones become the selection, so repeated Ctrl+D keeps appending
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
    newClip.startBeat = snapPlacementToGrid(beat); // the cell the pointer is in, not the nearest line

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

bool TrackManagerUI::isClipMuted(ClipInstanceID clipId) const {
    if (!m_trackManager || !clipId.isValid()) return false;
    const ClipInstance* clip = m_trackManager->getPlaylistModel().getClip(clipId);
    return clip && clip->edits.muted;
}

bool TrackManagerUI::toggleClipMute(ClipInstanceID clipId, std::optional<bool> target) {
    if (!m_trackManager || !clipId.isValid()) return false;
    auto& model = m_trackManager->getPlaylistModel();
    const ClipInstance* clip = model.getClip(clipId);
    if (!clip) return false;
    const bool before = clip->edits.muted;
    const bool next = target.value_or(!before);
    if (next == before) return next; // already there: no undo step for nothing

    ClipEdits edits = clip->edits;
    edits.muted = next;
    auto change = std::make_shared<SetClipEditsCommand>(model, clipId, edits);
    change->named(next ? "Mute Clip" : "Unmute Clip");
    m_trackManager->getCommandHistory().pushAndExecute(change);

    // A muted clip must go quiet (or come back) while the timeline is playing, the same refresh paint and duplicate use.
    m_trackManager->refreshTimelinePatternInstances();
    refreshTracks();
    invalidateCache();
    scheduleTimelineMinimapRebuild();
    return next;
}

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
