# DSP Lab — Eval Documentation

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
| `AestraOscillatorTest` | Oscillator correctness |
| `AestraMixerBusTest` | MixerBus correctness |
| `AestraSampleRateConverterTest` | Resampler correctness |
| `AestraFilterTest` | Filter correctness (experimental) |
| `OfflineRenderRegressionTest` | Offline export parity |
| `SincBenchmark` | Sinc interpolator micro-benchmark |
| `ResamplerBenchmark` | Full resampler benchmark |

## Eval Lanes

### Lane 1: Core Correctness (`MathTests`)

**Gate**: HARD — exit code must be 0.

### Lane 2: Oscillator (`AestraOscillatorTest`)

Runs oscillator unit tests:
- Waveform generation (sine, square, saw, triangle, noise)
- BLEP anti-aliasing
- Frequency accuracy
- Amplitude accuracy

**Gate**: HARD — exit code must be 0.

### Lane 3: MixerBus (`AestraMixerBusTest`)

Runs MixerBus unit tests:
- Channel mixing
- Panning
- Gain staging
- Metering

**Gate**: HARD — exit code must be 0.

### Lane 4: Resampler (`AestraSampleRateConverterTest`)

Runs the resampler test suite — the most DSP-sensitive existing test.

**Gate**: HARD — exit code must be 0.

### Lane 5: Filter (`AestraFilterTest`)

Runs the filter test suite:
- Filter type responses
- Oversampling behavior
- Saturation modes
- Parameter smoothing

**Gate**: HARD — exit code must be 0 (when experimental tests enabled).

### Lane 6: Offline Render Regression (`OfflineRenderRegressionTest`)

Validates audio output is unchanged after DSP changes:
- Correlation > 0.995
- RMS diff > -35 dB

**Gate**: HARD — exit code must be 0.

### Lane 7: Sinc Benchmark (`AestraSincBenchmark`)

Micro-benchmarks individual interpolator implementations:
- Cubic (4-point)
- Sinc8 (8-point)
- Sinc64 TURBO (Multi-SIMD)

**Gate**: ADVISORY — MFrame/sec tracked, >10% regression is flagged.

### Lane 8: Resampler Benchmark (`ResamplerBenchmark`)

Benchmarks the full `SampleRateConverter::process()` pipeline.

**Gate**: ADVISORY — median time per case tracked, >10% regression is flagged.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| `MathTests` exit code | HARD | Reject + revert |
| `AestraOscillatorTest` exit code | HARD | Reject + revert |
| `AestraMixerBusTest` exit code | HARD | Reject + revert |
| `AestraSampleRateConverterTest` exit code | HARD | Reject + revert |
| `AestraFilterTest` exit code | HARD | Reject + revert |
| `OfflineRenderRegressionTest` exit code | HARD | Reject + revert |
| `SincBenchmark` regression > 10% | ADVISORY | Flag + investigate |
| `ResamplerBenchmark` regression > 10% | ADVISORY | Flag + investigate |

## Baseline Policy

- Baselines are captured from the **first successful eval run**.
- Baselines are stored in `labs/dsp/results/baseline.json`.
- Baselines are updated only when a change is **accepted**.

## Noise Policy

- Benchmarks are run with `--iterations 3` by default.
- The median is used as the primary metric (robust to outliers).
- If the coefficient of variation (stddev / mean) exceeds 5%, the run is marked `inconclusive`.
