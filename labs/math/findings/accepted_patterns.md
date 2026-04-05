# Accepted Patterns

## GLSL-Style Free Functions for Vectors

**Where**: `AestraCore/include/AestraMath.h`
**What**: Added `distance()`, `reflect()`, `project()`, `lerp()`, and `clamp()` overloads for Vector2/3/4.
- `distance(a, b)`: Euclidean distance via `(a - b).length()`
- `reflect(v, n)`: Reflection formula `v - n * 2 * dot(v, n)` (requires normalized normal)
- `project(v, onto)`: Projection of `v` onto `onto` with zero-vector guard
- `lerp(a, b, t)`: Vector linear interpolation via `a + (b - a) * t`
- `clamp(v, minVal, maxVal)`: Component-wise and scalar variants using explicit ternary to avoid overload shadowing

**Why it works**: These are standard GLSL-style utilities that users expect from a math library. All are `constexpr` where possible, `noexcept`, and `[[nodiscard]]`. The `clamp` overloads use explicit ternary expressions instead of calling the scalar `clamp` to avoid name shadowing between vector and scalar overloads.
**Session**: M002 R1
