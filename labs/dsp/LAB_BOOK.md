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
- **Last commit**: `dsp-lab: accept round 02 — Sinc16Turbo polyphase filter bank`
- **⚠️ Benchmark machine has extreme variance (CV 20-35%)** — absolute Mf/s
  numbers are unreliable. See `findings/benchmark_reliability.md`.
- **Ranking is valid** (Sinc8 TURBO > Sinc16 TURBO > Sinc64 TURBO, always).
  Absolute numbers swing wildly: Sinc8 TURBO ranges 27-44 Mf/s across 5
  consecutive runs on the same binary.

| Algorithm | Range (5 runs) | Consistent Ranking |
|-----------|---------------|-------------------|
| Cubic (4-pt) | 43–65 Mf/s | Always fastest |
| Sinc8 TURBO | 27–44 Mf/s | Always 2nd |
| Sinc16 TURBO | 10–26 Mf/s | Always 3rd |
| Sinc64 TURBO | 8–13 Mf/s | Always 4th |

- **Cannot reliably measure**: headroom, simultaneous clip capacity, or
  absolute speedup. Need CPU isolation, frequency scaling control, and 20+
  iterations for trustworthy numbers.
- **What's still valid**: structural correctness (zero math in hot path),
  relative ordering, quality tiers (~100dB/~120dB/~144dB).
- **Next**: The DSP lab is reaching its practical limit on this noisy machine.
  Further optimization work needs a controlled benchmark environment.
