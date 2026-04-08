# Invariants

1. **`dbToGain(0.0f) == 1.0f`**: 0 dB is unity gain.
2. **`gainToDb(1.0f) == 0.0f`**: Unity gain is 0 dB.
3. **`dbToGain(-90.0f) == 0.0f`**: Below -90 dB is silence.
4. **`gainToDb(0.0f)` must not return `-inf` or `NaN`**: Returns a defined floor value (e.g. -90 dB).
5. **`map()` must not return `NaN` when `inMax == inMin`**: Returns `outMin` or handles gracefully.
6. **`lerp(a, b, 0.0f) == a` and `lerp(a, b, 1.0f) == b`**: Interpolation endpoints are exact.
7. **Vector identity**: `v.normalized().length()` is `1.0f` for non-zero `v`.
8. **Matrix identity**: `Matrix4x4::identity() * v == v` for any Vector4.
9. **No silent NaN propagation**: Operations on valid inputs must not produce NaN/Inf.
10. **Cross product**: `X × Y == Z` for unit basis vectors (1,0,0) × (0,1,0) = (0,0,1).
