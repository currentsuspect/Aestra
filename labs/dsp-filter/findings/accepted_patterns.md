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
