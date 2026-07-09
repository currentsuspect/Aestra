# Deep Undo/Redo Trust — Implementation Plan
Date: 2026-04-13
Priority: Beta blocker (user trust)

## Goal
Every user-facing modification in Mixer, Piano Roll, and Arsenal goes through CommandHistory. Ctrl+Z/Ctrl+Y works everywhere, not just timeline.

## Current State
- Timeline: fully wired ✅ (18 pushAndExecute calls)
- Mixer: commands exist (SetVolume, SetMute, SetSolo, SetPan) but NOT wired
- Piano Roll: no commands, direct modification
- Arsenal: no commands, direct modification

## Phase 1: Wire Existing Mixer Commands (Low risk)

The commands exist. Just wire the mixer UI to use them.

### Files to modify:

**MixerPanel or MixerViewModel** — wherever fader/btn changes happen:
- Fader drag → `pushAndExecute(SetVolumeCommand(...))` instead of `setVolume()` directly
- Mute click → `pushAndExecute(SetMuteCommand(...))` instead of `setMute()` directly
- Solo click → `pushAndExecute(SetSoloCommand(...))` instead of `setSolo()` directly
- Pan knob → `pushAndExecute(SetPanCommand(...))` instead of `setPan()` directly

The mixer needs access to `CommandHistory` — pass `m_trackManager->getCommandHistory()` reference.

## Phase 2: Create Piano Roll Commands (Medium risk)

New commands needed:
- **AddNoteCommand** — add a MIDI note to a pattern
- **RemoveNoteCommand** — remove a MIDI note from a pattern
- **MoveNoteCommand** — change note startBeat/pitch
- **ResizeNoteCommand** — change note durationBeats
- **ChangeVelocityCommand** — change note velocity
- **ClearNotesCommand** — delete all notes in a pattern (for the Piano Roll)

Each command stores:
- PatternID (which pattern)
- MidiNote (the note data, before/after for move/resize)
- PatternManager reference (for applyPatch)

### Files to create:
- `AestraAudio/include/Commands/AddNoteCommand.h`
- `AestraAudio/include/Commands/RemoveNoteCommand.h`
- `AestraAudio/include/Commands/MoveNoteCommand.h`
- `AestraAudio/include/Commands/ResizeNoteCommand.h`

### Files to modify:
- `Source/Panels/PianoRollPanel.cpp` — replace direct note manipulation with commands
- `Source/Panels/PianoRollPanel.h` — add CommandHistory reference

## Phase 3: Create Arsenal Commands (Medium risk)

New commands needed:
- **CreateUnitCommand** — create a new Arsenal unit
- **RemoveUnitCommand** — remove an Arsenal unit
- **SetUnitMuteCommand** — mute/unmute a unit
- **SetUnitSoloCommand** — solo/unsolo a unit
- **SetUnitColorCommand** — change unit color

### Files to create:
- `AestraAudio/include/Commands/CreateUnitCommand.h`
- `AestraAudio/include/Commands/RemoveUnitCommand.h`

### Files to modify:
- `Source/Panels/ArsenalPanel.cpp` — wire commands for unit operations

## Phase 4: Unified Ctrl+Z/Ctrl+Y Handler

Currently undo/redo is handled in TrackManagerUI::onKeyEvent. Make sure:
1. Ctrl+Z calls `getCommandHistory().undo()`
2. Ctrl+Y / Ctrl+Shift+Z calls `getCommandHistory().redo()`
3. Undo/redo triggers UI refresh in ALL panels (timeline, mixer, piano roll, arsenal)

The `invalidateCache()` + `refreshTracks()` calls after undo/redo need to cascade to all active panels.

## Verification
1. Build: cmake --build build --target Aestra -j2
2. Test mixer: change volume → Ctrl+Z → volume restores
3. Test piano roll: add note → Ctrl+Z → note removed
4. Test arsenal: create unit → Ctrl+Z → unit removed
5. Test cross-panel: change volume in mixer → Ctrl+Z in timeline → mixer updates
