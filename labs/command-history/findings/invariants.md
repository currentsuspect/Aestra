# Invariants

Things that must never break in the command history subsystem.

## Correctness Invariants

1. **Undo reverses execute** — `undo()` must reverse the effects of the last `execute()`.
2. **Redo re-executes** — `redo()` must re-execute the last undone command.
3. **New command clears redo stack** — Executing a new command after undo clears redo history.
4. **Empty history is safe** — `undo()`/`redo()` on empty history return `false` without crashing.
5. **Null command is safe** — `pushAndExecute(nullptr)` does not crash.
6. **Failed execute does not add to history** — If `execute()` throws, the command is not recorded.
7. **Failed undo returns false** — If `undo()` throws, it returns `false` and history is not corrupted.

## Limit Invariants

8. **History size limit** — Oldest entries are trimmed when count exceeds `maxHistorySize`.
9. **Memory limit** — Oldest entries are trimmed when `getHistoryMemoryUsage()` exceeds `maxHistoryMemory`.
10. **Memory limit 0 = unlimited** — No trimming based on memory when limit is 0.

## Callback Invariants

11. **onStateChanged fires** — Callback fires on every execute, undo, and redo.

## Transaction Invariants

12. **Transaction atomicity** — Commands within a transaction undo/redo as a unit.
13. **MacroCommand order** — Execute runs children in order; undo runs in reverse order.

## Build Invariants

14. **No new warnings** — Command code compiles cleanly.
