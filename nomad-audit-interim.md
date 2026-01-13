# Nomad Audit & Improvement Report (Interim)

**Date:** 2025-05-24
**Auditor:** Jules (Bolt)

## Executive Summary

A comprehensive audit of the Nomad codebase has been performed, focusing on real-time safety, architectural modularity, and cross-platform build stability. Several critical real-time safety violations were identified and fixed, specifically regarding lazy initialization in DSP paths and race conditions in resource loading. The monolithic `NomadApp` class has been refactored into modular controllers. A new benchmark suite for the `Sinc64Turbo` resampler confirms its viability for high-performance real-time usage (approx. 290x real-time throughput at 48kHz).

## Critical Actions & Fixes

### 1. Real-Time Safety Violations Fixed
-   **Resource Loading Race Condition:** `AudioEngine::loadMetronomeClicks` performed blocking I/O on the main thread without adequate protection against concurrent audio thread access if playback started during load.
    -   **Fix:** Implemented `m_resourcesLoading` atomic guard to prevent the audio thread from accessing metronome buffers while they are being modified.
-   **DSP Lazy Initialization:** `Interpolators::Sinc32Turbo` and `Sinc64Turbo` utilized "Magic Static" initialization for their heavy coefficient tables. This caused the first audio callback to block for significant time (computing tables), causing a glitch.
    -   **Fix:** Added `Interpolators::precomputeTables()` and enforced initialization during `AudioEngine` startup (Main Thread), ensuring the audio path is wait-free.

### 2. Cross-Platform Compatibility
-   **VST3 String Conversion:** `VST3Host.cpp` relied on Windows-specific `WideCharToMultiByte` API, breaking Linux builds.
    -   **Fix:** Replaced with standard C++ `std::codecvt` based UTF-16 to UTF-8 conversion helper.
-   **Build System Robustness:** The build system failed on environments lacking ALSA or SDL2.
    -   **Fix:** Updated `CMakeLists.txt` in `NomadAudio` and `NomadPlat` to conditionally compile backend sources and dependencies.

### 3. Architecture Refactoring
-   **Monolithic Class Breakdown:** `Source/NomadApp.cpp` was identified as a "God Class" handling UI, Audio, and App logic.
    -   **Action:** Extracted responsibilities into:
        -   `NomadWindowManager`: Handles window creation, events, rendering, and UI component lifecycle.
        -   `NomadAudioController`: Handles `AudioDeviceManager` and `AudioEngine` lifecycle and stream callbacks.
    -   `NomadApp` remains as a lightweight coordinator.

## Performance Benchmark: Sinc64Turbo

A new benchmark suite (`Tests/ResamplerBenchmark.cpp`) was created to verify the performance of the polyphase resampler.

**Results (Linux, x86_64, AVX2 enabled):**
-   **Sinc64Turbo (64-tap, 2048 phases):** ~72 ns/sample (Upsampling 44.1 -> 48kHz).
    -   Throughput: ~13.9 MHz (Stereo samples/sec).
    -   Efficiency: ~290x real-time at 48kHz.
-   **Sinc16:** ~40 ns/sample.
-   **Cubic:** ~33 ns/sample.

**Conclusion:** `Sinc64Turbo` is highly optimized and perfectly suitable for real-time tracking and playback on modern CPUs.

## Remaining Risks & Recommendations

1.  **Plugin Hosting:** While `VST3Host` string issues were fixed, hosting third-party plugins in the main process remains the biggest stability risk. **Recommendation:** Investigate out-of-process hosting (sandboxing) for Beta 2.
2.  **Pattern Engine:** The interaction between `TrackManager` (UI) and `PatternPlaybackEngine` (RT) relies on atomic message passing. Deep stress testing of concurrent pattern editing during playback is recommended.
3.  **Documentation:** Doxygen documentation coverage is good but needs updates to reflect the new `NomadWindowManager` and `NomadAudioController` classes.

## Next Steps

1.  Merge the `NomadApp` refactor.
2.  Run the full test suite (including `VST3PluginTest`) in a CI environment with VST3 SDK availability.
3.  Proceed with "Innovation" tasks (e.g., Neural Oversampling prototype).
