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
| 001 | 2026-04-06 PM | 0 | 0 | 0 | Lab created. No work yet. |

## Current State

- **Branch**: `develop`
- **Last commit**: None (lab just created)
- **DSP files in scope**: Filter.h/cpp, Oscillator.h/cpp, Interpolators.h, MixerBus.h/cpp, ClipResampler.h, Sinc*.h/cpp
- **Existing tests**: OscillatorTest (always-registered), MixerBusTest (always), FilterTest (experimental), SincBenchmark, ResamplerBenchmark
- **Existing SIMD dispatch**: SSE4.1, AVX2, AVX-512, NEON — runtime dispatch via `CPUDetection`
- **Known from resampler lab**: Code is at a local optimum for GCC 15 -O3 on x86_64 with no AVX2
