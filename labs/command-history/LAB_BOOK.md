# Command History Lab Book

## Purpose

Persistent memory for the command history (undo/redo) lab. Tracks correctness
gates across 6 test suites covering CommandHistory, MacroCommand, MoveClip,
ClipCommands, MixerCommands, and CommandTransaction.

## Structure

```
labs/command-history/
├── program.md              — Constitution (scope, rules, gates)
├── EVALS.md                — Eval documentation (build, lanes, thresholds)
├── LAB_BOOK.md             — This file
├── result_schema.json      — JSON schema for eval results
├── run_eval.sh             — Eval runner script
├── results/                — Generated eval outputs (gitignored)
├── sessions/               — Per-session logs
└── findings/               — Durable knowledge
    ├── accepted_patterns.md
    ├── rejected_patterns.md
    ├── invariants.md
    └── bottlenecks.md
```

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, eval lanes
3. `LAB_BOOK.md` — this file
4. `findings/invariants.md` — things that must never break

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| M001 | 2026-04-05 | 0 | 0 | 0 | Maintenance pass: corrected real test target names, tightened governance, repaired eval runner alignment |
| M002 | 2026-04-05 | 1 | 0 | 0 | Validation run: all six command suites passed; dirty-tree maintenance run so not treated as a baseline capture |

## Current State

- **Branch**: `develop`
- **Last commit**: N/A (lab scaffold only)
- **Baselines**: Green summary captured in `results/summary.json`, but it was produced on a dirty maintenance tree and is not an accepted baseline capture
- **Known issues**: No benchmark lane yet; this remains a correctness-only lab until real observability exists

## Selective Retrieval

1. Check `LAB_BOOK.md` first for session summary.
2. If a specific optimization is relevant, check `findings/accepted_patterns.md`.
3. If a failure mode is relevant, check `findings/rejected_patterns.md`.
4. Only read full session logs when needed.
