# Accepted Patterns

(No sessions yet)

## Exact Coefficient Initialization Before Runtime Smoothing

**Where**: `AestraAudio/src/DSP/Filter.cpp`
**What**: `updateCoefficients()` now copies target biquad coefficients directly
into the live coefficients, while runtime smoothing continues until coefficient
convergence for later updates.
**Why it works**: The filter previously started from identity coefficients and
only moved one smoothing step toward the target, leaving the default low-pass
path close to passthrough.
**Session**: M003

## Normalize Filter Response Measurements By Input RMS

**Where**: `Tests/AestraAudio/FilterTest.cpp`
**What**: `measureResponse()` now returns gain relative to the input sine RMS
instead of raw output RMS.
**Why it works**: A unity-gain passband on a full-scale sine should measure
near `1.0`, not `0.707`. This makes the hard-gate thresholds match the metric
being asserted.
**Session**: M003

## Short-Circuit `updateInternal()` Once Parameters Settle

**Where**: `AestraAudio/src/DSP/Filter.cpp`
**What**: `updateInternal()` now returns immediately when neither
`m_parametersChanged` nor `m_needsUpdate` is set, and clears
`m_parametersChanged` once the smoothed parameters have converged.
**Why it works**: Stable filters were still paying per-block interpolation
checks even after reaching their targets. The accepted change removes that dead
work without changing filter math.
**Session**: M004

## Per-Block Dispatch in processBlock()

**Where**: `AestraAudio/src/DSP/Filter.cpp`
**What**: `processBlock()` now evaluates `m_drive` and `m_oversampling` once
before the loop and dispatches into one of four specialized loops:
(1) fast path — no drive, no oversampling; (2) drive only; (3) oversampling
only; (4) both.
**Why it works**: The original loop checked two branches per sample even though
these values are constant per block. The fast path (most common in practice)
is now a tight loop with only `processSample()` calls and zero branches.
**Session**: M005

## Inline Oversampling Math in Stereo Dispatch

**Where**: `AestraAudio/src/DSP/Filter.cpp`
**What**: `processBlockStereo()` oversampling paths now inline the TwoX
oversampling math (two biquad passes + average) and the right-channel biquad
instead of calling `processOversampled()` and `processSample()` per sample.
State variables are hoisted outside the loop and written back once per block.
**Why it works**: The original code made two function calls per sample per
channel in the oversampling paths. Inlining the biquad math eliminates call
overhead and improves register locality, following the same pattern as M005/M007
but applied to the stereo oversampling branches.
**Session**: M008
