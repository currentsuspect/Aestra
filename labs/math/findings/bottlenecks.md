# Bottlenecks

No profiling done yet. The math utilities are used in two places in the audio
engine (`dbToGain` in AudioEngine.cpp and AudioRenderer.cpp).

## Potential Areas

- Matrix4x4 multiplication: naive O(n³) loop, no unrolling or SIMD
- Vector normalization: calls `std::sqrt` + division; `lengthSquared()` could
  avoid sqrt in callers that only need relative magnitude
- `dbToGain`: uses `std::exp` which is correct but ~50-100 cycles; the
  `LN10_OVER_20` constant is precomputed but could use a lookup table or
  polynomial approximation for hot paths
- `lerp`: could overflow if `a + t*(b-a)` exceeds float range; `a*(1-t) + b*t`
  is safer but has different numerical properties
