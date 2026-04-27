# Reverb Lab — Eval Documentation

## Build Commands

### Standard build

```bash
cmake -S . -B build \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel
```

### Target list

| Target | Purpose |
|--------|---------|
| `AestraReverbBenchmark` | Reverb SIMD + quality benchmark |

## Eval Lanes

### Lane 1: Build

**Gate**: HARD — must compile with zero new warnings in reverb code.

### Lane 2: Benchmark Run

```bash
./build/Tests/AestraReverbBenchmark --iterations 5 --duration 5
```

**Metrics captured**:
- Median processing time (us)
- Throughput (Msamples/sec)
- Real-time factor (x RT)
- CPU budget per 256-sample callback
- Output RMS (sanity check)

**Gate**: HARD — benchmark completes without crash.

### Lane 3: Quality Check

Verifies cubic Hermite interpolation preserves more high-frequency energy than linear.

**Gate**: ADVISORY — HF energy ratio should be measurable and positive.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| Build cleanliness | HARD | Reject + fix |
| Benchmark completion | HARD | Reject + fix |
| HF preservation | ADVISORY | Flag + investigate |

## Baseline Policy

- Baselines are captured from the **first successful eval run**.
- Baselines stored in `labs/reverb/results/baseline.json`.
- Updated only when a change is **accepted**.

## Noise Policy

- Benchmarks run with `--iterations 3` minimum.
- Median is primary metric.
- CV > 5% marks run as `inconclusive`.
