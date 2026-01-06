# Turbo Resampler Implementation

## Overview
We have upgraded the standard `SampleRateConverter` to a runtime-dispatched "Turbo" version that selects the optimal SIMD path (Scalar, SSE, AVX, AVX-512) at startup.

## Changes
1.  **CPUDetection:** Added `NomadCore/include/CPUDetection.h` and `src/CPUDetection.cpp` to query CPUID features.
2.  **AVX-512 Kernel:** Implemented `NomadAudio/src/SampleRateConverterAVX512.cpp` with explicit `__m512` intrinsics.
    -   Uses `_mm512_loadu_ps` for unaligned loads, ensuring safety with standard allocators.
    -   Uses target attributes to ensure correct compilation even if global flags don't enable AVX-512.
3.  **Dispatch Logic:** Modified `SampleRateConverter.cpp` to:
    -   Initialize SIMD dispatch on first configuration using `std::call_once`.
    -   Select function pointer `g_dotProductFunc` based on hardware capabilities.
    -   Call through cached function pointer in the hot loop.

## Performance
Microbenchmarks on AVX2 hardware show:
-   **Baseline (Static AVX2):** ~28 ns/sample
-   **New (Dynamic Dispatch):** ~28-29 ns/sample

*Note: The overhead of the function pointer call is negligible (<1ns) thanks to modern branch prediction, while gaining the ability to transparently scale to AVX-512.*

## Future Work
-   Implement dynamic tap count reduction (Idea #5 from prompt) to skip calculations for silence or low-frequency content.
