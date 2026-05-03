# Reverb Lab — Accepted Patterns

Patterns that have been validated and accepted into the codebase.

## SIMD-Accelerated FDN Matrix (8-wide AVX2)

**Status**: Accepted
**Speedup**: ~2.5-4x on FDN inner loop (theoretical)
**Why**: 8 FDN lines map perfectly to 256-bit AVX2 registers. Horizontal operations (Householder mean, dot products) use `_mm256_hadd_ps` which is efficient for small fixed-size vectors.

## Cubic Hermite Delay-Line Interpolation

**Status**: Accepted
**Quality gain**: Measurable HF preservation
**Why**: Linear interpolation acts as a gentle lowpass (-3dB at Nyquist/2). In a feedback delay network, this compounds with every pass. Cubic Hermite preserves flat response to ~0.8 * Nyquist, keeping the reverb tail airy and natural.

## Vectorized LFO Quadrature Oscillators

**Status**: Accepted
**Speedup**: ~4-8x on LFO update path
**Why**: 8 oscillators updated simultaneously with FMA operations. The recurrence `sin' = sin*cosInc + cos*sinInc` is exactly 2 FMAs per oscillator, vectorized perfectly.

## Runtime CPU Dispatch

**Status**: Accepted
**Why**: AVX2 path used if available, SSE4.1 fallback for older CPUs, NEON for ARM, scalar as last resort. No global compiler flags needed.
