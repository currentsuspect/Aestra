# DSP Filter Lab — Eval Documentation

## Build Commands

```bash
cmake -S . -B build-autoresearch \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-autoresearch --target AestraFilterTest --parallel 2
```

## Eval Lanes

### Lane 1: FilterTest (Correctness)

Runs filter correctness tests:
- Low-pass frequency response
- High-pass frequency response
- Band-pass frequency response
- Resonance control
- Filter stability (impulse response)

The binary prompts for an optional interactive audio test after the automated
checks. Autonomous eval runs must answer `n` so the correctness lane remains
non-interactive.

**Gate**: HARD — exit code must be 0.

### Lane 2: Broad Performance Context (Advisory Only)

`Tests/AestraAudio/PerformanceStressTest.cpp` builds into
`AestraAudioPerformanceTest`, which exercises broader engine behavior. It is
not a trusted filter-only benchmark and must not decide keep/revert outcomes.

**Gate**: CONTEXT ONLY — optional raw capture, never an acceptance gate.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| AestraFilterTest exit code | HARD | Reject + revert |
| Build warnings in filter code | HARD | Reject + investigate |
| Dirty worktree | ADVISORY | Record maintenance context |

## Baseline Policy

- Current trustworthy baseline is a passing `AestraFilterTest` run plus repo
  metadata in `summary.json`.
- No numeric performance baseline is valid yet for this lab.
- Do not create `baseline.json` until a dedicated filter-specific benchmark
  exists with stable machine-readable output.

## Noise Policy

- The correctness lane is expected to be deterministic once the optional
  interactive prompt is answered `n`.
- Broad engine timing from `AestraAudioPerformanceTest` is too noisy and too
  indirect for lab acceptance.
- If the correctness lane behaves intermittently, mark the run `inconclusive`
  and investigate before accepting anything.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Test pass rate | 100% |
| Build warnings | 0 new warnings |
