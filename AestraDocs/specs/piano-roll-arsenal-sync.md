# Piano Roll ↔ Arsenal Sync — Implementation Plan
Date: 2026-04-13
Priority: Beta blocker (user-facing workflow)

## Goal
Double-click a unit in Arsenal → Piano Roll opens with that unit's pattern → edit notes → save → Arsenal + Timeline reflect changes instantly.

## Current State
- `UnitManager` has full CRUD but `UnitInfo` has no `PatternID`
- `ArsenalPanel::setOnRequestEditor` fires on double-click → opens PLUGIN editor (not Piano Roll)
- `PianoRollPanel` has `loadPattern(PatternID)` and `savePattern()` — works
- `PatternPlaybackEngine` renders patterns in real-time — works
- `MidiNote` has `unitId` for routing — works

## What's Missing
1. `UnitInfo` needs a `defaultPatternId` field
2. `UnitManager::createUnit()` should auto-create a pattern in PatternManager and link it
3. ArsenalPanel's editor callback should open Piano Roll (not plugin editor)
4. Piano Roll's `onPatternEdited` callback needs to notify Arsenal to refresh

## Implementation Steps

### Step 1: Add `defaultPatternId` to `UnitInfo`
**File:** `AestraAudio/include/Models/UnitManager.h`
**Change:** Add `PatternID defaultPatternId;` to `UnitInfo` struct (after line 56)

### Step 2: Wire `UnitManager::createUnit()` to auto-create pattern
**File:** `AestraAudio/src/Models/UnitManager.cpp`
**Change:** `createUnit()` should:
1. Create a new PatternSource via PatternManager
2. Set the pattern's name to match the unit's name
3. Store the PatternID in UnitInfo::defaultPatternId

**Dependency:** UnitManager needs a reference to PatternManager.
- Add `PatternManager* m_patternManager{nullptr};` to UnitManager
- Add `void setPatternManager(PatternManager* pm)` setter
- Call `setPatternManager()` from AestraContent during init

### Step 3: Update ArsenalPanel editor callback
**File:** `Source/Panels/ArsenalPanel.cpp` (where double-click is handled)
**Change:** Double-click behavior:
- If unit has a plugin → fire `onRequestEditor(UnitID)` (existing behavior → plugin editor)
- If unit has no plugin but has `defaultPatternId` → fire `onRequestPatternEditor(PatternID)` (new callback)

**File:** `Source/Panels/ArsenalPanel.h`
**Change:** Add `std::function<void(PatternID)> m_onRequestPatternEditor;` and setter.

### Step 4: Wire new callback in AestraContent
**File:** `Source/Core/AestraContent.cpp` (around line 453)
**Change:**
```cpp
m_sequencerPanel->setOnRequestPatternEditor([this](PatternID patternId) {
    openPatternInPianoRoll(patternId);
});
```
This reuses the existing `openPatternInPianoRoll()` which already calls `m_pianoRollPanel->loadPattern(patternId)`.

### Step 5: Add Arsenal refresh on Piano Roll edit
**File:** `Source/Core/AestraContent.cpp` (around line 431, where `setOnPatternEdited` is set)
**Change:** Add a call to refresh the Arsenal panel when a pattern is edited:
```cpp
m_pianoRollPanel->setOnPatternEdited([this](PatternID patternId) {
    // Save pattern to manager
    m_pianoRollPanel->savePattern();
    // Refresh Arsenal if it's showing this unit's pattern
    if (m_sequencerPanel) {
        m_sequencerPanel->refreshUnits();
    }
});
```

### Step 6: Update serialization
**File:** `AestraAudio/src/Models/UnitManager.cpp`
**Change:** `saveToJSON()` and `loadFromJSON()` must include `defaultPatternId`.

## Verification
1. Build: `cmake --build build --target Aestra -j2`
2. Launch Aestra
3. Create a unit in Arsenal
4. Double-click the unit → Piano Roll should open with the unit's pattern
5. Add notes in Piano Roll
6. Close Piano Roll → Arsenal should show the updated pattern
7. Play → notes should sound through the correct unit
8. Save project → reload → pattern should persist

## Risk
- **Low risk**: All pieces exist (PianoRollPanel, PatternManager, PatternPlaybackEngine)
- **Medium risk**: PatternManager reference in UnitManager creates a new dependency
- **Mitigation**: Null-check PatternManager pointer; unit creation without PatternManager just skips pattern creation (degraded but not broken)
