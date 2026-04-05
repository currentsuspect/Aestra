# Command History Lab — Eval Documentation

## Build Commands

```bash
cmake -S . -B build-autoresearch \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-autoresearch --parallel 2
```

### Target list

| Target | Purpose |
|--------|---------|
| `CommandHistoryTest` | Core undo/redo correctness (15 tests) |
| `MacroCommandTest` | Batch command execution/undo |
| `MoveClipCommandTest` | Clip move command correctness |
| `ClipCommandsTest` | Add/duplicate/trim/split clip commands |
| `MixerCommandsTest` | Volume/pan/mute/solo commands |
| `CommandTransactionTest` | Atomic command transactions |

## Eval Lanes

### Lane 1: CommandHistoryTest (Core Correctness)

Runs all 15 unit tests:
- pushAndExecute, undo, redo
- undo/redo sequence
- clear redo on new command
- empty undo/redo
- history limits (count-based)
- clear
- callback accuracy
- execute/undo failure handling
- null command
- memory limit (count-based trimming)
- getHistoryMemoryUsage
- memory limit unlimited (0 = unlimited)

**Gate**: HARD — exit code must be 0.

### Lane 2: MacroCommandTest

Tests batch command execution and undo/redo of command groups.

**Gate**: HARD — exit code must be 0.

### Lane 3: MoveClipCommandTest

Tests clip movement command with position tracking.

**Gate**: HARD — exit code must be 0.

### Lane 4: ClipCommandsTest

Tests add, duplicate, trim, split clip commands.

**Gate**: HARD — exit code must be 0.

### Lane 5: MixerCommandsTest

Tests volume, pan, mute, solo commands.

**Gate**: HARD — exit code must be 0.

### Lane 6: CommandTransactionTest

Tests atomic transaction grouping.

**Gate**: HARD — exit code must be 0.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| Any test binary exit code | HARD | Reject + revert |
| Build warnings in command code | HARD | Reject + investigate |
| Memory usage accuracy | ADVISORY | Flag + investigate |
| History trim behavior | ADVISORY | Flag + investigate |

## Baseline Policy

- This is a correctness-first lab. The primary baseline is a full green run of
  all six binaries on the current machine.
- No performance baseline is accepted until a real command-history benchmark
  exists. Do not invent one.
- `summary.json` is the machine-readable record for the latest run. Historical
  raw outputs remain in `results/` and session conclusions belong in
  `sessions/` plus `LAB_BOOK.md`.

## Noise Policy

- This lab currently has no trusted benchmark lane.
- Unit-test outcomes are expected to be deterministic.
- If a test fails intermittently, treat the run as `inconclusive`, log it, and
  do not accept any optimization claim from that round.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% (all tests must pass) |
| Build warnings | 0 new warnings in command code |
