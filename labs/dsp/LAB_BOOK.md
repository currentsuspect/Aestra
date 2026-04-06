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
- **Last commit**: `dsp-lab: set up CI benchmark workflow to solve variance problem`
- **⚠️ Local benchmark variance: 20-35% CV** — absolute numbers unreliable
- **✅ CI benchmark workflow created** — `.github/workflows/dsp-benchmark.yml`
  triggers on DSP changes or manual dispatch
- **Next**: Merge to develop → triggers first CI benchmark run → establishes
  trustworthy baseline with isolated environment
