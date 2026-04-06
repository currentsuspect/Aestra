# Realtime Lab Book

## Purpose

This is the persistent memory system for the realtime scheduling lab. It exists
so that future sessions can pick up where the last one left off.

## Structure

```
labs/realtime/
├── program.md              — Constitution (scope, rules, gates)
├── EVALS.md                — Eval documentation (build, lanes, thresholds)
├── LAB_BOOK.md             — This file (entry point for lab memory)
├── result_schema.json      — JSON schema for eval results
├── run_eval.sh             — Eval runner script
├── results/                — Generated eval outputs (gitignored)
│   └── baseline.json
├── sessions/               — Per-session logs (one file per session)
└── findings/               — Durable knowledge, updated after each session
    ├── accepted_patterns.md     — Fixes that worked, why
    ├── rejected_patterns.md     — Fixes that failed, why
    ├── invariants.md            — Things that must never break
    └── bottlenecks.md           — Known scheduling characteristics
```

## Default Read Set

1. `program.md` — rules, scope, acceptance logic
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — this file (session summary, finding pointers)
4. `findings/invariants.md` — things that must never break

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| 001 | 2026-04-06 PM | 1 | 1 | 0 | Diagnostic test created. Confirms SCHED_OTHER, RLIMIT_RTPRIO=0. |

## Current State

- **Branch**: `develop`
- **Last commit**: `realtime-lab: accept round 01 — diagnostic test for RT scheduling`
- **Confirmed bug**: Audio thread runs SCHED_OTHER (default CFS).
- **RLIMIT_RTPRIO=0**: No RT scheduling capability without `CAP_SYS_NICE`.
- **RLIMIT_MEMLOCK=8MB**: Memory locking is allowed.
- **Next**: Fix `startStream()` to set priority on actual callback thread, or set capabilities.
