# DSP Lab — Program Constitution

## Scope

This lab targets the core DSP algorithms in `AestraAudio`. It covers:

### Filter Pipeline
- `AestraAudio/include/DSP/Filter.h` / `.cpp` — biquad filter, oversampling, saturation
- `AestraAudio/include/DSP/FilterSIMD.h` — SIMD-accelerated filter ops
- `Tests/AestraAudio/FilterTest.cpp`

### Oscillator
- `AestraAudio/include/DSP/Oscillator.h` / `.cpp` — waveform generation, BLEP
- `Tests/AestraAudio/OscillatorTest.cpp`

### MixerBus
- `AestraAudio/include/DSP/MixerBus.h` / `.cpp` — mixing, panning, metering
- `AestraAudio/include/DSP/MixerSIMD.h` — SIMD mixer operations
- `Tests/AestraAudio/MixerBusTest.cpp`

### Interpolators (shared with clip resampling)
- `AestraAudio/include/DSP/Interpolators.h` — Cubic, Sinc8/16/32/64
- `AestraAudio/include/DSP/SincSSE41.h` / `SincAVX2.h` / `SincAVX512.h` / `SincNEON.h`
- `Tests/AestraAudio/SincBenchmark.cpp`

### Clip Resampler
- `AestraAudio/include/DSP/ClipResampler.h` — clip playback resampling

### Allowed Files

An autonomous agent may modify:

1. **DSP implementations** (in-scope files above)
2. **Tests and benchmarks** — existing or new
3. **Build files** — only DSP-related targets
4. **Lab infrastructure** — all files under `labs/dsp/`

### Forbidden Behavior

- **DO NOT** modify unrelated engine code, UI, plugin host, or platform layers.
- **DO NOT** change the public API of DSP classes without strong reason.
- **DO NOT** remove or weaken existing test assertions.
- **DO NOT** add global AVX flags (`-mavx2`, `/arch:AVX2`). Use per-TU dispatch.
- **DO NOT** touch `AestraAudio/External/` submodules.
- **DO NOT** change DSP algorithm behavior — this is optimization, not feature change.
  Audio output must be bit-identical or within documented tolerance.

## Invariants

The following invariants MUST be preserved:

1. **Numerical correctness**: DSP algorithms must produce correct output within
   documented tolerance (typically float epsilon, or < -120dB SNR for resampling).
2. **RT-safety**: `process()` must remain zero-allocation, lock-free, exception-free.
3. **SIMD equivalence**: SIMD and scalar modes must match within tight tolerance.
4. **Cross-platform**: All code must compile on x86_64 and ARM (NEON).
5. **No regressions in existing tests**: All existing DSP tests must pass.

## Acceptance Logic

A change is **accepted** only if ALL of the following are true:

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `AestraOscillatorTest` | All tests pass (exit code 0) |
| `AestraMixerBusTest` | All tests pass (exit code 0) |
| `AestraSampleRateConverterTest` | All tests pass (exit code 0) |
| `OfflineRenderRegressionTest` | Exit code 0, correlation > 0.995 |
| Build | Compiles cleanly (no new warnings in DSP code) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| `AestraFilterTest` | Exit code 0 (experimental-gated) |
| `SincBenchmark` | MFrame/sec within 10% of baseline |
| `ResamplerBenchmark` | No >10% regression |

### Decision Status

- **`accept`**: All hard gates pass.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but some tests could not run.

## Reporting Format

All eval runs produce a JSON file at `labs/dsp/results/run_<timestamp>.json`
conforming to `labs/dsp/result_schema.json`.

The eval runner (`labs/dsp/run_eval.sh`) emits a summary at
`labs/dsp/results/summary.json`.

## Default Read Set

When starting a new session, read **only** these files:

1. `program.md` — this file (rules, scope, gates)
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — session summaries, finding pointers
4. `findings/invariants.md` — things that must never break

## Session-End Reporting

At the end of every session:

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update findings files with durable knowledge
3. Update `LAB_BOOK.md` session summary table
4. Commit all lab-book changes as a single commit:
   `dsp-lab: update lab-book after session NNN`
5. Verify clean working tree.

## Git Rules

- Start from a clean working tree.
- Before each round, confirm there are no leftover changes.
- For rejected rounds: `git checkout -- .` to discard.
- For accepted rounds: commit immediately.
- Commit message format: `dsp-lab: accept round NN <short hypothesis>`
- Do not amend unrelated commits.
- End the session with a clean working tree.

## Keep/Revert Discipline

- Every change is tracked by git commit.
- After each eval run:
  - `accept`: commit stands, update baselines, move to next round.
  - `reject`: `git checkout -- .` to discard all uncommitted changes.
  - `inconclusive`: re-run once; if still inconclusive, `git checkout -- .`
- Never accumulate rejected changes. Discard immediately.
