# Aestra Polyphase Resampling: Adaptive Quality at Interactive Rates

**Technical Paper — Aestra Audio Engine**  
*December 2025 — Revision 2.0*

---

## Abstract

We present the **Aestra Polyphase Resampling Engine**, a multi-tier interpolation system with a **144 dB SNR kernel design target at real-time throughput (4.32 MFrame/sec)** using polyphase filter banks with symmetry exploitation and multi-architecture SIMD dispatch. This revision introduces **Sinc32Turbo**, a cache-friendly 64KB tier enabling 2× throughput for mixing scenarios with a 100 dB SNR design target.

> **Measured delivered performance (Audio Research Bench Phases 1 + 2D, 2026-07).**
> SNR figures in this paper are *kernel stopband design targets*. Bench-measured
> delivered behavior of Sinc64Turbo (full-band single-tone residual SINAD, 1 kHz):
> **~88 dB at fractional rate ratios** (bounded by nearest-phase LUT quantization,
> not the kernel) and **~154 dB at exact 2:1 ratios**. That figure is
> **path-specific, not a statement about all Aestra resampling**: full-session
> measurement (Phases 2D/2E) shows mainline session playback and both export
> flavors (full-mix and isolated-track bounce, kernel-unified in Phase 2E)
> dispatch the Sinc64 quality setting to the *legacy exact-sinc*
> `Sinc64Interpolator` and measured **~146–154 dB** end-to-end; Sinc64Turbo is
> what the sampler and audition paths run (~88 dB class).
> The interpolators on **all** measured paths reconstruct at the *source*
> Nyquist: **downsampling is not currently anti-aliased by a ratio-aware
> low-pass**, so content between the output and source Nyquist frequencies folds
> back into the output band. Passband level and DC accuracy measure essentially
> exact (<1e-5 dB / ≤3e-8). Methodology, full rate-matrix tables, and claim
> boundaries are recorded in the internal audio research bench, which is not
> published with this repository.

---

## 1. The Resampling Spectrum

### Quality vs. Performance Tiers

| Tier | Algorithm | Taps | Phases | Table Size | SNR (design target) | Use Case |
|------|-----------|------|--------|------------|---------------------|----------|
| **Fast** | Linear | 2 | — | — | ~60dB | Scrubbing, preview |
| **Draft** | Sinc32Turbo | 32 | 1024 | 64KB (L1) | ~100dB | Mixing, muted tracks |
| **Standard** | Cubic Hermite | 4 | — | — | ~80dB | General editing |
| **High** | Sinc64Turbo | 64 | 2048 | 256KB (L2) | ~144dB (measured ~88 dB SINAD at fractional ratios) | Mixing, export |

### Cache Hierarchy Alignment

```
Sinc32Turbo:  64KB  → Fits entirely in L1 cache (most CPUs)
Sinc64Turbo: 256KB  → Fits in L2 cache (guaranteed hit rate)
```

---

## 2. Sinc32Turbo: The Mixing Tier

### Why 32 Taps?

For mixing workflows where 50+ tracks may be pitch-shifted simultaneously:

| Metric | Sinc32 | Sinc64 | Δ |
|--------|--------|--------|---|
| **Table Size** | 64KB | 256KB | 4× smaller |
| **Cache Level** | L1 | L2 | 3× faster access |
| **Throughput** | ~8 MFrame/s | ~4 MFrame/s | 2× faster |
| **SNR (design target)** | ~100dB | ~144dB | Imperceptible difference |

At the ~100 dB design target, kernel noise is far below perception in a mix context. (Delivered fractional-ratio SINAD is phase-LUT-bound for both tiers — see the measured-performance note in the Abstract.)

### Implementation

```cpp
struct Sinc32Turbo {
    static constexpr int TAPS = 32;
    static constexpr int PHASES = 1024;
    static constexpr int HALF_PHASES = 512;  // Symmetry
    static constexpr double KAISER_BETA = 9.0;

    struct alignas(64) Table {
        float coeffs[HALF_PHASES][TAPS];  // 64KB - fits L1
    };
};
```

---

## 3. Sinc64Turbo: The High-Quality Tier

### Architecture

```cpp
struct Sinc64Turbo {
    static constexpr int TAPS = 64;
    static constexpr int PHASES = 2048;
    static constexpr int HALF_PHASES = 1024;  // Symmetry
    static constexpr double KAISER_BETA = 12.0;

    struct Table {
        float coeffs[HALF_PHASES][TAPS];  // 256KB - fits L2
    };
};
```

### Symmetry Exploitation

The sinc function has even symmetry: `sinc(-x) = sinc(x)`. We store only phases [0, 1024):

```cpp
bool reversed = (phaseIdx >= HALF_PHASES);
int lutIdx = reversed ? (PHASES - 1 - phaseIdx) : phaseIdx;
const float* c = table->coeffs[lutIdx];

if (reversed) coeff = c[TAPS - 1 - t];  // Read backwards
```

### SIMD Vectorization

The inner loop is a dot product—ideal for SIMD:

```cpp
// AVX2: Process 8 taps per iteration (8 iterations for 64 taps)
__m256 vSumL = _mm256_setzero_ps();
__m256 vSumR = _mm256_setzero_ps();

for (int i = 0; i < 64; i += 8) {
    __m256 vCoeff = _mm256_loadu_ps(&coeffs[i]);
    __m256 vL = /* gather left samples */;
    __m256 vR = /* gather right samples */;
    
    vSumL = _mm256_fmadd_ps(vL, vCoeff, vSumL);
    vSumR = _mm256_fmadd_ps(vR, vCoeff, vSumR);
}
```

---

## 4. Multi-Architecture SIMD Dispatch

```cpp
static const auto& cpu = CPUDetection::get();
static const bool useAVX2 = cpu.hasAVX2() && cpu.hasFMA();
static const bool useSSE41 = cpu.hasSSE41() && !useAVX2;
static const bool useNEON = cpu.hasNEON();

if (useAVX2) sincDotProductAVX2(c, samples, sumL, sumR);
else if (useSSE41) sincDotProductSSE41(c, samples, sumL, sumR);
else if (useNEON) sincDotProductNEON(c, samples, sumL, sumR);
else /* scalar fallback */;
```

| Architecture | Samples/Cycle | Platforms |
|-------------|---------------|-----------|
| AVX2 | 8 | Intel Haswell+, AMD Zen+ |
| SSE4.1 | 4 | Intel Core 2+, AMD K10+ |
| ARM NEON | 4 | Apple Silicon, Snapdragon |
| Scalar | 1 | Fallback |

---

## 5. Benchmark Results

**Test Configuration:**
- CPU: Intel Core (SSE4.1 available)
- Buffer: 256 frames × 1000 blocks
- Audio: 48kHz stereo random noise

| Algorithm | MFrame/sec | Rel. Speed | SNR (design target) |
|-----------|-----------|------------|---------------------|
| Cubic (4-point) | 43.44 | — | ~80dB |
| Sinc8 (8-point) | 9.41 | — | ~100dB |
| Sinc64 Legacy | 1.63 | 1.0× | ~144dB |
| **Sinc32 Turbo** | **~8.0** | **4.9×** | **~100dB** |
| **Sinc64 Turbo** | **4.32** | **2.65×** | **~144dB** |

### Practical Track Limits at 48kHz

| Quality | MFrame/sec | Tracks (100% CPU) | Tracks (50% headroom) |
|---------|-----------|-------------------|----------------------|
| Draft | ~8.0 | 166 | **83** |
| High | ~4.32 | 90 | **45** |

---

## 6. Audio Quality Verification

### Frequency Response (64-tap)

- Transition band: < 0.5% of Nyquist *(design model)*
- Stopband attenuation: > 140 dB *(design model)*
- Passband ripple: < 0.001 dB *(design model; passband level measured exact to <1e-5 dB at 1 kHz)*

### Distortion Metrics

The table below reflects the *filter design model*, not bench measurements.

| Metric (design model) | Sinc64 | Sinc32 | Linear | Unit |
|--------|--------|--------|--------|------|
| THD+N | -144 | -100 | -60 | dB |
| IMD | -140 | -96 | -55 | dB |

**Measured corrections (Audio Research Bench Phases 1 + 2D, 2026-07 — internal
audio research bench, unpublished):** delivered full-band single-tone residual
(THD+N-style) for Sinc64Turbo is **~-88 dB at fractional rate ratios** (phase-LUT
quantization bound; ~-154 dB at exact 2:1) — a path-specific figure: mainline
session playback and both export flavors run the legacy exact-sinc
`Sinc64Interpolator` (kernel-unified in Phase 2E) and measured ~-146 to -154 dB
end-to-end (doc §8); the ~-88 dB class applies to the Sinc64Turbo paths (sampler,
audition). IMD has not been bench-measured yet.
Aliasing is **not** "None": the kernels reconstruct at the *source* Nyquist and no
ratio-aware anti-alias low-pass is applied, so when downsampling, content between
the output and source Nyquist frequencies folds back (measured near full scale for
between-Nyquists probes); upsampling image rejection measured -60.5 to -68.2 dBc
at 0.9-Nyquist probes and -21.7 dBc for a transition-band probe (44.1→48 kHz at
21 kHz).

---

## 7. Intelligent Quality Switching

The Aestra audio engine supports **runtime quality selection**:

```cpp
// PlaylistMixer: Global quality setting
PlaylistMixer::setResamplingQuality(ClipResamplingQuality::Draft);

// Per-track override for soloed/bouncing tracks
if (track.isSoloed() || isBouncing) {
    resampler.setQuality(ClipResamplingQuality::High);
}
```

### Recommended Workflow

| Action | Quality |
|--------|---------|
| Live playback (many tracks) | **Draft** |
| Soloed track preview | **High** |
| Final mixdown/export | **High** |
| Scrubbing/preview | **Fast** |

---

## 8. Conclusion

The Aestra Polyphase Resampling Engine proves that **adaptive quality switching** combined with **cache-aware design** enables:

- **Sinc64Turbo**: 144 dB-target kernel at 4.32 MFrame/sec (measured delivered
  SINAD: ~88 dB at fractional rate ratios, ~154 dB at exact 2:1 — see the
  measured-performance note in the Abstract)
- **Sinc32Turbo**: 100 dB-target kernel at ~8 MFrame/s, 2× faster
- **Multi-arch SIMD**: AVX2/SSE4.1/ARM NEON with runtime dispatch

The key innovations:
1. **Tiered polyphase tables** — L1 vs L2 cache optimization
2. **Symmetry exploitation** — 50% memory reduction
3. **Quality-aware scheduling** — Use High only when perceivable

---

## References

1. Smith, J.O. "Digital Audio Resampling Home Page", CCRMA Stanford, 2021
2. Kaiser, J.F. "Nonrecursive digital filter design using I0-sinh window function", IEEE, 1974
3. Simper, A. "Cytomic Technical Papers", 2013

---

## Appendix A: 2025-05-24 Benchmark Update

**Test Configuration:**
- Platform: Linux (x86_64, AVX2 enabled)
- Source: 44.1kHz -> 48kHz (Upsampling)

| Algorithm | Speed (ns/sample) | Throughput (MFrame/sec) | Relative |
|-----------|-------------------|-------------------------|----------|
| Linear    | 34.20 ns          | ~29.2 MHz               | 1.0x     |
| Cubic     | 33.51 ns          | ~29.8 MHz               | 1.02x    |
| Sinc16    | 39.82 ns          | ~25.1 MHz               | 0.86x    |
| Sinc64Turbo | 71.91 ns        | ~13.9 MHz               | 0.48x    |

**Analysis:**
On modern AVX2 hardware, `Sinc64Turbo` achieves nearly 14 million stereo samples per second, providing massive headroom (approx 290x real-time at 48kHz). The cost of the highest-quality tier is now negligible for typical track counts.

---

*© 2025 Aestra Studios. This research is part of the Aestra Digital Audio Workstation project.*
