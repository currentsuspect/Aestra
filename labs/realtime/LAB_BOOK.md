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
| 002 | 2026-04-06 Early PM | 1 | 1 | 0 | Fixed RT scheduling: moved from startStream() to callback via pthread_once. |

## Current State

- **Branch**: `develop`
- **Last commit**: `realtime-lab: accept round 02 — fix RT scheduling to run on actual callback thread`
- **Fixed**: `pthread_setschedparam` now runs on actual RtAudio callback thread via `pthread_once`
- **Still limited**: Without `CAP_SYS_NICE`, audio thread falls back to SCHED_OTHER (graceful)
- **mlockall**: Now called from callback (process-wide, prevents page faults)
- **Next**: No more rounds needed on this machine (no CAP_SYS_NICE). The fix is correct; it just needs proper capabilities at deployment time.
