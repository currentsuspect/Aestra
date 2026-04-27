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

## Current State

- **Branch**: `develop`
- **Status**: Stage profiling active, hotspots identified, Session 004 target selected

### Benchmark Results (SSE4.1, no AVX2)

| Mode | Optimized | Scalar | Original | vs Scalar | vs Original |
|------|-----------|--------|----------|-----------|-------------|
| Room | 107.3 ms | 139.8 ms | 63.1 ms | **1.30x** | 0.59x (quality) |
| Hall | 122.2 ms | 147.0 ms | 61.0 ms | **1.20x** | 0.50x (quality) |
| Plate | 118.9 ms | 143.8 ms | 59.6 ms | **1.21x** | 0.50x (quality) |

### Quality Results

- **Cubic Hermite interpolation**: +1.12 pp HF energy >10kHz vs linear
- **Callback budget**: <2.5% even in heaviest Hall mode
- **Projected AVX2 speedup**: 1.5-2.0x overall vs scalar (cubic would match/exceed original linear speed)

### Stage Profile Hotspots (from Session 003)

| Rank | Stage | % (Dispatch) | % (Scalar) |
|------|-------|-------------|------------|
| 1 | **FDN Delay Read** | **34.5%** | **32.7%** |
| 2 | Early Reflections | 14.7% | 14.9% |
| 3 | FDN Feedback/Matrix | 10.9% | 14.2% |
| 4 | Diffuser | 8.6% | 9.1% |
| 5 | LFO Normalize + Control | 8.2% | 7.8% |

**Session 004 target**: FDN Delay Read — 34.5% of total CPU time.

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
