# Math Lab Book

## Purpose

Persistent memory for the math module lab. Tracks correctness and performance
of Vector2/3/4, Matrix4x4, and DSP math utilities in AestraCore.

## Structure

```
labs/math/
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
| M001 | 2026-04-05 | 0 | 0 | 0 | Initial scaffold: lab infrastructure created, known issues documented |
| M002 | 2026-04-05 | 1 | 1 | 0 | Single loop: added distance/reflect/project/lerp/clamp free functions for Vector2/3/4; clamp shadowing gotcha fixed with explicit ternary; tests + benchmark green |

## Current State

- **Branch**: `develop`
- **Last commit**: see `git log` for M002 round commits
- **Baselines**: Passing `results/summary.json` + `results/baseline_benchmark.json` from initial scaffold, confirmed by M002
- **Known issues**:
  - `Matrix4x4` constructor not `constexpr` in C++17 (loop body in constructor — blocked until C++20)
  - All vector/matrix types lack SIMD alignment (`alignas(16)`)
  - `dbToGain` uses `std::exp` (~50-100 cycles) — could use polynomial approximation for hot paths
  - ~290 lines of math are effectively dead code (only `dbToGain` consumed by other modules)
