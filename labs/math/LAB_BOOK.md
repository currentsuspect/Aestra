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

## Current State

- **Branch**: `develop`
- **Last commit**: math lab scaffold (not yet committed)
- **Baselines**: None yet — first session will capture baseline
- **Known issues**:
  - `map()` divides by zero when `inMax == inMin` → NaN
  - `gainToDb(0.0f)` returns `-inf`; `gainToDb(-1.0f)` returns NaN
  - No `operator==` on any vector or matrix type
  - No `constexpr` on constructors or Matrix4x4 factories
  - No `Matrix4x4::inverse()`, `transpose()`, `determinant()`
  - No static constants: `Vector3::zero()`, `unitX()`, etc.
  - MathTests.cpp uses relative include path `../include/AestraMath.h`
  - ~290 of 308 lines of AestraMath.h are dead code (only `dbToGain` is consumed)
  - No `Matrix4x4 * Vector3` homogeneous multiply
