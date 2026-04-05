# Threading Lab — Eval Documentation

## Build Commands

```bash
cmake -S . -B build-autoresearch \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-autoresearch --target ThreadingTests --parallel 2
```

## Eval Lanes

### Lane 1: Threading Tests

Runs 4 test cases:
- Ring buffer basic operations (push/pop/full/empty)
- Ring buffer thread safety (10,000 items producer/consumer)
- ThreadPool task execution (100 tasks, sum verification)
- Atomic utilities (flag, counter, spinlock with 2 threads)

**Gate**: HARD — exit code must be 0.

## Advisory Status

- No trusted throughput benchmark exists yet for this lab.
- Do not claim performance regressions or wins until a dedicated, repeatable
  benchmark harness is added.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| ThreadingTests exit code | HARD | Reject + revert |
| Build warnings | HARD | Reject + investigate |

## Baseline Policy

- Current baseline is a passing `ThreadingTests` run plus its recorded repo
  metadata in `summary.json`.
- No numeric performance baseline is valid yet.
- Do not create `baseline.json` until the lab has a real benchmark lane.

## Noise Policy

- Thread safety tests are inherently non-deterministic in timing.
- The correctness assertions (sum verification, item count) are deterministic.
- If a test fails intermittently, mark `inconclusive`, re-run once, and do not
  accept any optimization claims from that round.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% |
| Build warnings | 0 new warnings |
