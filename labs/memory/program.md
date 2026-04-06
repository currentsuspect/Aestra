# Memory Lab — Program Constitution

## Scope

This lab targets memory allocation patterns across `AestraCore` and `AestraAudio`.
The goal is to eliminate runtime allocations in audio-thread-adjacent code,
introduce a proper arena allocator for RT-safe buffer management, and activate
the dormant memory profiling infrastructure.

### In-Scope Files

**Core allocator implementation:**
- `AestraCore/include/AestraMemory.h` (new — arena allocator, memory pool)
- `AestraCore/src/AestraMemory.cpp` (new — implementations)
- `AestraCore/include/AestraUnifiedProfiler.h` (memory profiling hooks)
- `AestraCore/src/AestraUnifiedProfiler.cpp` (memory stats)

**Hot-path allocation sites:**
- `AestraAudio/src/DSP/AudioProcessor.cpp` (AudioBufferManager)
- `AestraAudio/src/DSP/Filter.cpp` (OversampledBuffer::resize)
- `AestraAudio/include/GarbageCollector.h` (deferred deletion)
- `AestraAudio/include/IO/SamplePool.h` (sample cache)
- `AestraAudio/src/DSP/SampleRateConverter.cpp` (filter bank allocation)

**Test and benchmark harnesses:**
- `Tests/AestraCore/MemoryAllocatorTest.cpp` (new)
- `Tests/AestraCore/MemoryProfilingTest.cpp` (new)
- `Tests/Integration/MemoryBenchmark.cpp` (new)
- `Tests/CMakeLists.txt` (allocator-related targets only)

**Lab infrastructure:**
- `labs/memory/program.md` — this file
- `labs/memory/EVALS.md`
- `labs/memory/LAB_BOOK.md`
- `labs/memory/run_eval.sh`
- `labs/memory/result_schema.json`
- `labs/memory/findings/*`
- `labs/memory/results/*`
- `labs/memory/sessions/*`

## Allowed Files

An autonomous agent working in this lab may modify:

1. **Allocator implementation** (new files):
   - `AestraCore/include/AestraMemory.h`
   - `AestraCore/src/AestraMemory.cpp`

2. **Hot-path allocation sites** (refactor existing):
   - `AestraAudio/src/DSP/AudioProcessor.cpp`
   - `AestraAudio/src/DSP/Filter.cpp`
   - `AestraAudio/include/GarbageCollector.h`
   - `AestraAudio/src/DSP/SampleRateConverter.cpp`
   - `AestraCore/include/AestraUnifiedProfiler.h`
   - `AestraCore/src/AestraUnifiedProfiler.cpp`

3. **Tests and benchmarks** (new files):
   - `Tests/AestraCore/MemoryAllocatorTest.cpp`
   - `Tests/AestraCore/MemoryProfilingTest.cpp`
   - `Tests/Integration/MemoryBenchmark.cpp`

4. **Build files** (only allocator-related targets):
   - `Tests/CMakeLists.txt`
   - `AestraCore/CMakeLists.txt` (only if needed for new allocator source)

5. **Lab infrastructure** (this directory):
   - All files under `labs/memory/`

## Forbidden Behavior

- **DO NOT** modify unrelated engine code, UI, plugin host, or platform layers.
- **DO NOT** change the public API of existing audio classes without strong reason.
- **DO NOT** break RT-safety (no blocking calls, no allocations in audio callback).
- **DO NOT** introduce thread-safety bugs (lock-free must remain lock-free).
- **DO NOT** remove or weaken existing test assertions.
- **DO NOT** add clever metaprogramming or template-heavy code without strong reason.
- **DO NOT** touch `AestraAudio/External/` submodules.
- **DO NOT** change existing behavior — this is a performance optimization lab, not a feature lab.

## Invariants

The following invariants MUST be preserved across all changes:

1. **RT-safety**: No allocations, no locks, no blocking calls in the audio callback.
2. **Thread safety**: All shared state must remain correctly synchronized.
3. **Correctness**: All existing tests must pass. Audio output must be bit-identical where applicable.
4. **No regressions**: Performance must not regress in non-allocator paths.
5. **Backward compatibility**: Public APIs of `AudioBufferManager`, `GarbageCollector`, `SamplePool` must not change.

## Acceptance Logic

A change is **accepted** only if ALL of the following are true:

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `AestraCore` tests | All tests pass (exit code 0) |
| `AestraSampleRateConverterTest` | All tests pass (exit code 0) |
| `OfflineRenderRegressionTest` | All tests pass (exit code 0) |
| Audio output parity | Bit-identical output where applicable |
| Build | Compiles cleanly (no new warnings) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| `MemoryBenchmark` — no regression | Allocation count in hot path = 0 after configure |
| `MemoryBenchmark` — improvement | Fewer total allocations than baseline |
| Memory profiler accuracy | Profiler reports correct allocation counts |

### Decision Status

Each eval run produces one of:

- **`accept`**: All hard gates pass, advisory gates within tolerance.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but some tests could not run.

## Reporting Format

All eval runs must produce a JSON file at `labs/memory/results/run_<timestamp>.json` conforming to `labs/memory/result_schema.json`.

The eval runner (`labs/memory/run_eval.sh`) emits a summary at `labs/memory/results/summary.json`.

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
   `memory-lab: update lab-book after session NNN`
5. Verify clean working tree.

## Lab-Book Update Discipline

- Write findings, not hype. Only record what was actually observed.
- Do not invent metrics beyond what the eval runner reports.
- Accepted patterns must include: where, what, why it works, round number.
- Rejected patterns must include: round, what was tried, why it failed, lesson.
- Invariants must be testable — if it can't be checked by a gate, it's not an invariant.
- Bottlenecks should reference the actual code location and current state.

## Research Order

1. **Activate memory profiling**: Wire up `AESTRA_MEMORY_ALLOC/FREE` macros at real allocation sites. Verify profiler reports accurate counts.
2. **Implement arena allocator**: Build `AestraMemory.h` with a simple bump allocator + memory pool for RT-safe audio buffer allocation.
3. **Replace hot-path allocations**: Migrate `AudioBufferManager`, `Filter::resize`, and other hot-adjacent sites to use the arena allocator.
4. **Improve GarbageCollector**: Replace mutex+vector with lock-free SPSC deferred deletion.
5. **Benchmark and iterate**: Each change runs through the eval harness. Accept or revert based on gates.

## Git Rules

- Start from a clean working tree.
- Before each round, confirm there are no leftover changes (`git status --short`).
- For a rejected or inconclusive round, discard all changes from that round using `git checkout -- .` so the tree returns exactly to the latest accepted commit.
- For an accepted round, commit immediately.
- Commit message format: `memory-lab: accept round NN <short hypothesis>`
- Do not amend unrelated commits.
- Do not rewrite history during the session.
- End the session with a clean working tree.

## Keep/Revert Discipline

- Every change to allocator implementation is tracked by git commit.
- After each eval run, the decision status determines the next action:
  - `accept`: commit stands, update baselines, move to next round.
  - `reject`: `git checkout -- .` to discard all uncommitted changes, log the failure in `results/`.
  - `inconclusive`: re-run once; if still inconclusive, `git checkout -- .` and investigate.
- Never accumulate rejected changes. Discard immediately.
- Baseline files are updated only on `accept`.
