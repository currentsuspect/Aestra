# Aestra Command Model Specification

**Document ID:** E-001  
**Author:** Engineering  
**Date:** 2026-02-24  
**Status:** Draft for Review  

---

## Purpose

Define the canonical command model for Aestra's undo/redo system. This spec covers:
- Command interface contract
- All supported operations for v1 Beta
- Transaction/grouping semantics
- Thread safety requirements
- Error handling expectations

---

## 1. Command Interface Contract

### 1.1 ICommand Interface

```cpp
class ICommand {
public:
    virtual ~ICommand() = default;
    
    virtual void execute() = 0;           // Perform the operation
    virtual void undo() = 0;              // Reverse the operation
    virtual void redo() { execute(); }    // Re-perform (default: call execute)
    
    virtual std::string getName() const = 0;        // UI display name
    virtual size_t getSizeInBytes() const { return sizeof(*this); }  // Memory estimation
    virtual bool changesProjectState() const { return true; }        // Dirty flag trigger
};
```

### 1.2 Command Lifecycle

```
[Created] → pushAndExecute() → [Executed] → [On Undo Stack]
                                        ↓
                              undo() called
                                        ↓
                              [On Redo Stack]
                                        ↓
                              redo() called
                                        ↓
                              [On Undo Stack]
```

### 1.3 Thread Safety

- `CommandHistory` is mutex-protected
- Commands themselves must be safe to execute from the UI thread
- Audio thread must NEVER execute commands directly

### 1.4 Model Synchronization (Critical)

**Problem:** Audio thread reads `PlaylistModel` while `MoveClipCommand::execute()` writes to it → data race.

**Solution:** Commands require exclusive model access during execution.

```cpp
// Option A: Stop audio engine during modifications (simplest for Beta)
void CommandHistory::pushAndExecute(std::shared_ptr<ICommand> cmd) {
    AudioEngine::instance().pause();  // Stop audio processing
    cmd->execute();
    m_undoStack.push_back(cmd);
    AudioEngine::instance().resume();
}

// Option B: Lock-free model updates with atomic swaps (post-Beta)
// - Commands build a new model snapshot
// - Atomic swap replaces the active model
// - Audio thread always sees consistent state
```

**Beta Decision:** Use Option A (pause audio during command execution).

**Requirements:**
1. `execute()`, `undo()`, `redo()` must complete quickly (<10ms target)
2. Audio engine must support pause/resume without artifacts
3. UI must indicate when audio is paused for edit

**Destructor Safety:** Commands use `shared_ptr` — safe to destroy while references exist.

---

## 2. Command Categories

### 2.1 Clip Operations (EPIC F)

| Command | execute() | undo() | Status |
|---------|-----------|--------|--------|
| `AddClipCommand` | Add clip to lane | Remove clip | ✅ Implemented |
| `RemoveClipCommand` | Remove clip from lane | Restore clip | ❌ TODO |
| `MoveClipCommand` | Change clip position/beat | Restore original position | ❌ TODO |
| `SplitClipCommand` | Split clip at beat | Merge back | ✅ Implemented |
| `TrimClipCommand` | Adjust clip start/end | Restore original bounds | ❌ TODO |
| `DuplicateClipCommand` | Create copy at new position | Remove duplicate | ❌ TODO |
| `ResizeClipCommand` | Change clip duration | Restore original duration | ❌ TODO |

### 2.2 Lane/Track Operations

| Command | execute() | undo() | Status |
|---------|-----------|--------|--------|
| `AddLaneCommand` | Create new lane | Remove lane | ❌ TODO |
| `RemoveLaneCommand` | Remove lane + clips | Restore lane + clips | ❌ TODO |
| `RenameLaneCommand` | Change lane name | Restore original name | ❌ TODO |
| `ReorderLaneCommand` | Move lane position | Restore original position | ❌ TODO |

### 2.3 Mixer Operations (EPIC G)

| Command | execute() | undo() | Status |
|---------|-----------|--------|--------|
| `SetVolumeCommand` | Change lane volume | Restore original volume | ❌ TODO |
| `SetPanCommand` | Change lane pan | Restore original pan | ❌ TODO |
| `SetMuteCommand` | Toggle mute state | Restore mute state | ❌ TODO |
| `SetSoloCommand` | Toggle solo state | Restore solo state | ❌ TODO |
| `SetMasterVolumeCommand` | Change master volume | Restore original | ❌ TODO |

### 2.4 Pattern/Playlist Operations

| Command | execute() | undo() | Status |
|---------|-----------|--------|--------|
| `CreatePatternCommand` | Create new pattern | Delete pattern | ❌ TODO |
| `DeletePatternCommand` | Delete pattern | Restore pattern | ❌ TODO |
| `DuplicatePatternCommand` | Copy pattern | Remove copy | ❌ TODO |
| `MovePatternInstanceCommand` | Move in playlist | Restore position | ❌ TODO |

### 2.5 Transport/Tempo Operations (EPIC H)

| Command | execute() | undo() | Status |
|---------|-----------|--------|--------|
| `SetTempoCommand` | Change project tempo | Restore original tempo | ❌ TODO |
| `SetTimeSignatureCommand` | Change time sig | Restore original | ❌ TODO |

---

## 3. Transaction Support (E-003)

### 3.1 Problem

User drags 5 clips simultaneously → 5 undo steps → bad UX.

### 3.2 Solution: MacroCommand

```cpp
class MacroCommand : public ICommand {
public:
    void addCommand(std::shared_ptr<ICommand> cmd);
    
    void execute() override {
        for (auto& cmd : m_commands) {
            cmd->execute();
        }
    }
    
    void undo() override {
        // Reverse order for undo
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
            (*it)->undo();
        }
    }
    
    std::string getName() const override {
        return m_name.empty() ? "Macro" : m_name;
    }

private:
    std::vector<std::shared_ptr<ICommand>> m_commands;
    std::string m_name;
};
```

### 3.3 Usage Pattern

```cpp
auto macro = std::make_shared<MacroCommand>();
macro->setName("Move Clips");

for (auto& clip : selectedClips) {
    macro->addCommand(std::make_shared<MoveClipCommand>(clip, newPos));
}

commandHistory.pushAndExecute(macro);
```

---

## 4. Command State Capture

### 4.1 Rule: Capture State at Construction

Commands must capture all state needed for undo at construction time:

```cpp
class MoveClipCommand : public ICommand {
public:
    MoveClipCommand(PlaylistModel& model, ClipInstanceID clipId, double newBeat)
        : m_model(model)
        , m_clipId(clipId)
        , m_newBeat(newBeat)
        , m_originalBeat(model.getClipBeat(clipId))  // Capture NOW
    {}
    
private:
    PlaylistModel& m_model;
    ClipInstanceID m_clipId;
    double m_newBeat;
    double m_originalBeat;  // Saved for undo
};
```

### 4.2 No Re-Fetching During Undo

```cpp
// ❌ WRONG - state may have changed
void undo() {
    auto oldBeat = m_model.getClipBeat(m_clipId);  // WRONG!
}

// ✅ CORRECT - use captured state
void undo() {
    m_model.setClipBeat(m_clipId, m_originalBeat);
}
```

---

## 5. Error Handling

### 5.1 Execution Failure

If `execute()` fails:
- Command is NOT added to undo stack
- Exception propagates to caller
- User sees error message

### 5.2 Undo Failure

If `undo()` fails:
- Log error
- Command is NOT added to redo stack
- History may be corrupted → clear history

### 5.3 Redo Failure

If `redo()` fails:
- Log error
- Command is NOT added to undo stack

---

## 6. History Management

### 6.1 Limits

- Default max: 100 commands
- Configurable via `setMaxHistorySize()`
- Oldest commands trimmed when exceeded

### 6.2 Clear Conditions

- Project load → clear history
- Project close → clear history
- Explicit user action → clear history

### 6.3 Memory Considerations

Commands holding audio data should:
- Store references (file paths, offsets) not copies
- Implement `getSizeInBytes()` accurately
- Be trimmed aggressively

---

## 7. UI Integration

### 7.1 Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+Shift+Z | Redo (alternate) |

### 7.2 Menu Items

- Edit → Undo: `{getUndoName()}`
- Edit → Redo: `{getRedoName()}`
- Grayed out when `canUndo()`/`canRedo()` false

### 7.3 State Callback

```cpp
commandHistory.setOnStateChanged([this]() {
    updateUndoRedoButtons();
    updateEditMenu();
});
```

---

## 8. Testing Requirements

### 8.1 Unit Tests Required

- Each command: execute → verify state → undo → verify original
- Each command: execute → undo → redo → verify state
- MacroCommand: multi-command grouping
- History: max size enforcement
- History: clear on project load

### 8.2 Integration Tests

- Drag 10 clips → single undo restores all
- Rapid undo/redo while playing
- Undo during recording (should be blocked)

---

## 9. Implementation Priority (Phase 2)

### P0 — Beta Blockers

1. `MoveClipCommand` — most common operation
2. `RemoveClipCommand` — essential
3. `MacroCommand` — transaction support
4. `SetVolumeCommand` — mixing basic
5. `SetTempoCommand` — tempo changes

### P1 — Expected

6. `TrimClipCommand`
7. `DuplicateClipCommand`
8. `SetPanCommand`
9. `SetMuteCommand` / `SetSoloCommand`

### P2 — Nice to Have

10. Pattern operations
11. Lane operations
12. Time signature changes

---

## 10. Open Questions

1. **Undo during playback?** Block or allow?
2. **Undo during recording?** Definitely block.
3. **Undo history persistence?** Save with project or discard on close?
4. **Memory limit behavior?** Warn user or silent trim?

---

## Appendix A: File Locations

```
AestraAudio/
├── include/Commands/
│   ├── ICommand.h           # Interface
│   ├── CommandHistory.h     # History manager
│   ├── AddClipCommand.h
│   ├── RemoveClipCommand.h  # TODO
│   ├── MoveClipCommand.h    # TODO
│   ├── SplitClipCommand.h
│   ├── TrimClipCommand.h    # TODO
│   ├── MacroCommand.h       # TODO
│   └── ...
└── src/Commands/
    ├── CommandHistory.cpp
    ├── AddClipCommand.cpp
    └── ...
```

---

*Last updated: 2026-02-24*
