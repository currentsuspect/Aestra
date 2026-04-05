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
cmake --build build-autoresearch --target ThreadingBenchmark --parallel 2
```

## Eval Lanes

### Lane 1: Threading Tests (Correctness)

Runs 4 test cases:
- Ring buffer basic operations (push/pop/full/empty)
- Ring buffer thread safety (10,000 items producer/consumer)
- ThreadPool task execution (100 tasks, sum verification)
- Atomic utilities (flag, counter, spinlock with 2 threads)

**Gate**: HARD — exit code must be 0.

### Lane 2: Threading Benchmark (Performance)

Runs 5 microbenchmarks via `ThreadingBenchmark`:

| Benchmark | What it measures | Samples |
|-----------|-----------------|---------|
| Ring buffer latency | Single-threaded push+pop latency | 100K |
| Ring buffer contention | Push latency under producer/consumer pressure | 100K |
| ThreadPool dispatch | Enqueue → task start round-trip | 10K |
| Barrier sync | Time for 4 threads to synchronize | 10K |
| SpinLock contention | Lock+unlock latency under 4-thread contention | 50K |

Each benchmark reports median, P95, P99, min, max, mean, stddev, XRUN count,
and deadline miss count. XRUNs are operations exceeding 10× median latency.

**Gate**: HARD — XRUN rate < 0.1% and deadline miss rate < 0.1% across all benchmarks.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| ThreadingTests exit code | HARD | Reject + revert |
| ThreadingBenchmark XRUN/miss rate | HARD | Reject + investigate |
| Build warnings in threading code | HARD | Reject + investigate |

## Baseline Policy

- The benchmark captures a baseline JSON on the first clean pass.
- Future rounds compare against this baseline; a > 20% regression on median
  latency of any benchmark triggers a warning (advisory, not blocking).
- XRUN rate > 0.1% or deadline miss rate > 0.1% is a hard gate failure.

## Noise Policy

- Thread safety tests are deterministic on correctness assertions.
- Benchmarks on a non-realtime kernel will show OS scheduler jitter in the
  P99 and tail latency. This is expected and tolerated by the 0.1% XRUN/miss
  rate threshold.
- If benchmark rates exceed the gate, mark `inconclusive`, re-run once, and
  do not accept any optimization claims from that round.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% |
| Build warnings | 0 new warnings |
| XRUN rate (all benchmarks) | < 0.1% |
| Deadline miss rate (all benchmarks) | < 0.1% |
