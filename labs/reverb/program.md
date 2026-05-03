# Reverb Lab — Program Constitution

## Scope

This lab targets the AestraVerb FDN reverb plugin. It covers:

### AestraVerb Core
- `AestraAudio/include/Plugin/AestraVerb.h` — Main reverb plugin (header-only)
- `AestraAudio/include/DSP/ReverbSIMD.h` — SIMD-optimized FDN routines
- `Tests/AestraAudio/ReverbBenchmark.cpp` — Performance & quality benchmark

### Allowed Files

1. **Reverb implementations** (files above)
2. **Tests and benchmarks** — existing or new
3. **Build files** — only reverb-related targets
4. **Lab infrastructure** — all files under `labs/reverb/`

### Forbidden Behavior

- **DO NOT** modify unrelated engine code, UI, plugin host, or platform layers.
- **DO NOT** change the public API of AestraVerb without strong reason.
- **DO NOT** remove or weaken existing test assertions.
- **DO NOT** add global AVX flags (`-mavx2`, `/arch:AVX2`). Use per-TU dispatch.
- **DO NOT** change parameter ranges or behavior — optimization + quality, not feature change.
- **DO NOT** compromise audio quality for speed.

## Invariants

1. **Numerical correctness**: SIMD and scalar paths must match within float epsilon.
2. **RT-safety**: `process()` must remain zero-allocation, lock-free, exception-free.
3. **Quality preservation**: Any interpolation change must demonstrably improve or preserve HF response.
4. **Cross-platform**: All code must compile on x86_64 (SSE/AVX2) and ARM (NEON).
5. **No regressions**: Existing plugin tests must pass.

## Acceptance Logic

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| Build | Compiles cleanly on x86_64 and ARM |
| `AestraReverbBenchmark` | Runs successfully, outputs quantifiable metrics |
| Auditory parity | Impulse response correlation > 0.999 vs baseline (same interpolation method) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| Throughput | No regression in samples/sec |
| CPU budget | < 15% per 256-sample callback @ 48kHz |
| HF preservation | Cubic Hermite shows measurable HF improvement over linear |

### Decision Status

- **`accept`**: All hard gates pass.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but some tests could not run.

## Reporting Format

All eval runs produce text output at `labs/reverb/results/run_<timestamp>.txt`.

## Session-End Reporting

At the end of every session:

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update findings files with durable knowledge
3. Update `LAB_BOOK.md` session summary table
4. Commit all lab-book changes

## Git Rules

- Start from a clean working tree.
- Keep commits surgical and grouped by concern.
- End the session with a clean working tree.
