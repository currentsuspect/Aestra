# DSP Filter Lab Book

## Purpose

Persistent memory for the DSP filter lab. Tracks correctness of biquad filters
(low-pass, high-pass, band-pass, resonance, oversampling, saturation).

## Structure

```
labs/dsp-filter/
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
| M001 | 2026-04-05 | 0 | 0 | 0 | Maintenance pass: hardened non-interactive correctness eval, clarified that no trusted filter-specific performance lane exists yet |
| M002 | 2026-04-05 | 1 | 0 | 1 | Validation run: `AestraFilterTest` executed non-interactively and failed hard gates on current HEAD |
| M003 | 2026-04-05 | 1 | 1 | 0 | Validation after repairing coefficient initialization/smoothing behavior and normalizing the test response metric; `AestraFilterTest` passed |
| M004 | 2026-04-05 | 3 | 1 | 2 | Optimization pass: rejected two hot-loop reshapes, kept settled-state smoothing short-circuit; hard gate green, broad perf context noisy |

## Current State

- **Branch**: `develop`
- **Last commit**: N/A (lab scaffold only)
- **Baselines**: Passing `results/summary.json` exists from session M003, but it was captured on a dirty repair tree and should not be treated as the canonical baseline
- **Known issues**: No dedicated filter benchmark lane yet; broad performance stress data remains too noisy for trusted keep/revert decisions
