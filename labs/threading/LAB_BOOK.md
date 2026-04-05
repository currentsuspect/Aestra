# Threading Lab Book

## Purpose

Persistent memory for the threading primitives lab. Tracks correctness of
LockFreeRingBuffer, ThreadPool, Barrier, and atomic utilities.

## Structure

```
labs/threading/
├── program.md              — Constitution
├── EVALS.md                — Eval documentation
├── LAB_BOOK.md             — This file
├── result_schema.json      — JSON schema
├── run_eval.sh             — Eval runner
├── results/                — Generated outputs
├── sessions/               — Per-session logs
└── findings/               — Durable knowledge
    ├── accepted_patterns.md
    ├── rejected_patterns.md
    ├── invariants.md
    └── bottlenecks.md
```

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, lanes
3. `LAB_BOOK.md` — this file
4. `findings/invariants.md` — things that must never break

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| M001 | 2026-04-05 | 0 | 0 | 0 | Maintenance pass: corrected real target name, repaired eval runner JSON bug, tightened anti-gaming guidance |
| M002 | 2026-04-05 | 1 | 0 | 0 | Validation run: `ThreadingTests` built on demand and passed; dirty-tree maintenance run so not treated as a baseline capture |
| M003 | 2026-04-05 | 20 | 20 | 0 | Automated loop session: 20 rounds covering ring buffer optimizations (modulo→bitmask, size arithmetic, unlikely hints, nodiscard), ThreadPool hardening (atomic stop, double-checked lock, conditional notify, reserve, nodiscard enqueue, hardware_concurrency fallback), Barrier fixes (spin-wait, exponential backoff, reset assert), SpinLock (atomic_flag + pause), RealTimeThreadPool (atomic m_taskCount, dispatch assert); all green |

## Current State

- **Branch**: `develop`
- **Last commit**: see `git log` for M003 round commits
- **Baselines**: Passing `results/summary.json` + `results/baseline_benchmark.json` from M003 round 20 + benchmark lane
- **Known issues**: Benchmarks run on non-realtime kernel — P99 tail latency reflects OS scheduler jitter, not primitive quality. XRUN rate threshold (0.1%) accounts for this.
