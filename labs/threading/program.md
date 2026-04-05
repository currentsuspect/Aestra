# Threading Lab — Program Constitution

## Scope

This lab targets the threading primitives in `AestraCore`. It covers:

- `AestraCore/include/AestraThreading.h` (header-only: LockFreeRingBuffer, ThreadPool, Barrier, RealTimeThreadPool, AtomicFlag, AtomicCounter, SpinLock)
- `AestraCore/src/ThreadingTests.cpp`
- `AestraCore/include/AestraLog.h` (indirect dependency)
- `AestraCore/CMakeLists.txt` (target wiring only, if needed)

No other engine, UI, plugin-host, or platform code is in scope.

## Allowed Files

An autonomous agent may modify:

1. **Threading implementation**: `AestraCore/include/AestraThreading.h`
2. **Test harnesses**: `AestraCore/src/ThreadingTests.cpp`
3. **Lab infrastructure**: `labs/threading/`
4. **Build files**: Only if required for lab targets (`AestraCore/CMakeLists.txt`)

## Forbidden Behavior

- **DO NOT** modify unrelated core, audio, UI, or platform code.
- **DO NOT** weaken or remove test assertions.
- **DO NOT** introduce data races or remove thread-safety guarantees.
- **DO NOT** change the public API of LockFreeRingBuffer or ThreadPool without strong reason.
- **DO NOT** invent baseline files or fake golden outputs.
- **DO NOT** claim throughput wins until a deterministic benchmark lane exists.

## Invariants

1. **SPSC correctness**: LockFreeRingBuffer delivers exactly-once semantics for single-producer, single-consumer.
2. **Capacity**: `capacity()` returns `Size - 1` (one slot reserved for full/empty disambiguation).
3. **Size invariant**: `size() + available() == capacity()` at all times.
4. **ThreadPool completeness**: Every enqueued task executes exactly once.
5. **Barrier synchronization**: Barrier unblocks only when all expected signals arrive.
6. **Atomic correctness**: AtomicFlag, AtomicCounter, and SpinLock behave correctly under contention.
7. **Cache line alignment**: `alignas(64)` on ring buffer head/tail to prevent false sharing.

## Acceptance Logic

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `ThreadingTests` | All test cases pass (exit code 0) |
| `ThreadingBenchmark` | XRUN rate < 0.1%, deadline miss rate < 0.1% |
| Build | Compiles cleanly (no new warnings in threading code) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| Dirty worktree | Logged as maintenance context, never treated as an optimization acceptance |
| Median regression | > 20% regression on any benchmark median vs baseline is advisory only |

### Decision Status

- **`accept`**: All hard gates pass.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but tests/benchmarks could not run.

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, eval lanes
3. `LAB_BOOK.md` — session summaries
4. `findings/invariants.md` — things that must never break

## Session Budget

- One maintenance or optimization hypothesis at a time.
- Stop after the first coherent repair or after one accepted threading change.
- Do not widen from threading primitives into unrelated engine work.

## Session-End Reporting

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update durable findings only if a real lesson was observed
3. Update `LAB_BOOK.md` with the session outcome
4. End with a clean working tree

## Anti-Gaming Rules

- Do not count timing anecdotes as evidence; this lab has no trusted
  performance lane yet.
- Do not rely on a single successful flaky run for acceptance.
- If concurrency behavior is noisy or nondeterministic, mark the round
  `inconclusive` and investigate before accepting anything.

## Git Rules

- Start from a clean working tree.
- Dirty-tree maintenance is allowed only for lab repair and must be recorded.
- For rejected rounds: `git checkout -- .`
- For accepted rounds: commit with `threading-lab: accept round NN <hypothesis>`
- End with a clean working tree.

## Keep/Revert Discipline

- `accept`: commit stands, update baselines.
- `reject`: `git checkout -- .` to discard, log failure.
- Never accumulate rejected changes.
