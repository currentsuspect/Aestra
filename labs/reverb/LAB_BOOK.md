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
| 006 | 2026-04-28 | 1 | 4 | 1 | Plate post-allpass optimization + profile stage + benchmark bug fix. See sessions. |

## Current State

- **Branch**: `develop`
- **Status**: Plate post-allpass optimized, Plate now on par with Room, diffuser and predelay identified as next targets, Session 007 target selected

### Benchmark Results (SSE4.1, no AVX2, 5s @ 48kHz)

| Mode | Dispatch (S006) | Scalar (S006) | vs Scalar | Real-Time (Dispatch) |
|------|-----------------|---------------|-----------|---------------------|
| Room | 127.5 ms | 147.5 ms | **1.16x** | **39.21x** |
| Hall | 131.3 ms | 183.4 ms | **1.40x** | **38.07x** |
| Plate | 126.4 ms | — | — | **39.56x** |

*Note: Absolute numbers affected by system load on 2-core machine. Relative trends are valid.*

### Quality Results

- **Cubic Hermite interpolation**: +1.12 pp HF energy >10kHz vs linear
- **Callback budget**: <2.5% even in heaviest mode (non-profile)
- **Projected AVX2 speedup**: 1.5-2.0x overall vs scalar (cubic would match/exceed original linear speed)

### Stage Profile Hotspots (Session 006, Plate Mode)

| Rank | Stage | % (Plate) |
|------|-------|----------|
| 1 | **FDN Delay Read** | **28.8%** |
| 2 | Output/Mix | 13.4% |
| 3 | Early Reflections | 12.6% |
| 4 | Diffuser | 9.2% |
| 5 | FDN Feedback/Matrix | 8.7% |
| 6 | LFO Normalize + Control | 7.2% |
| 7 | Input/Predelay | 7.0% |
| 8 | **Plate Post-Allpass** | **4.9%** |
| 9 | Modulation/LFO | 4.5% |
| 10 | Parameter Smoothing | 3.7% |

**Plate Post-Allpass improvement**: Now a separate 4.9% stage with zero modulo overhead.
**Plate-vs-Room gap**: Closed. Plate (126.4 ms) ≈ Room (127.5 ms).

### Plate Timing — Root Cause Resolved

**Plate was ~10% slower than Room in Session 005 (136.6 ms vs 124.0 ms).**

**Cause:** `processPlatePostAllpass` used non-power-of-two buffers (101 and 77 at 48kHz)
with expensive integer `%` modulo operations (`wrapIndex` + `(p+1) % size`).

**Fix:** Power-of-two buffer capacity (128) with separate delay lengths (101, 77)
and bitmask wrapping (`& mask`). Mathematically equivalent output.

**Result:** Plate is now on par with Room. The expensive modulo was the dominant cost.

### Durable Patterns (Accepted)

1. **Power-of-two + bitmask wrapping** — Works for any ring buffer where capacity
   can be rounded up. Eliminates branches and modulo. Applied to FDN delay lines
   (S004), early reflections (S005), and post-allpass (S006). Applicable to diffusers
   and predelay.
2. **Separate delay length from buffer capacity** — Allows power-of-two capacity
   while preserving exact delay timing. Used in post-allpass (S006).
3. **Cache mask as member variable** — `m_delayLineMasks[]`, `m_earlyMask`,
   `m_platePostMasks[]`. Avoids recomputing `size - 1` in hot paths.
4. **Remove ineffective prefetch** — `__builtin_prefetch` removed from FDN delay
   read (S004). Four cache-line-local samples don't benefit from explicit prefetch.

### Rejected Patterns (Documented)

1. **SIMD delay reads** — Gather/scatter not available on SSE4.1; would require
   interleaved storage (topology change). Rejected per prompt guidance.
2. **SIMD early reflection taps** — 12 taps with interleaved L/R accumulation
   don't vectorize cleanly on SSE4.1. Scalar bitmask is faster.
3. **Branchless `frac < 0` fix** — Compiler handles predictable branch well.
   No improvement measured.
4. **Hybrid cubic/linear interpolation** — Violates hard constraint.
5. **Changing delay to match buffer capacity** — Would alter sound. Delay length
   must be preserved independently of capacity.

**Session 007 target**: Diffuser wrapping — 4 stages with conditional `if (p >= len) p = 0;`
and non-power-of-two buffers. Same power-of-two + bitmask pattern applies.
