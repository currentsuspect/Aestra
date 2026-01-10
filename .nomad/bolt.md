# Performance Journal - Agent Bolt

## 2025-05-15: dB to Linear Optimization

### Hypothesis
`std::pow(10.0, db / 20.0)` is a heavy operation used in the inner mixing loop of `AudioEngine`. `std::exp` should be significantly faster because $10^x = e^{x \ln 10}$.

### Benchmark
Created `bench_db.cpp` performing 10,000,000 conversions.
*   Input: -100dB to +20dB sweep.

### Results
*   `std::pow`: 0.297031 s
*   `std::exp`: 0.0913932 s
*   **Speedup**: 3.25x
*   **Error**: 5.3e-15 (Negligible)

### Action
Applied optimization to `NomadAudio/src/AudioEngine.cpp` and `NomadCore/include/NomadMath.h`.

## 2025-05-15: Real-Time Safety Scan

### Findings
*   `loadMetronomeClicks` in `AudioEngine.cpp` performs file I/O (`fopen`).
    *   **Severity**: Critical if called during playback.
    *   **Mitigation**: Currently only called in `Main.cpp` initialization. Needs architectural guard.
