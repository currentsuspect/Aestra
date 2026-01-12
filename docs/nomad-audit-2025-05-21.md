# Nomad Codebase Audit & Roadmap (2025-05-21)

## Executive Summary

Nomad v1 Beta is a solid foundation with a high-performance audio core (`NomadAudio`), but it currently suffers from **critical real-time safety violations** in auxiliary components (specifically `PreviewEngine`) and architectural debt in the application layer (`Main.cpp`).

This audit identified:
1.  **P0 Critical Safety Violation**: `PreviewEngine` used `std::shared_ptr` atomic operations (locking) inside the audio callback. **Status: Fixed** in this session by refactoring to a lock-free raw-pointer swap with deferred garbage collection.
2.  **P1 Architecture Debt**: `Source/Main.cpp` is a 3200+ line "God Class" handling everything from UI layout to audio init. This impedes testability and modularity.
3.  **P2 Performance**: `SampleRateConverter` uses `Sinc64Turbo` with manual loop unrolling and AVX/SSE intrinsics.
4.  **Innovation Opportunities**: The project is well-positioned for "Neural Oversampling" and "Adaptive Resampling" to differentiate itself.

## Prioritized Issues List

| Severity | Component | Issue | Status | Effort |
| :--- | :--- | :--- | :--- | :--- |
| **P0** | `PreviewEngine` | **Real-time Lock Violation**: `std::atomic_load` of `shared_ptr` in `process()`. | **FIXED** | 2h |
| **P1** | `Source/Main.cpp` | **God Class**: 3220 lines. Needs split into `NomadAppController`, `NomadWindowManager`. | Open | 20h |
| **P2** | `AudioEngine` | **Initialization Safety**: `loadMetronomeClicks` uses `fopen` (blocking IO). Must ensure this is never called during playback. | Open | 4h |
| **P2** | `TrackManager` | **Locking**: `std::lock_guard` used extensively. Needs audit to ensure no locks in `processBlock` path. | Open | 8h |
| **P3** | `Docs` | **Missing Documentation**: Public headers lack Doxygen comments. | Open | 6h |

## Deep Dive: Real-Time Safety Fix

The `PreviewEngine::process` method was identified as unsafe due to implicit locking in `std::atomic_load` of `shared_ptr`.

**Resolution**: Replaced with a raw atomic pointer swap pattern (`std::atomic<PreviewVoice*>`).
*   **Audio Thread**: Reads raw pointer (wait-free).
*   **Worker Thread**: Handles "Zombie Voices". When a voice is replaced, it is moved to a zombie list with a timestamp. The worker thread waits 2 seconds before destroying it, ensuring the audio thread has finished any processing involving that pointer.

## DSP Innovation: Adaptive Resampler (Prototype)

An `AdaptiveResampler` class was implemented in `NomadAudio` to dynamically switch quality modes based on signal content.
*   **Logic**: Uses Zero-Crossing Rate (ZCR) and High-Frequency Energy flux to detect transients.
*   **Switching**: Cross-fades between `Sinc64` (High Quality) and `Sinc16` (Low CPU) to avoid audio glitches.
*   **Constraint**: Requires double the CPU during the brief cross-fade transition, but saves significant CPU during sustained low-frequency passages (pads, bass).

## Next Steps

1.  **Refactor `Main.cpp`**: Create `NomadAppController.h` and move initialization logic there.
2.  **Verify `TrackManager` Locks**: Instrument the code to assert if locks are taken during the audio callback.
3.  **Prototype Neural Oversampler**: Start a new module `NomadML`.
