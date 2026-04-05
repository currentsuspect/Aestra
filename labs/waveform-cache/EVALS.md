# Waveform Cache Lab — Eval Documentation

## Build Commands

```bash
cmake -S . -B build-autoresearch \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-autoresearch \
  --target AestraWaveformCacheTest AestraWaveformLockTest \
  --parallel 2
```

## Eval Lanes

### Lane 1: Waveform Cache Correctness

`AestraWaveformCacheTest` validates:

- mip-level layout and growth
- deterministic peak values on a fixed stereo fixture
- level selection behavior
- safe invalid-query behavior
- clear/reset semantics

**Gate**: HARD.

### Lane 2: Waveform Reader Latency Under Rebuild

`AestraWaveformLockTest` continuously rebuilds the cache while measuring
`getPeaksForRange()` latency.

**Gate**: ADVISORY.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| `AestraWaveformCacheTest` exit code | HARD | Reject + revert |
| Build warnings in waveform-cache code | HARD | Reject + investigate |
| `AestraWaveformLockTest` exit code | ADVISORY | Flag, rerun once if needed |
| Dirty worktree | ADVISORY | Record maintenance context |

## Baseline Policy

- Current trustworthy baseline is a green `AestraWaveformCacheTest`.
- `AestraWaveformLockTest` is advisory until we collect stable variance data.
- Do not invent numeric baseline files before the advisory lane is stable.

## Noise Policy

- The correctness lane is deterministic.
- The lock test is timing-sensitive and may vary by machine load.
- If the advisory lane fails once, rerun once before classifying it as a real
  regression.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| `AestraWaveformCacheTest` | exit code 0 |
| `AestraWaveformLockTest` | current in-test threshold |
