# Invariants

Things that must never break.

## Cross-Platform Invariants

1. **Linux-only changes**: All modifications must be `#ifdef __linux__` guarded.
   Windows and macOS behavior must remain unchanged.
2. **No external dependency changes**: `AestraAudio/External/rtaudio/` must not be modified.
3. **Public API unchanged**: `AudioDriver` and `RtAudioDriver` interfaces must remain stable.

## Correctness Invariants

4. **MathTests pass**: AestraCore correctness tests must all pass.
5. **Resampler tests pass**: All 22 unit tests must pass.
6. **SIMD/scalar equivalence**: RMS < 1e-6, max error < 1e-5.
7. **Offline render parity**: Exporter output matches engine render.

## RT-Safety Invariants

8. **No allocations in callback**: Must remain lock-free, allocation-free.
9. **Graceful degradation**: If SCHED_FIFO fails (no CAP_SYS_NICE), fall back
   to SCHED_OTHER without crashing.
10. **mlockall preserved**: Process-wide memory locking must continue to work.

## Build Invariants

11. **No new warnings**: Modified files must compile cleanly.
12. **No global AVX flags**: Never add `-mavx2` or `/arch:AVX2` globally.
