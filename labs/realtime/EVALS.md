# Realtime Lab — Eval Documentation

## Build Commands

### Dedicated autoresearch build

```bash
cmake -S . -B build-autoresearch \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-autoresearch --parallel
```

### Target list

| Target | Purpose |
|--------|---------|
| `MathTests` | AestraCore correctness baseline |
| `AestraSampleRateConverterTest` | Audio engine correctness |
| `OfflineRenderRegressionTest` | Offline export parity |
| `RealtimeSchedulingTest` | Verifies audio thread policy/priority (new) |

## Eval Lanes

### Lane 1: AestraCore Correctness (`MathTests`)

**Gate**: HARD — exit code must be 0.

### Lane 2: Hot-Path Correctness (`AestraSampleRateConverterTest`)

Runs the resampler test suite — verifies audio pipeline correctness.

**Gate**: HARD — exit code must be 0.

### Lane 3: Realtime Scheduling (`RealtimeSchedulingTest`)

Tests that the audio thread has proper scheduling:
- `sched_getscheduler()` returns SCHED_FIFO or SCHED_RR (not SCHED_OTHER)
- Priority is > 0
- `mlockall` is active (process-wide)
- Fallback to SCHED_OTHER works if RT not available

**Gate**: HARD — exit code must be 0.

### Lane 4: Offline Render Regression (`OfflineRenderRegressionTest`)

Validates audio output is unchanged after scheduling changes.

**Gate**: HARD — exit code must be 0.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| `MathTests` exit code | HARD | Reject + revert |
| `AestraSampleRateConverterTest` exit code | HARD | Reject + revert |
| `RealtimeSchedulingTest` exit code | HARD | Reject + revert |
| `OfflineRenderRegressionTest` exit code | HARD | Reject + revert |

## Baseline Policy

- Baselines are captured from the **first successful eval run**.
- Baselines are stored in `labs/realtime/results/baseline.json`.
- Baselines include: scheduling policy, priority level, mlockall status.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% |
| Scheduling policy | SCHED_FIFO or SCHED_RR |
| Scheduling priority | > 0 |
| mlockall | Active |
| Audio output parity | Bit-identical |
| Build warnings | 0 new warnings |

## Infrastructure Milestone

**Scheduling diagnosis is the first infrastructure milestone.**

Before any fix is attempted:

1. `RealtimeSchedulingTest` must exist and be able to report current state.
2. A baseline run must capture current (broken) state.
3. The test must be able to distinguish SCHED_OTHER from SCHED_FIFO/SCHED_RR.
