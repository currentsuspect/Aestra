# Resampler Lab Book

## Purpose

This is the persistent memory system for the resampler lab. It exists so that
future sessions can pick up where the last one left off without re-reading
entire session logs or re-discovering known facts.

## Structure

```
labs/resampler/
├── program.md              — Constitution (scope, rules, gates)
├── EVALS.md                — Eval documentation (build, lanes, thresholds)
├── LAB_BOOK.md             — This file (entry point for lab memory)
├── result_schema.json      — JSON schema for eval results
├── run_eval.sh             — Eval runner script
├── results/                — Generated eval outputs (gitignored)
│   ├── baseline_resampler.json
│   ├── baseline_sinc.json
│   └── .gitignore
├── sessions/               — Per-session logs (one file per session)
│   └── 2026-04-05_session_001.md
└── findings/               — Durable knowledge, updated after each session
    ├── accepted_patterns.md     — Optimizations that worked, why
    ├── rejected_patterns.md     — Optimizations that failed, why
    ├── invariants.md            — Things that must never break
    └── bottlenecks.md           — Known performance characteristics
```

## Default Read Set

When starting a new session, read **only** these files by default:

1. `program.md` — rules, scope, acceptance logic
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — this file (session summary, finding pointers)
4. `findings/invariants.md` — things that must never break

**Do NOT** load full session history by default. Only read session logs or
findings files when the current work makes them relevant.

## Session Discipline

- Each session gets one log file in `sessions/`.
- At session end, update findings files with durable knowledge.
- Write findings, not hype. Only record what was actually observed.
- Use `accepted_patterns.md` and `rejected_patterns.md` to guide future rounds.
- Use `bottlenecks.md` to track what's still slow after accepted optimizations.

## Selective Retrieval

When a future agent needs context:

1. Check `LAB_BOOK.md` first for the session summary.
2. If a specific optimization is relevant, check `findings/accepted_patterns.md`.
3. If a failure mode is relevant, check `findings/rejected_patterns.md`.
4. Only read full session logs in `sessions/` when the above files don't
   contain enough detail.

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| 001 | 2026-04-05 | 16 | 15 | 1 | Hot-path optimization. See `sessions/2026-04-05_session_001.md` |

## Current State

- **Branch**: `develop`
- **Last commit**: `resampler-lab: accept round 16 add pragma GCC ivdep to scalar dot product loop`
- **Baselines**: `results/baseline_resampler.json`, `results/baseline_sinc.json`
- **Known issues**: High machine variance (CV > 5% on many cases). Baselines should be refreshed on a quieter machine.
