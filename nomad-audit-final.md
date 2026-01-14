# Nomad Codebase Audit & Improvement Report
**Date:** May 2025
**Author:** Jules (Agent Bolt)

## 1. Executive Summary

Nomad is a modern, high-performance C++ DAW with a solid foundation. The codebase generally adheres to real-time safety principles, but there are areas where architectural technical debt (God Classes) and minor safety violations (try_lock usage in RT threads) pose risks.

This audit focused on:
1.  **Automated Safety Checks**: Identified 8 potential real-time safety violations.
2.  **Architecture**: Refactored the `AudioEngine` "God Class" to extract `MetronomeEngine`, improving maintainability and isolation.
3.  **Innovation**: Prototyped `NeuralAmp`, a differentiable DSP component for future "AI-powered" effects.

## 2. Prioritized Issues List

### P0: Critical (Real-Time Safety / Crashes)
*   **Issue:** `TrackManager::processInput` and `SamplerPlugin::process` use `std::unique_lock` with `std::try_to_lock`.
    *   **Impact:** If the lock is held by the UI thread (e.g., loading samples or starting recording), the audio thread will skip processing (dropout/silence) instead of blocking. While better than a hard block, this causes audible glitches or data loss during recording.
    *   **Fix:** Use lock-free queues for passing data (samples/recording buffers) between UI and Audio threads. Replace `try_lock` with double-buffering or RCU (Read-Copy-Update) patterns.

### P1: Audio Quality & Correctness
*   **Issue:** `SampleRateConverter` uses integer/modulo logic in its inner loop (`getWindow`) which, while safe, introduces unnecessary overhead per sample.
    *   **Impact:** Higher CPU usage than necessary for resampling, potentially limiting voice count.
    *   **Fix:** Optimize `SampleHistory` iteration to pointer arithmetic in the hot loop.

### P2: Architecture & Refactoring
*   **Issue:** `AudioEngine` is a God Class handling Transport, Metronome, Graph Compilation, Metering, and Telemetry.
    *   **Status:** **PARTIALLY FIXED**. Metronome logic extracted to `MetronomeEngine` (PR included).
    *   **Next Steps:** Extract `TransportManager` and `MeteringEngine`.

### P3: Documentation & UX
*   **Issue:** Missing architectural diagrams for the "Hybrid Engine" transition.
*   **Issue:** `AGENTS.md` and `README.md` are good but technical deep-dives on the Audio Graph are missing.

## 3. Delivered Improvements

### A. Automatic Safety Audit (`scripts/audit_codebase.py`)
A custom script was created to scan for:
*   Memory allocations (`malloc`, `new`) in critical paths.
*   Blocking calls (`mutex`, `sleep`) in critical paths.
*   File I/O (`fopen`) in critical paths.

**Results:**
*   Confirmed `AudioEngine` hot path (`processBlock`) is free of `malloc`/`new`.
*   Flagged `std::mutex` usage in `TrackManager`.

### B. Architectural Refactor: `MetronomeEngine`
Extracted metronome logic from `AudioEngine` into a dedicated component.
*   **Files:** `NomadAudio/include/MetronomeEngine.h`, `NomadAudio/src/MetronomeEngine.cpp`
*   **Benefit:** Reduces `AudioEngine` complexity, encapsulates metronome state (sounds, timing), and simplifies testing.

### C. Innovation Prototype: `NeuralAmp`
A header-only prototype for a Neural Network inference engine designed for real-time audio.
*   **File:** `NomadAudio/include/NeuralAmp.h`
*   **Concept:** Allows loading pre-trained PyTorch models (exported weights) to perform non-linear distortion (Amp Simulation) using a small MLP (Multi-Layer Perceptron).
*   **Differentiation:** "AI-powered Analog Warmth" feature.

## 4. Roadmap Recommendations

1.  **Phase 1 (Immediate):** Fix P0 lock issues in `TrackManager` using lock-free ring buffers.
2.  **Phase 2 (1-2 Months):** Complete `AudioEngine` decomposition (Transport, Metering). Implement `NeuralAmp` training pipeline.
3.  **Phase 3 (3-6 Months):** Implement "Adaptive Polyphase Resampling" to dynamically reduce CPU usage on silence/low-frequency content.

## Appendix: Audit Log
*   **Scan Tool:** `scripts/audit_codebase.py`
*   **Target:** `NomadAudio`
*   **Findings:** See `audit_results.txt` (generated in workspace).
