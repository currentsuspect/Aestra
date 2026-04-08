# Invariants

Things that must never break.

## Correctness Invariants

1. **Oscillator tests pass** — All tests in `AestraOscillatorTest` must pass.
2. **MixerBus tests pass** — All tests in `AestraMixerBusTest` must pass.
3. **Resampler tests pass** — All 22 tests in `AestraSampleRateConverterTest` must pass.
4. **SIMD/scalar equivalence** — RMS < 1e-6, max error < 1e-5 between SIMD and
   scalar outputs for the same input.
5. **Offline render parity** — Exporter output matches engine render:
   correlation > 0.995, RMS diff > -35 dB.

## RT-Safety Invariants

6. **Zero allocation** — `process()` must not allocate memory.
7. **Lock-free** — `process()` must not acquire locks.
8. **Exception-free** — `process()` must not throw.

## Build Invariants

9. **No global AVX flags** — Never add `-mavx2` or `/arch:AVX2` globally.
   Use per-TU `#pragma` or `__attribute__((target(...)))` for runtime dispatch.
10. **No new warnings** — Modified files must compile cleanly.
11. **Cross-platform** — All code must compile on x86_64 and ARM (NEON).

## API Invariants

12. **Public API unchanged** — `Filter`, `Oscillator`, `MixerBus`,
    `ClipResampler` public interfaces must remain stable.
