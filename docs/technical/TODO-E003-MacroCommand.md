# TODO: E-003 MacroCommand Multi-Clip Drag

**Status:** Pending
**Priority:** P1 (Phase 2)
**Blocked by:** None

## What's Needed

Wire MacroCommand for multi-clip drag operations so users get a single undo step when moving multiple clips.

## Current State

- `MacroCommand` class exists and is tested
- `TrackManagerUI` only tracks `m_selectedClipId` (single clip)
- Drag operations directly modify `PlaylistModel` without going through `CommandHistory`

## Implementation Plan

1. **Add multi-clip selection tracking:**
   ```cpp
   std::vector<ClipInstanceID> m_selectedClipIds;
   std::unordered_map<ClipInstanceID, double> m_clipDragOriginalPositions;
   std::unordered_map<ClipInstanceID, PlaylistLaneID> m_clipDragOriginalLanes;
   ```

2. **Capture original positions at drag start:**
   - In `startInstantClipDrag()`, record all selected clips' positions

3. **Build MacroCommand at drag end:**
   ```cpp
   auto macro = std::make_shared<MacroCommand>();
   macro->setName("Move Clips");
   
   for (auto& clipId : m_selectedClipIds) {
       auto* clip = playlist.getClip(clipId);
       if (clip && clip->startBeat != m_clipDragOriginalPositions[clipId]) {
           macro->addCommand(std::make_shared<MoveClipCommand>(
               playlist, clipId, clip->startBeat));
       }
   }
   
   history.pushAndExecute(macro);
   ```

4. **Add Ctrl+click multi-select UI**

5. **Add marquee selection for clips**

## Files to Modify

- `Source/Components/TrackManagerUI.h` — Add selection tracking
- `Source/Components/TrackManagerUI.cpp` — Implement multi-select + MacroCommand
- `AestraAudio/include/Commands/MoveClipCommand.h` — Already done

## Estimated Effort

~2-3 hours

---

*Created: 2026-03-14*
