# Bottlenecks

Known performance characteristics of the resampler after session 003
optimizations. This file tracks what's still slow so future sessions know
where to look.

## Current Hot Path (after session 003)

The `process()` loop structure after all accepted optimizations:

```
for each input frame:
    push frame to history (2 writes per channel, bitwise AND wrap)
    srcPosition += 1.0
    srcNextDiff = srcPosition - nextOutputSrcPos    (kept for register allocation!)
    while outputFrames < maxOutputFrames:
        check history window bounds
        compute historyPos, intPos, phaseIndex
        compute windowIdx (bitwise AND + add)
        for each channel:
            dotProduct(window, coeffs, numTaps)  ← SSE4.1 auto-vectorized, unroll 8
        advance nextOutputSrcPos, srcNextDiff, historyPos
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

**Current state**: Unroll by 8 + `__restrict__` pointers. SSE4.1 target attribute
does NOT have `#pragma GCC ivdep` (it was dropped in session 002 because it
regressed on GCC 15). On this machine (no AVX2), this is the best scalar path.

**Potential improvements**: None found. Multiple attempts to optimize further
(tap-count specialization, ivdep restoration, variable elimination) all regressed.
The dot product is at a local optimum for this compiler/hardware combination.

### 2. GCC 15 Register Allocation Sensitivity

**Critical finding**: GCC 15.2.1 -O3 is extremely sensitive to the local variable
set in the hot loop. Removing `srcNextDiff` (a seemingly dead variable in the
output loop) caused a 64-108% regression across ALL cases. The compiler uses
this variable for optimal register allocation.

**Impact**: Any change to the local variable set in `process()` risks triggering
a worse register allocation. Small changes can have massive, non-obvious effects.

**Mitigation**: Test any structural change with full benchmark, not just correctness
tests. Expect regressions from seemingly benign simplifications.

### 3. History Push

`SampleHistory::push()` writes 2 copies per channel per input frame.
With mirror factor 2 and 2 channels, that's 4 writes per frame.

**Current state**: Optimized with bitwise AND wrap and inline stereo fast path.
Already good.

### 4. Machine Variance

This machine has high benchmark variance (CV > 5% on many cases, sometimes
> 20%). This makes it hard to distinguish real improvements from noise.

**Impact**: Baselines are noisy. Small improvements (< 5%) cannot be
reliably detected.

**Mitigation**: Use `--iterations 5` or more for more stable medians.
Run on a quieter machine for final validation.

## What's NOT a Bottleneck

- **Branch prediction**: Most branches in the hot path are now outside the
  innermost loop or are predictable.
- **Memory allocation**: None in the hot path (zero-allocation by design).
- **Function calls**: `getWindow()` is inlined, `push()` is small and likely
  inlined by the compiler.
- **Modulo operations**: All replaced with bitwise AND.
- **`#pragma GCC ivdep`**: Harmful on GCC 15 with explicit unrolling.
- **`srcNextDiff` elimination**: Causes massive regressions due to register allocation.

## Local Optimum Reached

After 4 sessions (22 accepted rounds, 4 rejected), the code appears to be at a
local optimum for GCC 15 on this hardware. Further improvements would require:
- AVX/FMA instructions (not available on this machine)
- Algorithmic changes (FFT-based resampling — out of scope)
- A different compiler version
