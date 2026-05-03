# Reverb Lab Book

## Purpose

Persistent memory for the Reverb SIMD optimization lab.

## Structure

```
labs/reverb/
├── program.md              — Constitution (scope, rules, gates)
├── EVALS.md                — Eval documentation (build, lanes, thresholds)
├── LAB_BOOK.md             — This file (entry point for lab memory)
├── results/                — Generated eval outputs (gitignored)
├── sessions/               — Per-session logs (one file per session)
└── findings/               — Durable knowledge, updated after each session
    ├── accepted_patterns.md     — Optimizations that worked, why
    ├── rejected_patterns.md     — Optimizations that failed, why
    ├── invariants.md            — Things that must never break
    └── bottlenecks.md           — Known performance characteristics
```

## Default Read Set

1. `program.md` — rules, scope, acceptance logic
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — this file (session summary, finding pointers)
4. `findings/invariants.md` — things that must never break

## Session Summary

| Session | Date | Rounds | Accepted | Rejected | Notes |
|---------|------|--------|----------|----------|-------|
| 001 | 2026-04-28 | 1 | 1 | 0 | SIMD core + cubic Hermite + benchmark. See sessions. |
| 002 | 2026-04-28 | 1 | 1 | 0 | CI workflow + benchmark CLI extensions. See sessions. |
| 003 | 2026-04-28 | 1 | 1 | 0 | Stage profiling + quality guardrails. See sessions. |
| 004 | 2026-04-28 | 1 | 3 | 1 | FDN Delay Read optimization (power-of-two + bitmask). See sessions. |
| 005 | 2026-04-28 | 1 | 2 | 2 | Early Reflection optimization + Plate timing investigation. See sessions. |
| 006 | 2026-04-28 | 1 | 4 | 1 | Plate post-allpass optimization + profile stage + benchmark bug fix. See sessions. |
| 007 | 2026-04-28 | 1 | 4 | 1 | Diffuser wrapping optimization (power-of-two + bitmask). See sessions. |
| 008 | 2026-04-28 | 1 | 3 | 0 | Input/Predelay optimization (power-of-two + bitmask). See sessions. |
| 009 | 2026-04-28 | 1 | 0 | 2 | LFO Normalize + Control investigated, no measurable gain. Rejected. See sessions. |
| 010 | 2026-04-28 | 1 | 2 | 0 | Quality measurement baseline established. See sessions. |
| 011 | 2026-04-28 | 1 | 3 | 0 | Late-tail decorrelation + quality lab bug fix. See sessions. |
| 012 | 2026-04-28 | 1 | 1 | 2 | Plate metallic peak elimination with peaking EQ. See sessions. |
| 013 | 2026-04-28 | 1 | 1 | 1 | 4687 Hz peak validation + perceptual metric improvement. See sessions. |
| 014 | 2026-04-28 | 1 | 1 | 0 | Early reflection density metric calibration. See sessions. |
| 015 | 2026-04-28 | 1 | 1 | 0 | Synthetic material validation. See sessions. |
| 016 | 2026-04-28 | 1 | 1 | 0 | Room width safety + v0.1 freeze decision. See sessions. |

## Current State

- **Branch**: `develop`
- **Status**: Performance plateau reached. All low-hanging fruit (ring buffer wrapping) harvested. All modes exceed 50x real-time. Quality baseline corrected (Hall was measured as Plate in S010). Late-tail decorrelation fixes Room/Plate stereo collapse.

### Benchmark Results (SSE4.1, no AVX2, 5s @ 48kHz)

| Mode | Dispatch (S011) | Real-Time (Dispatch) |
|------|-----------------|---------------------|
| Room | **96.3 ms** | **51.92x** |
| Hall | **92.6 ms** | **53.99x** |
| Plate | **88.8 ms** | **56.32x** |

**All modes exceed 50x real-time with cubic Hermite interpolation.**

### Quality Results

- **Cubic Hermite interpolation**: +1.12 pp HF energy >10kHz vs linear
- **Callback budget**: <2.0% even in heaviest mode (non-profile)
- **Projected AVX2 speedup**: 1.5-2.0x overall vs scalar (cubic would match/exceed original linear speed)

### Cumulative Progress (Room Dispatch, 5s @ 48kHz)

| Session | Room Time | Delta | Cumulative vs S001 |
|---------|-----------|-------|-------------------|
| Baseline (original linear) | ~63 ms | — | — |
| S001 (cubic + SIMD core) | 107.3 ms | +70% quality | Quality baseline |
| S004 (FDN bitmask) | 110.0 ms | -4.5% | — |
| S005 (early refl bitmask) | 124.0 ms | +1.8%* | — |
| S006 (post-allpass bitmask) | 127.5 ms | -1.8%* | — |
| S007 (diffuser bitmask) | 103.7 ms | **-18.7%** | **-3.3%** |
| S008 (predelay bitmask) | 98.6 ms | **-4.9%** | **-8.1%** |
| S009 (LFO/control) | 98.6 ms | 0% | **-8.1%** |

*Numbers affected by system load variance on 2-core machine.

### Stage Profile Hotspots (Session 008, Final State)

| Rank | Stage | % |
|------|-------|---|
| 1 | **FDN Delay Read** | **28.6%** |
| 2 | Output/Mix | 14.0% |
| 3 | Early Reflections | 12.2% |
| 4 | FDN Feedback/Matrix | 8.7% |
| 5 | Diffuser | 8.4% |
| 6 | LFO Normalize + Control | 7.4% |
| 7 | Input/Predelay | 6.5% |
| 8 | Plate Post-Allpass | 5.4% |
| 9 | Parameter Smoothing | 4.4% |
| 10 | Modulation/LFO | 4.2% |

**All ring buffers** (FDN, early reflections, diffusers, post-allpass, predelay) now use power-of-two + bitmask.

### Durable Patterns (Accepted)

1. **Power-of-two + bitmask wrapping** — The universal pattern. Applied to all 5
   ring buffer types. Eliminates all branches and modulo operations from buffer wrapping.
2. **Separate delay length from buffer capacity** — Allows power-of-two capacity
   while preserving exact delay timing. Used in post-allpass, diffusers.
3. **Cache mask as member variable** — `m_delayLineMasks[]`, `m_earlyMask`,
   `m_platePostMasks[]`, `m_diffuserMasks[]`, `m_predelayMask`. Avoids recomputing
   `size - 1` in hot paths.
4. **Remove ineffective prefetch** — `__builtin_prefetch` removed from FDN delay
   read (S004). Cache-line-local samples don't benefit from explicit prefetch.

### Rejected Patterns (Documented)

1. **SIMD delay reads** — Gather/scatter not available on SSE4.1; would require
   interleaved storage (topology change). Rejected per prompt guidance.
2. **SIMD early reflection taps** — 12 taps with interleaved L/R accumulation
   don't vectorize cleanly on SSE4.1. Scalar bitmask is faster.
3. **Branchless `frac < 0` fix** — Compiler handles predictable branch well.
   No improvement measured.
4. **Hybrid cubic/linear interpolation** — Violates hard constraint.
5. **Changing delay to match buffer capacity** — Would alter sound. Delay length
   must be preserved independently of capacity.
6. **SIMD gather for diffuser reads** — Only 4 stages, different buffers, different
   offsets. Not worth complexity; scalar bitmask is nearly as fast as SSE.
7. **Reducing LFO normalization frequency** — Expected gain (~0.2%) is below
   benchmark noise floor (~5-15%). No measurable improvement.
8. **Fast reciprocal sqrt for scalar LFOs** — Expected gain (~0.2%) is below
   measurement threshold. Not worth adding approximate math.

### Performance Plateau Analysis

The remaining hotspots are fundamentally limited by:
- **FDN Delay Read (28.6%)**: Cubic Hermite interpolation arithmetic (64 FMAs/sample)
  and memory bandwidth (32 float reads/sample). Cannot be optimized further without
  changing interpolation order or memory layout.
- **Output/Mix (14.0%)**: Width matrix (4 FMAs/sample) and clamping. SIMD would save
  ~2 ops, negligible at current performance levels.
- **LFO Normalize + Control (7.4%)**: Infrequent operations that appear large in
  profile builds due to `std::chrono` overhead distortion. Actual production cost <1%.

**Further meaningful gains require:**
- AVX2-capable hardware (projected 1.5-2.0x from vectorized FDN feedback)
- Lower-noise benchmark environment (to measure sub-5% optimizations)
- Architectural redesign of FDN delay-line memory layout for gather-friendly access

### Quality Baseline (Session 014)

| Metric | Room | Hall | Plate |
|--------|------|------|-------|
| T60 | 661 ms | 1349 ms | 576 ms |
| Spectral | Mid-heavy | High-heavy | Balanced |
| Crest Factor | 6.9 | 5.2 | 9.1 |
| Late Correlation | 0.643 | 0.525 | 0.604 |
| Bloom Time | 6.9 ms | 8.7 ms | 48.1 ms |
| Metallic Peak (Hz/dB/local/band/sev) | 960 / 16.8 / 4.9 / mid / none | 1007 / 14.3 / 7.4 / mid / none | 4687 / 19.2 / 14.9 / presence / low |
| Early Onset | 54.3 ms | 59.9 ms | 53.1 ms |
| Early Peak Count | 593 | 29 | 1375 |
| Early Active Ratio | 0.55 | 0.14 | 0.65 |
| Early Density Score | 12.07 | 0.10 | 90.54 |

**Red flags:** None.

**Session 014 verdict:** Early reflection metric now correctly distinguishes modes:
Hall is sparse (0.10), Room is moderate (12.07), Plate is dense/diffuse (90.54).

**Note:** Session 010 Hall metrics were incorrect due to a quality lab bug
(Hall parameter 1.0f mapped to Plate mode). True Hall T60 is ~1349ms.

**Quality artifacts:** `labs/reverb/quality/` — impulse WAVs, noise burst WAVs, JSON metrics, Markdown report.

### v0.1 Freeze State (Session 016)

**Room decorrelation: k=0.30** (reduced from 0.60 to tame extreme width on dense material)

| Metric | Room | Hall | Plate |
|--------|------|------|-------|
| T60 | 666 ms | 1349 ms | 576 ms |
| Late Correlation (impulse) | **0.820** | 0.525 | 0.604 |
| Late Correlation (chord stab) | **-0.679** | 0.824 | -0.404 |
| Late Correlation (mix bus) | **-0.835** | 0.813 | -0.522 |
| Metallic Severity | none | none | low |
| Mono Peak (mix bus) | 0.218 | 0.226 | 0.134 |
| Real-Time | ~55x | ~49x | ~44x |

**v0.1 Freeze Verdict:**
- **No critical issues.** All modes stable, real-time safe, no metallic ringing.
- **Hall** is the most consistent mode across all material.
- **Room** width is now moderate (k=0.30). Extreme negative correlation on dense
  material is partly an FDN characteristic; decorrelation adjustment alone cannot
  fully eliminate it.
- **Plate** is bright and characterful. Transient tail energy is low (12-19% of
  Room RMS) — documented as v0.2 tuning candidate, not a v0.1 blocker.

**v0.2 Candidates:**
1. Plate transient tail energy tuning (level/decay scaling)
2. Room dense-material FDN correlation (architectural)
3. Real recorded material listening tests
4. Additional preset variations
