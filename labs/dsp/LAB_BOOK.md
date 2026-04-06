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
| 001 | 2026-04-06 PM | 1 | 1 | 0 | Sinc8Turbo: 3.06x speedup (8.97 → 27.40 Mf/s). See `sessions/2026-04-06_session_001.md` |

## Current State

- **Branch**: `develop`
- **Last commit**: `dsp-lab: accept round 01 — Sinc8Turbo polyphase filter bank`
- **Sinc8Turbo**: 27.40 Mf/s (3.06x over original 8.97 Mf/s)
- **Sinc64Turbo**: 8.61 Mf/s (4.35x over original 1.98 Mf/s)
- **Next**: AVX2 dormant code paths for all interpolators, or Sinc16Turbo
