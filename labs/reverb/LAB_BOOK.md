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
| 007 | 2026-04-28 | 1 | 4 | 1 | Diffuser wrapping optimization (power-of-two + bitmask). See sessions. |

## Current State

- **Branch**: `develop`
- **Status**: All major ring buffers now power-of-two + bitmask. Predelay and LFO control identified as next targets. Session 008 target selected.

### Benchmark Results (SSE4.1, no AVX2, 5s @ 48kHz)

| Mode | Dispatch (S007) | Scalar (S007) | vs Scalar | Real-Time (Dispatch) |
|------|-----------------|---------------|-----------|---------------------|
| Room | **103.7 ms** | 107.8 ms | **1.04x** | **48.20x** |
| Hall | **100.3 ms** | 110.8 ms | **1.10x** | **49.85x** |
| Plate | **93.5 ms** | — | — | **53.47x** |

**Biggest single-session improvement in the lab.** Diffuser was a larger bottleneck than the profiler suggested.

### Quality Results

- **Cubic Hermite interpolation**: +1.12 pp HF energy >10kHz vs linear
- **Callback budget**: <2.1% even in heaviest mode (non-profile)
- **Projected AVX2 speedup**: 1.5-2.0x overall vs scalar (cubic would match/exceed original linear speed)

### Stage Profile Hotspots (Session 007, Room Mode)

| Rank | Stage | % |
|------|-------|---|
| 1 | **FDN Delay Read** | **30.1%** |
| 2 | Output/Mix | 14.1% |
| 3 | Early Reflections | 10.7% |
| 4 | FDN Feedback/Matrix | 8.6% |
| 5 | **Diffuser** | **8.1%** |
| 6 | LFO Normalize + Control | 7.8% |
| 7 | Input/Predelay | 6.5% |
| 8 | Plate Post-Allpass | 5.0% |
| 9 | Modulation/LFO | 4.6% |
| 10 | Parameter Smoothing | 4.5% |

**Diffuser improvement**: ~121 ms → ~89 ms stage time (-26%).
**Whole-reverb improvement**: -18.7% (Room), -23.6% (Hall), -26.0% (Plate).

### Durable Patterns (Accepted)

1. **Power-of-two + bitmask wrapping** — Works for any ring buffer where capacity
   can be rounded up. Eliminates branches and modulo. Applied to FDN delay lines
   (S004), early reflections (S005), post-allpass (S006), and diffusers (S007).
2. **Separate delay length from buffer capacity** — Allows power-of-two capacity
   while preserving exact delay timing. Used in post-allpass (S006) and diffusers (S007).
3. **Cache mask as member variable** — `m_delayLineMasks[]`, `m_earlyMask`,
   `m_platePostMasks[]`, `m_diffuserMasks[]`. Avoids recomputing `size - 1` in hot paths.
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
6. **SIMD gather for diffuser reads** — Only 4 stages, different buffers, different
   offsets. Not worth complexity; scalar bitmask is nearly as fast as SSE.

**Session 008 target**: Input/Predelay — `if (predelayRead < 0) predelayRead += size` branch
and `wrapIndex` modulo in init. Predelay buffer (~24,000 samples at 48kHz) can be
rounded to 32,768 for bitmask wrapping.
