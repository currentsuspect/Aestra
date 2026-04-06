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
- **Last commit**: `dsp-lab: Sinc16 TURBO variance fixed — 7.7% → 1.2% CV`
- **✅ CI baseline established** — all algorithms under 2% CV (trustworthy)

### Validated Tier System (GitHub Actions, AVX2, 10 iterations)

| Tier | Algorithm | Mf/s | CV | SNR | Role |
|------|-----------|------|-----|-----|------|
| — | Cubic (4-pt) | 123.97 | 1.6% | ~80dB | Low-end hardware fallback |
| **Draft** | **Sinc8 TURBO** | **81.37** | **0.3%** | **~100dB** | **Real-time playback** |
| **Quality** | **Sinc16 TURBO** | **45.93** | **1.2%** | **~120dB** | **Mixdown preview** |
| **Export** | **Sinc64 TURBO** | **31.31** | **0.1%** | **~144dB** | **Offline bounce** |

### Key Findings

- **Sinc64 TURBO: 5.24x speedup** (5.97 → 31.31 Mf/s) — the headline number
- **Sinc8 TURBO: 2.84x speedup** (28.63 → 81.37 Mf/s) — comfortably fast
- **Sinc16 TURBO: variance fixed** (7.7% → 1.2% CV by reducing table to 8KB)
- **Cubic stays** — 1.5x faster than Sinc8 TURBO, legitimate for low-end hardware
- **All TURBO tables capped at 8KB** — larger tables cause L1 cache pressure
