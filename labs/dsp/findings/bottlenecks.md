# Bottlenecks

Known performance characteristics of the DSP subsystem.

## Current Performance (GCC 15.2.1, SSE4.1 only, no AVX2)

| Algorithm | Mf/s | us/block | Relative to Cubic |
|-----------|------|----------|-------------------|
| Cubic (4-pt) | ~46 | 5.5 | 1.00x |
| Sinc8 original | 8.97 | 28.54 | 0.20x |
| **Sinc8 TURBO** | **27.40** | **9.34** | **0.62x** |
| Sinc64 original | 1.98 | 129.48 | 0.04x |
| Sinc64 TURBO | 8.61 | 29.75 | 0.19x |

## Why Sinc8 TURBO Got 3.06x (Not 4.35x Like Sinc64 TURBO)

The polyphase table approach has fixed overhead per sample:
- Phase index calculation: `frac * 2047 + 0.5`
- Half-phase check: `phaseIdx >= HALF_PHASES`
- Table pointer fetch: `table.coeffs[lutIdx]`
- Bounds check: `startIdx >= 0 && startIdx + TAPS <= totalFrames`

With 64 taps, this overhead is amortized over 64 multiply-adds.
With 8 taps, the same overhead is amortized over only 8 multiply-adds.
The ratio of "overhead work" to "compute work" is 8x higher for Sinc8.

**This is a fundamental limitation of the approach for small tap counts.**
For very short kernels (8 taps), the trig-reduction approach (1 sin per sample)
is already quite efficient. The polyphase table wins by eliminating the divide
and weight multiply, but those are only 2 operations per tap.

## Known Bottlenecks

### 1. Filter Oversampling (Filter.cpp)
Half-band filters for 2x/4x upsampling. Most CPU-intensive filter mode
because each stage doubles the sample rate.

### 2. Oscillator BLEP
BLEP table access for square/saw anti-aliasing. Sequential access pattern
but large table (256+ entries).

### 3. MixerBus Processing
O(n) over active channels. SIMD helps but only up to SIMD width.

## What's Already Optimized

- **SampleRateConverter**: Local optimum (resampler lab, 4 sessions)
- **Sinc8**: TURBO applied (this session)
- **Sinc64**: TURBO already existed (pre-this-lab)

## Compiler Context

- GCC 15.2.1 with `-O3`
- SSE4.1 only, no AVX2 hardware
- Register allocator is extremely sensitive to local variable changes
  (see resampler lab findings)
