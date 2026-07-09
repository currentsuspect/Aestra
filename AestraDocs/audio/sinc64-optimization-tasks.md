# Sinc64 Turbo — Quick Win Optimizations
Date: 2026-04-12
Priority: Performance improvement

## Task 1: Coefficient Symmetry Folding

**File:** `AestraAudio/include/DSP/Interpolators.h` (Sinc64Turbo struct)

**Current:** Table is 2048 phases × 64 taps = 131,072 floats = 512KB.
The dot product loads all 64 coefficients and multiplies each against its sample.

**Optimization:** The Kaiser-windowed sinc is symmetric: `h[n] = h[N-1-n]`.
Only store half the coefficients (32 taps). In the dot product:
1. Load sample from the front: `samples[i]`
2. Load sample from the back: `samples[63-i]`
3. Add them: `sum_pair = samples[i] + samples[63-i]`
4. Multiply once: `accum += sum_pair * coeffs[i]`

This replaces 64 multiplies with 32 multiplies + 32 adds. Adds are free alongside FMA.

**Table size:** 2048 × 32 = 65,536 floats = 256KB (fits better in L1)

**Files to change:**
- `Interpolators.h`: Change `coeffs[HALF_PHASES][TAPS]` → `coeffs[HALF_PHASES][TAPS/2]`
- `Interpolators.h`: Update coefficient precomputation loop
- `SincAVX2.h`: Update dot product to fold pairs
- `SincSSE41.h`: Update SSE version
- `SincNEON.h`: Update NEON version
- `SincAVX512.cpp`: Update AVX-512 version

**Benchmark:** Run `AestraSincBenchmark` before/after. Expect ~1.5-1.8× speedup on dot product.

---

## Task 2: Phase Table Linear Interpolation

**File:** `AestraAudio/include/DSP/Interpolators.h` (Sinc64Turbo::interpolate)

**Current:** Fractional phase quantized to nearest of 2048 entries. Fine for static resampling, but pitch shifting accumulates quantization error over long stretches.

**Optimization:** After loading coefficients for phase `p`, also load coefficients for phase `p+1`. Linearly interpolate between them using the sub-quantization fractional part.

```
// Current:
coeffs = table[nearest_phase]

// Optimized:
alpha = frac * 2048.0 - nearest_phase  // fractional part within the quantized step
coeffs_lo = table[nearest_phase]
coeffs_hi = table[min(nearest_phase + 1, 1023)]
for each tap:
    coeffs[t] = coeffs_lo[t] + alpha * (coeffs_hi[t] - coeffs_lo[t])
```

This is ONE extra FMA per coefficient (or vectorized: one `_mm256_fmadd_ps` per 8 taps). Negligible cost, infinite effective phase resolution.

**Files to change:**
- `Interpolators.h`: Update `Sinc64Turbo::interpolate` to load two phases and blend
- Same for `Sinc8Interpolator`, `Sinc16Interpolator` if they use phase tables

**Benchmark:** Run `AestraSincBenchmark` before/after. Expect <2% throughput decrease (from the extra FMA) but significant quality improvement for pitch shifting.

---

## Verification

After both changes:
1. `cmake -S . -B build -DAestra_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release`
2. `cmake --build build --target AestraSincBenchmark -j2`
3. `./build/Tests/AestraSincBenchmark --iterations 5`
4. Compare MFrame/sec against previous baseline (8.99 on SSE4.1)
5. Run full test suite: `ctest --test-dir build --output-on-failure`
