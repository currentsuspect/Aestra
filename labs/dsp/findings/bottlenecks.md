# Bottlenecks

Known performance characteristics of the DSP subsystem before any optimization work.

## SIMD Dispatch Architecture

All SIMD code uses **runtime dispatch** via `CPUDetection.h`. The dispatch hierarchy:

```
CPUDetection::hasAVX512F() → SincAVX512
CPUDetection::hasAVX2()    → SincAVX2
SSE4.1 (always on x86_64) → SincSSE41
ARM NEON                   → SincNEON
```

**No AVX2 on this machine** — confirmed by resampler lab. The SIMD path is SSE4.1 only.

## Known Bottlenecks

### 1. Filter Oversampling (Filter.cpp)

The oversampling path uses half-band filters for 2x/4x upsampling. This is the
most CPU-intensive filter mode because:
- Each 2x stage doubles the sample rate
- The filter processes 2x/4x more samples
- Half-band filters are efficient but still 2x/4x the work

### 2. Sinc64 TURBO (Interpolators.h + SincAVX2.h)

The TURBO path uses AVX2 dot product with 8-wide SIMD. On machines without AVX2,
this falls back to SSE4.1 (4-wide). The baseline is SSE4.1 with unroll-by-8.

### 3. Oscillator BLEP

BLEP (Band-Limited Step) anti-aliasing for square/saw waves requires a lookup
table and interpolation. The BLEP table access pattern is sequential but the
table itself is large (typically 256+ entries).

### 4. MixerBus Processing

The mixer bus iterates over all active channels and processes each one. For
high channel counts, this is O(n) where n = active channels. SIMD vectorization
(MixerSIMD.h) helps but only up to the SIMD width.

## What's NOT a Bottleneck

- **SampleRateConverter**: Already at local optimum (4 sessions, 22 accepted rounds).
  See resampler lab findings.
- **Basic filter types**: Biquad filters are computationally cheap (5 multiply-adds
  per sample per stage).

## Compiler Context

- GCC 15.2.1 with `-O3`
- Register allocator is extremely sensitive to local variable changes
  (see resampler lab findings)
- No AVX2 hardware on this machine
