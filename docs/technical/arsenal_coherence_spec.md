# Arsenal Coherence Specification

Status: implementation target for the November 18, 2026 Arsenal patch.

## Product contract

Arsenal must never manufacture musical state merely because a source folder was selected.

A kit/folder is source material. Units, samples, and patterns are musical/project state. Selecting a kit from the empty-state card opens that folder in the File Browser; it does not create units, assign samples, or create patterns.

Browsing and loading are separate actions.

## Unit lifecycle

Creating a unit is one undoable operation. Creation includes the unit's default pattern and default mixer destination.

Undoing creation removes the unit, its unplaced default pattern, and the created mixer destination as one transaction. Redo restores the same unit and pattern identities.

Deleting a unit is one undoable operation.

Deletion:
- removes only the requested unit(s);
- removes notes owned by the deleted unit;
- removes an unplaced default pattern owned by the unit;
- retains a default pattern that is independently used by the timeline;
- never creates a replacement unit or pattern;
- restores the exact unit identity/order, pattern identity, and owned notes on undo.

Deleting the last unit is allowed. Arsenal may become genuinely empty rather than silently creating a replacement.

## Sample lifecycle

A dropped/selected sample is one undoable operation.

The sample loader owns asynchronous decoding. Once the decoded sample is committed, an already-executed sample command is inserted into history so the user does not have to wait for synchronous decoding.

Undo restores:
- previous sample;
- previous unit name;
- previous enabled state.

Redo restores the newly loaded sample.

Clearing a sample must clear the sampler's actual audio buffer, not merely its stored file path.

## Selection

Unit and pattern selection are first-class.

- Plain click selects one.
- Shift-click selects a contiguous range.
- Ctrl-click toggles membership.
- A selection is visually explicit.
- The primary selected unit remains the target for editing, MIDI input, and other single-target workflows.

Pattern multi-selection can be deleted in one undoable transaction.

Deletion never deletes timeline-used patterns. At least one MIDI pattern remains available.

## Swing

Swing has an explicit target.

- If units are selected, Swing edits those units only.
- If multiple selected units have different values, the control displays MIX.
- If no unit is selected, Swing edits the pattern-wide fallback.
- Double-click resets the current target(s) to 0%.
- Dragging changes the value immediately for playback/feedback, but the whole gesture becomes one undo step on release.
- Per-unit swing overrides inherit the pattern-wide value until explicitly overridden.
- Per-unit swing is persisted and participates in pattern signatures.

Playback resolves swing per note's unit ID.

## State invariants

1. Loading a source folder never creates musical state.
2. Browsing never changes a unit.
3. Deleting never creates unrelated state.
4. Every meaningful Arsenal mutation is undoable.
5. Undo/redo restores stable identities wherever practical.
6. A control must have an unambiguous target.
7. Multi-selection operations are atomic from the user's undo perspective.
8. Derived UI state must reconcile after external undo/redo/deletion.

## Regression coverage

At minimum:
- per-unit swing expansion;
- unit creation undo/redo;
- unit deletion undo/redo;
- default-pattern cleanup;
- owned-note restoration;
- multi-selection semantics;
- sample-drop undo;
- swing drag/reset undo;
- kit selection produces no unit/sample/pattern mutation.
