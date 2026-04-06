# DSP Lab Book

## Purpose

This is the persistent memory system for the DSP lab. It exists so that future
sessions can pick up where the last one left off.

## Structure

```
labs/dsp/
├── program.md              — Constitution (scope, rules, gates)
├── EVALS.md                — Eval documentation (build, lanes, thresholds)
├── LAB_BOOK.md             — This file (entry point for lab memory)
├── result_schema.json      — JSON schema for eval results
├── run_eval.sh             — Eval runner script
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
| 001 | 2026-04-06 PM | 2 | 2 | 0 | Sinc8Turbo (3.16x) + Sinc16Turbo (new, 29.35 Mf/s). See sessions. |

## Current State

- **Branch**: `develop`
- **Last commit**: `dsp-lab: write stress test results to JSON artifact`
- **✅ CI baseline established** — all algorithms under 2% CV, stress test passing

### Interpolator Benchmarks (GitHub Actions, AVX2, 10 iterations)

| Algorithm | Mf/s | CV | Relative | Speedup |
|-----------|------|-----|----------|---------|
| Cubic (4-pt) | 124.39 | 0.6% | 1.00x | — |
| Sinc8 original | 28.61 | 0.1% | 0.23x | 1.00x |
| **Sinc8 TURBO** | **81.40** | **0.3%** | **0.65x** | **2.84x** |
| **Sinc16 TURBO** | **46.04** | **1.1%** | **0.37x** | **new** |
| Sinc64 original | 5.98 | 1.2% | 0.05x | 1.00x |
| **Sinc64 TURBO** | **35.03** | **0.1%** | **0.28x** | **5.86x** |

### Stress Test — Simultaneous Clip Capacity (Sinc8 TURBO, ~100dB SNR)

| Clips | Median | P99 | Budget Usage | Status |
|-------|--------|-----|-------------|--------|
| **16** | **0.06ms** | **0.07ms** | **1.2%** | **✅** |
| 32 | 0.14ms | 0.15ms | 2.5% | ✅ |
| 64 | 0.31ms | 0.35ms | 5.7% | ✅ |
| 128 | 0.72ms | 0.74ms | 13.5% | ✅ |

**Budget**: 5.33ms per callback (256 samples @ 48kHz)
**Target**: <70% budget (3.73ms)
**Honest caveat**: Stress test measures pure interpolation throughput.
Real sessions have additional overhead — plugin processing, mixing,
disk I/O, UI. Real-world capacity will be lower.

### Key Findings

- **Sinc64 TURBO: 5.86x speedup** (5.98 → 35.03 Mf/s) — headline number
- **Sinc8 TURBO: 2.84x speedup** (28.61 → 81.40 Mf/s) — draft mode
- **16 clips: 1.2% of callback budget** — enormous headroom for target session
- **Breaking point**: ~660 clips on CI (extrapolated from 13.5% at 128 clips)
- **All TURBO tables capped at 8KB** — larger tables cause L1 cache pressure
- **Cubic stays** — 1.5x faster than Sinc8 TURBO, legitimate for low-end hardware

### CI Infrastructure

- `.github/workflows/dsp-benchmark.yml` triggers on DSP code changes
- Runs interpolator benchmark + stress test (16/32/64/128 clips)
- Results uploaded as JSON artifacts for comparison across runs
