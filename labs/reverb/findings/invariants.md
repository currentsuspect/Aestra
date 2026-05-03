# Reverb Lab — Invariants

These must never be violated, regardless of performance gains.

## Audio Quality

1. **No audible artifacts**: SIMD paths must not introduce zipper noise, clicks, or drift.
2. **Interpolation quality**: Delay-line reads must use at least linear interpolation; cubic is preferred.
3. **LFO stability**: Quadrature oscillators must not explode (magnitude must remain bounded).

## Real-Time Safety

1. **Zero allocation in process()**: No `new`, `malloc`, `std::vector::resize`, etc.
2. **No locks**: No mutexes, semaphores, or blocking operations.
3. **Exception-free**: No exceptions thrown or caught in the audio callback.

## Numerical Correctness

1. **SIMD == Scalar**: For the same input and parameters, SIMD and scalar outputs must match within `1e-5` absolute tolerance.
2. **Deterministic**: Same input must produce same output every time (no unseeded random in process()).

## Cross-Platform

1. **x86_64 + ARM**: Code must compile and run on both architectures.
2. **Runtime dispatch**: SIMD paths must be selected at runtime, not compile time.
