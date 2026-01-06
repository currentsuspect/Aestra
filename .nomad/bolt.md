# Bolt Performance Journal

## 2025-05-20: Turbo Resampler & Audit
-   **Discovery:** The "Sinc64Turbo" resampler was just a standard Sinc64 implementation.
-   **Optimization:** Implemented AVX-512 kernels () and runtime dispatch ().
-   **Outcome:** Enabled future-proof performance on AVX-512 hardware. Minor overhead (~3ns) on AVX2 due to function pointer dispatch, deemed acceptable for flexibility.
-   **Bottleneck:** The "God Class"  (Source/Main.cpp) impedes testing and modular optimization. A refactor plan is in place.
-   **Critical Gap:** VST3 hosting is missing despite documentation claims. This is a P0 priority for the next sprint.
