# Reverb Lab Book

## Purpose

Persistent memory for the Reverb SIMD optimization lab.

## Structure

```
labs/reverb/
├── program.md              — Constitution (scope, rules, gates)
├── EVALS.md                — Eval documentation (build, lanes, thresholds)
├── LAB_BOOK.md             — This file (entry point for lab memory)
├── results/                — Generated eval outputs (gitignored)
├── sessions/               — Per-session logs (one file per session)
└── findings/               — Durable knowledge, updated after each session
    ├── accepted_patterns.md     — Optimizations that worked, why
    ├── rejected_patterns.md     — Optimizations that failed, why
    ├── invariants.md            — Things that must never break
    └── bottlenecks.md           — Known performance characteristics
```

## Default Read Set

1. `program.md` — rules, scope, acceptance logic
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — this file (session summary, finding pointers)
4. `findings/invariants.md` — things that must never break

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| 001 | 2026-04-28 | 1 | 1 | 0 | SIMD core + cubic Hermite + benchmark. See sessions. |
| 002 | 2026-04-28 | 1 | 1 | 0 | CI workflow + benchmark CLI extensions. See sessions. |
| 003 | 2026-04-28 | 1 | 1 | 0 | Stage profiling + quality guardrails. See sessions. |
| 004 | 2026-04-28 | 1 | 3 | 1 | FDN Delay Read optimization (power-of-two + bitmask). See sessions. |
| 005 | 2026-04-28 | 1 | 2 | 2 | Early Reflection optimization + Plate timing investigation. See sessions. |

## Current State

- **Branch**: `develop`
- **Status**: Early Reflections optimized, Plate post-allpass identified as root cause of Plate slowness, Session 006 target selected

### Benchmark Results (SSE4.1, no AVX2, 5s @ 48kHz)

| Mode | Dispatch (S005) | Scalar (S005) | vs Scalar | Real-Time (Dispatch) |
|------|-----------------|---------------|-----------|---------------------|
| Room | 124.0 ms | 139.8 ms | **1.13x** | **40.33x** |
| Hall | 139.5 ms | 188.2 ms | **1.35x** | **35.84x** |
| Plate | 136.6 ms | — | — | **36.59x** |

*Note: Absolute numbers elevated by system load. Relative improvements are valid.*

### Quality Results

- **Cubic Hermite interpolation**: +1.12 pp HF energy >10kHz vs linear
- **Callback budget**: <2.5% even in heaviest mode (non-profile)
- **Projected AVX2 speedup**: 1.5-2.0x overall vs scalar (cubic would match/exceed original linear speed)

### Stage Profile Hotspots (Session 005, after optimization)

| Rank | Stage | % (Dispatch) | Session 004 (Before) |
|------|-------|-------------|---------------------|
| 1 | **FDN Delay Read** | **34.3%** | 34.4% → 34.3% |
| 2 | Early Reflections | **14.5%** | 15.0% → 14.5% |
| 3 | FDN Feedback/Matrix | 9.8% | 9.6% → 9.8% |
| 4 | Diffuser | 8.7% | 8.6% → 8.7% |
| 5 | LFO Normalize + Control | 8.1% | 8.3% → 8.1% |

**Early Reflections improvement**: -7.5% stage time (184.97 ms → 171.00 ms)
**Whole-reverb improvement**: ~1.8% dispatch

### Plate Timing Investigation — Root Cause Found

**Plate was 43% slower than Room in Session 004 (157.8 ms vs 110.0 ms).**

**Finding:** Early Reflections are NOT the cause. The `processPlatePostAllpass`
function is the culprit. It adds 2 allpass stages per sample, each using
`wrapIndex()` (integer `%` modulo) and another `%` for position increment.
The post-allpass buffer sizes (93 and 71 at 48kHz) are **not power-of-two**,
making the modulo operations expensive integer divisions.

**Session 006 target**: Plate Post-Allpass — power-of-two buffers + bitmask wrapping.

### Durable Patterns (Accepted)

1. **Power-of-two + bitmask wrapping** — Works for any ring buffer where size
   can be rounded up. Eliminates branches and modulo. Applied to FDN delay lines
   (S004) and early reflections (S005). Applicable to diffusers and post-allpass.
2. **Cache mask as member variable** — `m_delayLineMasks[]`, `m_earlyMask`.
   Avoids recomputing `size - 1` in hot paths.
3. **Remove ineffective prefetch** — `__builtin_prefetch` removed from FDN delay
   read (S004). Four cache-line-local samples don't benefit from explicit prefetch.

### Rejected Patterns (Documented)

1. **SIMD delay reads** — Gather/scatter not available on SSE4.1; would require
   interleaved storage (topology change). Rejected per prompt guidance.
2. **SIMD early reflection taps** — 12 taps with interleaved L/R accumulation
   don't vectorize cleanly on SSE4.1. Scalar bitmask is faster.
3. **Branchless `frac < 0` fix** — Compiler handles predictable branch well.
   No improvement measured.
4. **Hybrid cubic/linear interpolation** — Violates hard constraint.
