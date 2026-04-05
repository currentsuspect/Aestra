# Waveform Cache Lab Book

## Purpose

Persistent memory for the waveform cache lab. Tracks correctness of mip
generation/query behavior and advisory reader latency under concurrent rebuilds.

## Structure

```
labs/waveform-cache/
├── program.md
├── EVALS.md
├── LAB_BOOK.md
├── result_schema.json
├── run_eval.sh
├── results/
├── sessions/
└── findings/
    ├── accepted_patterns.md
    ├── rejected_patterns.md
    ├── invariants.md
    └── bottlenecks.md
```

## Default Read Set

1. `program.md`
2. `EVALS.md`
3. `LAB_BOOK.md`
4. `findings/invariants.md`

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| M001 | 2026-04-05 | 0 | 0 | 0 | Lab created with deterministic correctness lane and advisory concurrency lane |
| M002 | 2026-04-05 | 1 | 1 | 0 | First end-to-end validation passed: correctness green, advisory lock-latency green |
| M003 | 2026-04-05 | 2 | 1 | 1 | Optimization pass: removed per-build info logging, rejected direct peak-access rewrite as too noisy to justify |

## Current State

- **Branch**: `develop`
- **Last commit**: N/A (new lab scaffold)
- **Baselines**: Latest green summary captured in `results/summary.json`; still on a dirty optimization tree, so not yet a canonical clean-tree baseline
- **Known issues**: Advisory latency lane is still variance-sensitive

## Selective Retrieval

1. Start with this file.
2. Read `findings/invariants.md` before changing behavior.
3. Read accepted/rejected patterns only if the current hypothesis overlaps.
4. Read full session logs only when summaries are insufficient.
