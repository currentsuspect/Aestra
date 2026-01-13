# Nomad Audit: Interim Report

**Date:** 2025-05-23
**Auditor:** Jules (Bolt)
**Status:** In Progress

## Executive Summary

Initial rapid audit has identified **2 critical real-time safety violations** in the core audio engine and **1 major audio quality defect** in the resampling algorithm. All three have been fixed in the current branch.

A "God Class" (`Source/Main.cpp`, 3200+ lines) has been confirmed as a major technical debt item preventing modular development. VST3 hosting remains unimplemented.

## Top 5 Critical Items & Quick Wins

### 1. Resampler Phase Discontinuity (Fixed)
*   **Severity:** P1 (Audio Quality)
*   **Issue:** `SampleRateConverter` was effectively stateless between blocks, resetting the phase interpolation origin to 0 for every `process()` call. This caused audible clicks/discontinuities for any non-integer resampling ratio (e.g., 44.1kHz -> 48kHz).
*   **Fix:** Refactored `SampleRateConverter::process` to correctly maintain `m_srcPosition` state across blocks.
*   **Verification:** Regression test `tests/ReproduceResamplerDiscontinuity.cpp` now passes with smooth continuity.

### 2. Real-time Safety: File I/O in Audio Thread (Fixed)
*   **Severity:** P0 (Real-time Safety)
*   **Issue:** `AudioEngine::loadMetronomeClicks` performs blocking `fopen`/`fread` and could be called during playback, causing audio dropouts.
*   **Fix:** Added atomic guard to prevent execution if `m_transportPlaying` is true.

### 3. Real-time Safety: Static Initialization Lock (Fixed)
*   **Severity:** P0 (Real-time Safety)
*   **Issue:** `AudioEngine::ensureTrackState` used a `static` local variable. C++11 guarantees thread-safe initialization of statics via a hidden mutex/lock. If this initialized on the audio thread, it posed a priority inversion risk.
*   **Fix:** Replaced static local with a member variable `m_dummyTrackState`.

### 4. God Class: `Source/Main.cpp` (Identified)
*   **Severity:** P2 (Maintainability)
*   **Issue:** `Source/Main.cpp` is 3220 lines long, handling application lifecycle, UI initialization, window management, and likely some audio logic glue.
*   **Plan:** Refactor into `NomadApp`, `WindowManager`, and `AudioController`.

### 5. Missing VST3 Support (Confirmed)
*   **Severity:** P1 (Feature Gap)
*   **Issue:** Documentation claims VST3 support, but implementation is missing/stubs only.
*   **Plan:** Create a `NomadPluginHost` module using the VST3 SDK.

## Next Steps

1.  **Refactor `Source/Main.cpp`**: Break down the monolith.
2.  **Prototype Adaptive Resampler**: Implement "Innovation" item where resampler quality degrades gracefully under load or signal content (e.g., lower taps for high-freq content where aliasing is less audible, or simple dynamic quality switching).
3.  **CI/Test Setup**: Integrate the new resampler test into a proper test runner.

## Quick Win Actions Taken
- [x] Fixed `SampleRateConverter.cpp` logic.
- [x] Patched `AudioEngine.h/cpp` for RT safety.
- [x] Verified fix with reproduction script.
