# Math Lab — Eval Documentation

## Build Commands

```bash
cmake -S . -B build-autoresearch \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-autoresearch --target MathTests --parallel 2
cmake --build build-autoresearch --target MathBenchmark --parallel 2
```

## Eval Lanes

### Lane 1: Math Tests (Correctness)

Runs test cases covering:
- Vector2: construction, arithmetic, dot, length, normalization
- Vector3: construction, arithmetic, dot, cross product, length, normalization
- Vector4: construction, arithmetic, dot, length
- Matrix4x4: identity, translation, scale, rotation, multiplication, vector transform
- DSP math: `lerp`, `clamp`, `smoothstep`, `map`, `dbToGain`, `gainToDb`
- Edge cases: div-by-zero guards, NaN/Inf protection, epsilon equality
- constexpr: compile-time vector construction and operations

**Gate**: HARD — exit code must be 0.

### Lane 2: Math Benchmark (Performance)

Runs 6 microbenchmarks via `MathBenchmark`:

| Benchmark | What it measures | Samples |
|-----------|-----------------|---------|
| Vector3 arithmetic | add/sub/mul/div latency | 1M |
| Vector3 normalization | normalized() + length() latency | 500K |
| Matrix4x4 multiplication | 4×4 matrix × matrix latency | 100K |
| Matrix-vector transform | Matrix4×4 × Vector4 latency | 500K |
| DSP math (scalar) | lerp/clamp/smoothstep/dbToGain latency | 1M |
| Batch transform | 1000 vectors through one matrix | 10K |

Each benchmark reports median, P95, P99, min, max, mean, stddev, XRUN count,
and deadline miss count. XRUNs are operations exceeding 10× median latency.

**Gate**: HARD — XRUN rate < 0.1% and deadline miss rate < 0.1% across all benchmarks.

## Baseline Policy

- The benchmark captures a baseline JSON on the first clean pass.
- Future rounds compare against this baseline; a > 20% regression on median
  latency of any benchmark triggers an advisory warning (not blocking).
- XRUN rate > 0.1% or deadline miss rate > 0.1% is a hard gate failure.

## Noise Policy

- Math correctness tests are fully deterministic (no concurrency, no I/O).
- Benchmarks are microbenchmarks with tight loops; the 0.1% XRUN threshold
  accounts for OS scheduler jitter on non-realtime machines.
- If a test fails or benchmark exceeds the gate, mark `inconclusive`,
  re-run once, and do not accept any optimization claims from that round.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% |
| Build warnings | 0 new warnings in math code |
| XRUN rate (all benchmarks) | < 0.1% |
| Deadline miss rate (all benchmarks) | < 0.1% |
