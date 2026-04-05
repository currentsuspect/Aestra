# Invariants

Things that must never break. If any of these fail, the round is immediately
rejected and all changes are discarded.

## Correctness Invariants

1. **All 22 unit tests pass** — `AestraSampleRateConverterTest` exit code 0.
2. **SIMD/scalar equivalence** — RMS < 1e-6, max error < 1e-5 between SIMD
   and scalar outputs for the same input.
3. **Passthrough correctness** — When srcRate == dstRate, output is
   byte-identical to input.
4. **Round-trip fidelity** — Up then down resampling: RMS < 0.15, max < 0.20.
5. **Offline render parity** — Exporter output matches engine render:
   correlation > 0.995, RMS diff > -35 dB.
6. **Latency reporting** — `getLatency()` returns non-zero for upsampling
   (was broken before round 02, fixed by using `getFilterBank()` dispatch).

## RT-Safety Invariants

7. **Zero allocation** — `process()` must not allocate memory.
8. **Lock-free** — `process()` must not acquire locks.
9. **Exception-free** — `process()` is `noexcept` and must not throw.

## Quality Invariants

10. **Multi-channel** — All channel counts (1-8) work correctly.
11. **All quality levels** — Linear, Cubic, Sinc8, Sinc16, Sinc64 all produce
    correct output.
12. **Reset consistency** — Output before and after `reset()` is identical.

## Build Invariants

13. **No global AVX flags** — Never add `-mavx2` or `/arch:AVX2` globally.
    Use per-TU `#pragma` or `__attribute__((target(...)))` for runtime dispatch.
14. **Compiles cleanly** — No new warnings in resampler code.

## Behavioral Invariants

15. **`writePos` changes during `process()`** — It is modified by `push()` on
    every input frame. Do NOT hoist it to a local variable.
16. **`srcPosition` and `nextOutputSrcPos` are loop-mutated** — They can be
    hoisted to locals because they are only written by the loop itself, not
    by any called function.
