# Bottlenecks

Known performance characteristics of the resampler after session 001
optimizations. This file tracks what's still slow so future sessions know
where to look.

## Current Hot Path (after session 001)

The `process()` loop structure after all accepted optimizations:

```
for each input frame:
    push frame to history (2 writes per channel, bitwise AND wrap)
    srcPosition += 1.0
    while outputFrames < maxOutputFrames:
        check history window bounds
        compute historyPos, intPos, fracPos
        compute phaseIndex (bitwise AND)
        compute samplePos0
        compute windowIdx (bitwise AND + add)
        for each channel:
            dotProduct(window, coeffs, numTaps)  ← SSE4.1 auto-vectorized
        advance nextOutputSrcPos
```

## Known Bottlenecks

### 1. Dot Product (dominant)

The `dotProductScalar()` function is the dominant cost for all quality levels.
It runs once per channel per output sample.

- **Linear**: 2 taps → very fast, but still called per channel per output sample
- **Cubic**: 4 taps → moderate
- **Sinc8**: 8 taps → significant
- **Sinc16**: 16 taps → heavy
- **Sinc64**: 64 taps → dominant cost

**Current state**: SSE4.1 target attribute + `#pragma GCC ivdep` enable
auto-vectorization. On this machine (no AVX2), this is the best scalar path.

**Potential improvements**:
- Loop unrolling for known tap counts (Linear=2, Cubic=4, Sinc8=8, etc.)
- FMA instructions if available (not on this machine)
- Cache-line-aware coefficient access for Sinc64

### 2. History Push

`SampleHistory::push()` writes 2 copies per channel per input frame.
With mirror factor 2 and 2 channels, that's 4 writes per frame.

**Current state**: Optimized with bitwise AND wrap. Already good.

**Potential improvements**:
- SIMD store for multi-channel writes (write all channels at once)
- Further reduce mirror factor to 1 (would require careful analysis of
  window access patterns — may not be safe)

### 3. Phase Computation

The fractional position computation involves several `double` operations
per output sample: multiply, subtract, cast, bitwise AND.

**Current state**: Hoisted constants, bitwise AND for polyphase index.

**Potential improvements**:
- Precompute phase increments for fixed ratios (upsampling/downsampling
  with common sample rates have fixed ratios)
- Use fixed-point arithmetic for phase tracking (avoid double operations)

### 4. Machine Variance

This machine has high benchmark variance (CV > 5% on many cases, sometimes
> 20%). This makes it hard to distinguish real improvements from noise.

**Impact**: Baselines are noisy. Small improvements (< 5%) cannot be
reliably detected.

**Mitigation**: Use `--iterations 5` or more for more stable medians.
Run on a quieter machine for final validation.

## What's NOT a Bottleneck

- **Branch prediction**: Most branches in the hot path are now outside the
  innermost loop or are predictable (the `while` loop condition).
- **Memory allocation**: None in the hot path (zero-allocation by design).
- **Function calls**: `getWindow()` is inlined, `push()` is small and likely
  inlined by the compiler.
- **Modulo operations**: All replaced with bitwise AND.
