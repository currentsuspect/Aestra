# Resampler Lab — Program Constitution

## Scope

This lab is exclusively for the `SampleRateConverter` subsystem in `AestraAudio`.
It covers:

- `AestraAudio/include/DSP/SampleRateConverter.h`
- `AestraAudio/src/DSP/SampleRateConverter.cpp`
- `Tests/AestraAudio/SampleRateConverterTest.cpp`
- `Tests/Integration/ResamplerBenchmark.cpp`
- `Tests/AestraAudio/SincBenchmark.cpp`

No other engine, UI, plugin-host, or platform code is in scope.

## Allowed Files

An autonomous agent working in this lab may modify:

1. **Core resampler implementation**:
   - `AestraAudio/include/DSP/SampleRateConverter.h`
   - `AestraAudio/src/DSP/SampleRateConverter.cpp`

2. **Test and benchmark harnesses** (observability only, not correctness gates):
   - `Tests/AestraAudio/SampleRateConverterTest.cpp`
   - `Tests/Integration/ResamplerBenchmark.cpp`
   - `Tests/AestraAudio/SincBenchmark.cpp`

3. **Lab infrastructure** (this directory):
   - `labs/resampler/program.md`
   - `labs/resampler/EVALS.md`
   - `labs/resampler/run_eval.sh`
   - `labs/resampler/result_schema.json`
   - `labs/resampler/results/*` (generated outputs)

4. **Build files** (only if required to support observability or keep the build green):
   - `Tests/CMakeLists.txt` (resampler-related targets only)

## Forbidden Behavior

- **DO NOT** modify unrelated engine code, UI, plugin host, or platform layers.
- **DO NOT** change the public API of `SampleRateConverter` without a strong reason (backward compatibility matters).
- **DO NOT** remove or weaken existing test assertions to make benchmarks pass.
- **DO NOT** benchmark-game (e.g., `volatile` tricks, dead-code elimination that changes semantics, caching results).
- **DO NOT** add clever complexity, templates, or metaprogramming without a strong reason.
- **DO NOT** change the resampler algorithm to produce fake quality numbers.
- **DO NOT** invent baseline files or fake golden outputs.
- **DO NOT** touch `AestraAudio/External/` submodules.
- **DO NOT** modify `AESTRA_HAS_SSE` / `AESTRA_HAS_AVX` detection logic.

## Invariants

The following invariants MUST be preserved across all changes:

1. **Correctness**: The resampler must produce numerically correct output for all quality levels (Linear, Cubic, Sinc8, Sinc16, Sinc64).
2. **Determinism**: Given the same input, configuration, and SIMD state, the resampler must produce identical output across runs.
3. **RT-safety**: `process()` must remain zero-allocation, lock-free, and exception-free.
4. **Quality**: Round-trip fidelity (up then down) must not degrade below existing thresholds.
5. **SIMD equivalence**: SIMD and scalar modes must produce results within tight tolerance (RMS < 1e-6, max error < 1e-5).
6. **Passthrough**: When srcRate == dstRate, output must be byte-identical to input.
7. **Multi-channel**: All channel counts (1–8) must work correctly.

## Acceptance Logic

A change is **accepted** only if ALL of the following are true:

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `AestraSampleRateConverterTest` | All tests pass (exit code 0) |
| SIMD vs Scalar equivalence | RMS < 1e-6, max error < 1e-5 |
| Passthrough correctness | Byte-identical output |
| Round-trip fidelity | RMS < 0.15, max error < 0.20 |
| Offline render regression | Correlation > 0.995, RMS diff > -35 dB |
| Build | Compiles cleanly (no new warnings in resampler code) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| ResamplerBenchmark — no regression | Median time per case within 10% of baseline |
| SincBenchmark — no regression | MFrame/sec within 10% of baseline |
| Real-time factor | > 10x for Sinc16, > 1x for Sinc64 |

### Decision Status

Each eval run produces one of:

- **`accept`**: All hard gates pass, advisory gates within tolerance.
- **`reject`**: Any hard gate fails, or advisory regression > 10%.
- **`inconclusive`**: Build succeeded but some tests could not run (e.g., environment issues), or noise exceeds thresholds.

## Reporting Format

All eval runs must produce a JSON file at `labs/resampler/results/run_<timestamp>.json` conforming to `labs/resampler/result_schema.json`.

The eval runner (`labs/resampler/run_eval.sh`) emits a summary at `labs/resampler/results/summary.json`.

## Default Read Set

When starting a new session, read **only** these files:

1. `program.md` — this file (rules, scope, gates)
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — session summaries, finding pointers
4. `findings/invariants.md` — things that must never break

**Do NOT** load full session history by default. Only read session logs
(`sessions/*.md`) or findings files (`findings/*.md`) when the current
work makes them relevant. Use `LAB_BOOK.md` as the entry point for
selective retrieval.

## Session-End Reporting

At the end of every session:

1. Write a session log to `sessions/<date>_session_<NNN>.md` with:
   - Round-by-round summary (hypothesis, result, notes)
   - Key observations
   - Files modified
2. Update findings files with durable knowledge:
   - `findings/accepted_patterns.md` — new optimizations that worked
   - `findings/rejected_patterns.md` — new failures and why
   - `findings/invariants.md` — new invariants discovered
   - `findings/bottlenecks.md` — updated performance characteristics
3. Update `LAB_BOOK.md` session summary table.
4. Commit all lab-book changes as a single commit:
   `resampler-lab: update lab-book after session NNN`
5. Verify clean working tree.

## Lab-Book Update Discipline

- Write findings, not hype. Only record what was actually observed.
- Do not invent metrics beyond what the eval runner reports.
- Accepted patterns must include: where, what, why it works, round number.
- Rejected patterns must include: round, what was tried, why it failed, lesson.
- Invariants must be testable — if it can't be checked by a gate, it's not an invariant.
- Bottlenecks should reference the actual code location and current state.

## Research Order

1. **Benchmark observability** (this phase): Add `--json`, `--iterations N`, stable case IDs, median/mean/best/worst metrics to `ResamplerBenchmark` and `AestraSincBenchmark`. Get the eval runner working end-to-end.
2. **Baseline capture**: Run the eval suite against the current HEAD to establish baselines.
3. **Algorithm optimization**: Only after steps 1–2 are solid, begin experimenting with the resampler implementation.
4. **Iterate**: Each change runs through the eval harness. Accept or revert based on gates.

## Git Rules

- Start from a clean working tree.
- Before each round, confirm there are no leftover changes (`git status --short`).
- For a rejected or inconclusive round, discard all changes from that round using `git checkout -- .` so the tree returns exactly to the latest accepted commit.
- For an accepted round, commit immediately.
- Commit message format: `resampler-lab: accept round NN <short hypothesis>`
- Do not amend unrelated commits.
- Do not rewrite history during the session.
- End the session with a clean working tree.

## Keep/Revert Discipline

- Every change to the resampler implementation is tracked by git commit.
- After each eval run, the decision status determines the next action:
  - `accept`: commit stands, update baselines, move to next round.
  - `reject`: `git checkout -- .` to discard all uncommitted changes, log the failure in `results/`.
  - `inconclusive`: re-run once; if still inconclusive, `git checkout -- .` and investigate.
- Never accumulate rejected changes. Discard immediately.
- Baseline files are updated only on `accept`.
