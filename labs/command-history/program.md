# Command History Lab — Program Constitution

## Scope

This lab targets the `CommandHistory` undo/redo subsystem in `AestraAudio`.
It covers:

- `AestraAudio/include/Commands/CommandHistory.h`
- `AestraAudio/src/Commands/CommandHistory.cpp`
- `AestraAudio/include/Commands/ICommand.h`
- `AestraAudio/include/Commands/MacroCommand.h`
- `AestraAudio/include/Commands/CommandTransaction.h`
- `AestraAudio/src/Commands/CommandTransaction.cpp`
- `AestraAudio/include/Commands/MoveClipCommand.h`
- `AestraAudio/include/Commands/AddClipCommand.h`
- `AestraAudio/include/Commands/DuplicateClipCommand.h`
- `AestraAudio/include/Commands/TrimClipCommand.h`
- `AestraAudio/include/Commands/SplitClipCommand.h`
- `AestraAudio/include/Commands/SetVolumeCommand.h`
- `AestraAudio/include/Commands/SetPanCommand.h`
- `AestraAudio/include/Commands/SetMuteCommand.h`
- `AestraAudio/include/Commands/SetSoloCommand.h`
- `Tests/Commands/CommandHistoryTest.cpp`
- `Tests/Commands/MacroCommandTest.cpp`
- `Tests/Commands/MoveClipCommandTest.cpp`
- `Tests/Commands/ClipCommandsTest.cpp`
- `Tests/Commands/MixerCommandsTest.cpp`
- `Tests/Commands/CommandTransactionTest.cpp`

No other engine, UI, plugin-host, or platform code is in scope.

## Allowed Files

An autonomous agent may modify:

1. **Command system implementation** (all files listed under Scope above)
2. **Test harnesses** (observability only, not correctness gates)
3. **Lab infrastructure** (`labs/command-history/`)
4. **Build files** (only if required for lab targets): `Tests/CMakeLists.txt`

## Forbidden Behavior

- **DO NOT** modify unrelated engine, UI, plugin, or platform code.
- **DO NOT** weaken or remove test assertions to make benchmarks pass.
- **DO NOT** change the public API of `CommandHistory` or `ICommand` without strong reason.
- **DO NOT** remove memory limits or history size limits to improve performance.
- **DO NOT** introduce data races or remove thread-safety guarantees.
- **DO NOT** invent baseline files or fake golden outputs.

## Invariants

1. **Undo correctness**: `undo()` reverses the last executed command's effects.
2. **Redo correctness**: `redo()` re-executes the last undone command.
3. **New command clears redo**: Executing a new command after undo clears the redo stack.
4. **Empty history**: `undo()`/`redo()` on empty history return `false` gracefully.
5. **History size limit**: Oldest entries are trimmed when `maxHistorySize` is exceeded.
6. **Memory limit**: Oldest entries are trimmed when `getHistoryMemoryUsage()` exceeds `maxHistoryMemory`.
7. **Failure safety**: Failed `execute()` does not add to history; failed `undo()` returns `false`.
8. **Null safety**: `pushAndExecute(nullptr)` does not crash.
9. **Callback accuracy**: `onStateChanged` fires on every execute/undo/redo.
10. **Transaction atomicity**: Commands within a transaction undo/redo as a unit.
11. **MacroCommand**: Executes/undoes all child commands in order/reverse order.

## Acceptance Logic

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `CommandHistoryTest` | All 15 tests pass (exit code 0) |
| `MacroCommandTest` | All tests pass (exit code 0) |
| `MoveClipCommandTest` | All tests pass (exit code 0) |
| `ClipCommandsTest` | All tests pass (exit code 0) |
| `MixerCommandsTest` | All tests pass (exit code 0) |
| `CommandTransactionTest` | All tests pass (exit code 0) |
| Build | Compiles cleanly (no new warnings in command code) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| Memory usage per command | No regression in `getHistoryMemoryUsage()` accuracy |
| History trim behavior | Oldest entries trimmed correctly under limits |
| Thread safety | No data races under concurrent access (if tested) |

### Decision Status

- **`accept`**: All hard gates pass, advisory gates within tolerance.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but some tests could not run.

## Reporting Format

All eval runs produce JSON at `labs/command-history/results/run_<timestamp>.json`
conforming to `labs/command-history/result_schema.json`.

The eval runner (`labs/command-history/run_eval.sh`) emits
`labs/command-history/results/summary.json`.

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — session summaries, finding pointers
4. `findings/invariants.md` — things that must never break

**Do NOT** load full session history by default.

## Session Budget

- One maintenance or optimization pass per session.
- Prefer at most one hypothesis per round.
- Stop when all hard gates are green and the next change would widen scope.

## Session-End Reporting

1. Write session log to `sessions/<date>_session_<NNN>.md`
2. Update findings files with durable knowledge
3. Update `LAB_BOOK.md` session summary table
4. Commit lab-book changes: `command-history-lab: update lab-book after session NNN`
5. Verify clean working tree

## Git Rules

- Start from a clean working tree.
- Confirm no leftover changes before each round.
- Dirty-tree runs are allowed only for lab maintenance, and must be recorded as
  such in the session log. Do not treat them as optimization acceptances.
- For rejected/inconclusive rounds: `git checkout -- .`
- For accepted rounds: commit immediately with `command-history-lab: accept round NN <hypothesis>`
- Do not rewrite history during the session.
- End with a clean working tree.

## Keep/Revert Discipline

- Every change is tracked by git commit.
- `accept`: commit stands, update baselines, next round.
- `reject`: `git checkout -- .` to discard, log failure.
- `inconclusive`: re-run once; if still, `git checkout -- .` and investigate.
- Never accumulate rejected changes.

## Anti-Gaming Rules

- Do not claim a win from fewer assertions, weaker fixtures, or changed command
  semantics.
- Do not treat a passing subset as acceptance; all six command suites must pass.
- Do not count dirty-tree maintenance runs as baseline captures.
- If a command-specific regression appears, fix the lab or revert the round;
  never paper over it in findings files.
