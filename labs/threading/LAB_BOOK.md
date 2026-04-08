# Threading Lab Book

## Purpose

Persistent memory for the threading primitives lab. Tracks correctness of
LockFreeRingBuffer, ThreadPool, Barrier, and atomic utilities.

## Structure

```
labs/threading/
├── program.md              — Constitution
├── EVALS.md                — Eval documentation
├── LAB_BOOK.md             — This file
├── result_schema.json      — JSON schema
├── run_eval.sh             — Eval runner
├── results/                — Generated outputs
├── sessions/               — Per-session logs
└── findings/               — Durable knowledge
    ├── accepted_patterns.md
    ├── rejected_patterns.md
    ├── invariants.md
    └── bottlenecks.md
```

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, lanes
3. `LAB_BOOK.md` — this file
4. `findings/invariants.md` — things that must never break

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| M001 | 2026-04-05 | 0 | 0 | 0 | Maintenance pass: corrected real target name, repaired eval runner JSON bug, tightened anti-gaming guidance |
| M002 | 2026-04-05 | 1 | 0 | 0 | Validation run: `ThreadingTests` built on demand and passed; dirty-tree maintenance run so not treated as a baseline capture |

## Current State

- **Branch**: `develop`
- **Last commit**: N/A (lab scaffold only)
- **Baselines**: Passing `results/summary.json` captured on a dirty maintenance tree; not an accepted baseline capture
- **Known issues**: Benchmark lane does not exist yet, so this remains correctness-only
