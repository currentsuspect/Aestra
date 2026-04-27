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

## Current State

- **Branch**: `develop`
- **Status**: FDN Delay Read optimized, Early Reflections now #1 hotspot, Session 005 target selected

### Benchmark Results (SSE4.1, no AVX2, 5s @ 48kHz)

| Mode | Dispatch (Session 004) | Scalar (Session 004) | vs Scalar | Real-Time (Dispatch) |
|------|------------------------|----------------------|-----------|---------------------|
| Room | 110.0 ms | 132.6 ms | **1.21x** | **45.45x** |
| Hall | 118.6 ms | 144.3 ms | **1.22x** | **42.17x** |
| Plate | 157.8 ms | 140.9 ms | 0.89x | **31.69x** |

### Quality Results

- **Cubic Hermite interpolation**: +1.12 pp HF energy >10kHz vs linear
- **Callback budget**: <2.5% even in heaviest Hall mode (non-profile)
- **Projected AVX2 speedup**: 1.5-2.0x overall vs scalar (cubic would match/exceed original linear speed)

### Stage Profile Hotspots (Session 004, after optimization)

| Rank | Stage | % (Dispatch) | % (Scalar) | Session 003 (Before) |
|------|-------|-------------|------------|---------------------|
| 1 | **FDN Delay Read** | **34.4%** | **32.5%** | 34.5% → 34.4% |
| 2 | Early Reflections | 13.8% | 13.2% | 14.7% → 13.8% |
| 3 | FDN Feedback/Matrix | 9.6% | 14.2% | 10.9% → 9.6% |
| 4 | Diffuser | 8.6% | 8.3% | 8.6% → 8.6% |
| 5 | LFO Normalize + Control | 8.3% | 8.0% | 8.2% → 8.3% |

**FDN Delay Read improvement**: -5.2% stage time (335.15 ms → 317.78 ms)
**Whole-reverb improvement**: -4.5% dispatch, -13.4% scalar

**Session 005 target**: Early Reflections — 12 scalar taps with conditional wrapping per tap.
Recommended: power-of-two buffer + bitmask wrapping, possibly partial SSE vectorization.

### Planned Optimizations

1. **AVX2 8-wide FDN** — Householder matrix, damping, feedback, output mixing
2. **AVX2 LFO updates** — 8 quadrature oscillators in parallel
3. **Cubic Hermite delay reads** — quality upgrade over linear interpolation
4. **SSE4.1 fallback** — 4-wide for older x86
5. **NEON fallback** — 4-wide for ARM

### Quality Hypothesis

Cubic Hermite interpolation in the 8 delay-line reads will preserve approximately
6-12 dB more high-frequency energy per feedback pass compared to linear.
Over 20+ passes in the FDN matrix, this compounds to a noticeably brighter,
more spacious reverb tail without adding artificial highs.
