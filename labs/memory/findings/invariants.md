# Invariants

Things that must never break. If any of these fail, the round is immediately
rejected and all changes are discarded.

## RT-Safety Invariants

1. **No allocations in audio callback** — `process()` must not call `new`,
   `malloc`, `std::make_shared`, `std::make_unique`, or any allocator.
2. **No locks in audio callback** — `process()` must not acquire mutexes.
3. **No blocking calls in audio callback** — `process()` must remain real-time safe.

## Correctness Invariants

4. **MathTests pass** — AestraCore correctness tests must all pass.
5. **Resampler tests pass** — All 22 unit tests must pass.
6. **SIMD/scalar equivalence** — RMS < 1e-6, max error < 1e-5.
7. **Offline render parity** — Exporter output matches engine render:
   correlation > 0.995, RMS diff > -35 dB.

## API Invariants

8. **AudioBufferManager public API unchanged** — `allocate()`, `clear()`,
   constructor signature must remain the same.
9. **GarbageCollector public API unchanged** — `release<T>()`, `collect()`
   must remain the same.
10. **SamplePool public API unchanged** — `acquire()`, `release()`,
    `setMemoryBudget()` must remain the same.

## Build Invariants

11. **No new warnings** — Modified files must compile cleanly.
12. **No global AVX flags** — Never add `-mavx2` or `/arch:AVX2` globally.
