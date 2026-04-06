# Memory Lab — Eval Documentation

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

This build:
- Uses `Release` for meaningful benchmark numbers.
- Skips UI targets (not needed for memory evals).
- Uses a dedicated `build-autoresearch` directory to avoid polluting the developer's `build/`.

### Target list

The eval runner builds these targets as needed:

| Target | Purpose |
|--------|---------|
| `MathTests` | AestraCore correctness baseline |
| `AestraSampleRateConverterTest` | Hot-path correctness (resampler uses allocators) |
| `OfflineRenderRegressionTest` | Offline export parity check |
| `MemoryAllocatorTest` | Allocator correctness (new) |
| `MemoryProfilingTest` | Profiler accuracy (new) |
| `MemoryBenchmark` | Allocation counts, arena performance (new) |

## Eval Lanes

### Lane 1: AestraCore Correctness (`MathTests`)

Runs the AestraCore test suite to ensure allocator changes don't break core functionality.

**Gate**: HARD — exit code must be 0.

### Lane 2: Hot-Path Correctness (`AestraSampleRateConverterTest`)

Runs the resampler test suite — the most allocation-sensitive existing code.

**Gate**: HARD — exit code must be 0.

### Lane 3: Allocator Correctness (`MemoryAllocatorTest`)

Tests the new arena allocator:
- Basic allocation/deallocation
- Boundary conditions
- Multi-threaded access patterns
- Memory alignment
- Reset behavior

**Gate**: HARD — exit code must be 0.

### Lane 4: Profiler Accuracy (`MemoryProfilingTest`)

Tests that memory profiling accurately reports allocation counts and sizes:
- AESTRA_MEMORY_ALLOC/FREE macros are wired correctly
- Profiler reports accurate counts after known allocation sequences
- Stats reset correctly

**Gate**: HARD — exit code must be 0.

### Lane 5: Allocation Count Benchmark (`MemoryBenchmark`)

Measures allocation counts during typical audio operations:
- Configure + process through SampleRateConverter
- AudioBufferManager allocation patterns
- GarbageCollector deferred deletion
- Total allocations per audio callback

**Gate**: ADVISORY — allocation count in hot path must be 0 after configure phase.

### Lane 6: Offline Render Regression (`OfflineRenderRegressionTest`)

Validates that audio output is unchanged after allocator modifications.

**Gate**: HARD — exit code must be 0.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| `MathTests` exit code | HARD | Reject + revert |
| `AestraSampleRateConverterTest` exit code | HARD | Reject + revert |
| `MemoryAllocatorTest` exit code | HARD | Reject + revert |
| `MemoryProfilingTest` exit code | HARD | Reject + revert |
| `OfflineRenderRegressionTest` exit code | HARD | Reject + revert |
| MemoryBenchmark allocation count > 0 in hot path | ADVISORY | Flag + investigate, do not auto-revert |

## Baseline Policy

- Baselines are captured from the **first successful eval run** on a given machine.
- Baselines are stored in `labs/memory/results/baseline.json`.
- Baselines are updated only when a change is **accepted**.
- Baselines include: allocation counts per benchmark case, profiler stats, test pass/fail.

## Noise Policy

- Benchmarks are run with `--iterations 3` by default (configurable).
- The median is used as the primary metric (robust to outliers).
- If the coefficient of variation (stddev / mean) exceeds 5% for any case, the run is marked `inconclusive`.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% (all tests must pass) |
| Hot-path allocations (after configure) | 0 |
| Audio output parity | Bit-identical where applicable |
| Offline correlation | > 0.995 |
| Offline RMS diff | > -35 dB |
| Build warnings | 0 new warnings in modified files |

## Infrastructure Milestone

**Memory profiling activation is the first infrastructure milestone.**

Before any allocator optimization work begins, the following must be true:

1. `AESTRA_MEMORY_ALLOC/FREE` macros are wired at all allocation sites in scope.
2. `MemoryProfilingTest` passes — profiler reports accurate counts.
3. `MemoryBenchmark` can report allocation counts per audio operation.
4. A baseline allocation count has been captured.

Do NOT begin allocator optimization until this milestone is complete.
