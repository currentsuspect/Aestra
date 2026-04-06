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
| 001 | 2026-04-06 PM | 0 | 0 | 0 | Lab created. No work yet. |

## Current State

- **Branch**: `develop`
- **Last commit**: None (lab just created)
- **Known bug**: `RtAudioDriver::startStream()` calls `pthread_setschedparam(pthread_self(), SCHED_FIFO, ...)` from the UI thread, not the audio thread. The audio callback thread runs SCHED_OTHER (default CFS).
- **RtAudio internals**: ALSA backend does not honor `RTAUDIO_SCHEDULE_REALTIME`. Only PulseAudio/JACK do.
- **Working**: `mlockall(MCL_CURRENT | MCL_FUTURE)` is active (process-wide, prevents page faults).
