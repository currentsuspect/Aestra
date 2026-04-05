# Resampler Lab — Eval Documentation

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
- Enables experimental tests (SampleRateConverterTest is gated behind `AESTRA_ENABLE_EXPERIMENTAL_TESTS`).
- Skips UI targets (not needed for resampler evals).
- Uses a dedicated `build-autoresearch` directory to avoid polluting the developer's `build/`.

### Target list

The eval runner builds only these targets:

| Target | Purpose |
|--------|---------|
| `AestraSampleRateConverterTest` | Correctness unit tests |
| `ResamplerBenchmark` | Integration-level resampler performance |
| `AestraSincBenchmark` | Sinc interpolator micro-benchmarks |
| `OfflineRenderRegressionTest` | Offline export parity check |

## Eval Lanes

### Lane 1: Correctness (`AestraSampleRateConverterTest`)

Runs the full unit test suite for `SampleRateConverter`:
- Passthrough mode
- Upsampling (44100 → 48000)
- Downsampling (48000 → 44100)
- Round-trip quality (44100 → 48000 → 44100)
- All quality levels (Linear, Cubic, Sinc8, Sinc16, Sinc64)
- Multi-channel (6ch)
- Reset functionality
- Variable ratio (pitch shifting)
- SIMD vs Scalar equivalence
- Performance (real-time factor)

**Gate**: HARD — exit code must be 0.

### Lane 2: Resampler Benchmark (`ResamplerBenchmark`)

Benchmarks the full `SampleRateConverter::process()` pipeline across:
- Upsampling (44.1 → 48 kHz) at all quality levels
- Downsampling (48 → 44.1 kHz) at all quality levels
- Extreme upsampling (48 → 192 kHz) at Cubic and Sinc64

**Gate**: ADVISORY — median time per case tracked, > 10% regression is flagged.

### Lane 3: Sinc Benchmark (`AestraSincBenchmark`)

Micro-benchmarks individual interpolator implementations:
- Cubic (4-point)
- Sinc8 (8-point)
- Sinc64 (Original Opt)
- Sinc64 TURBO (Multi-SIMD)

**Gate**: ADVISORY — MFrame/sec tracked, > 10% regression is flagged.

### Lane 4: Offline Render Regression (`OfflineRenderRegressionTest`)

Validates that `AudioExporter` output matches direct engine render:
- Correlation > 0.995
- RMS difference > -35 dB

**Gate**: HARD — exit code must be 0.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| `AestraSampleRateConverterTest` exit code | HARD | Reject + revert |
| SIMD vs Scalar equivalence | HARD | Reject + revert |
| Offline render regression exit code | HARD | Reject + revert |
| ResamplerBenchmark regression > 10% | ADVISORY | Flag + investigate, do not auto-revert |
| SincBenchmark regression > 10% | ADVISORY | Flag + investigate, do not auto-revert |

## Baseline Policy

- Baselines are captured from the **first successful eval run** on a given machine.
- Baselines are stored in `labs/resampler/results/baseline.json`.
- Baselines are updated only when a change is **accepted**.
- Baselines include: median time per benchmark case, MFrame/sec per algorithm, test pass/fail counts.

## Noise Policy

- Benchmarks are run with `--iterations 3` by default (configurable).
- The median is used as the primary metric (robust to outliers).
- If the coefficient of variation (stddev / mean) exceeds 5% for any case, the run is marked `inconclusive`.
- The system should NOT be run alongside other CPU-intensive processes.
- CPU frequency scaling should be disabled for consistent results (not enforced, but noted).

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% (all tests must pass) |
| SIMD vs Scalar RMS | < 1e-6 |
| SIMD vs Scalar max error | < 1e-5 |
| Round-trip RMS error | < 0.15 |
| Round-trip max error | < 0.20 |
| Offline correlation | > 0.995 |
| Offline RMS diff | > -35 dB |
| Benchmark regression | < 10% (advisory) |
| Coefficient of variation | < 5% (inconclusive if exceeded) |

## Infrastructure Milestone

**Benchmark observability is the first infrastructure milestone.**

Before any algorithm optimization work begins, the following must be true:

1. `ResamplerBenchmark` supports `--json` and `--iterations N`.
2. `AestraSincBenchmark` supports `--json` and `--iterations N`.
3. `labs/resampler/run_eval.sh` can build and run all four eval lanes.
4. Results are captured in machine-readable JSON conforming to `result_schema.json`.
5. A baseline run has been captured and stored.

Do NOT begin algorithm optimization until this milestone is complete.
