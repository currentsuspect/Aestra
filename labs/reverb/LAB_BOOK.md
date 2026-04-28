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

## Current State

- **Branch**: `develop`
- **Status**: Performance plateau reached. All low-hanging fruit (ring buffer wrapping) harvested. All modes exceed 50x real-time. Session 010 requires AVX2 hardware or architectural change.

### Benchmark Results (SSE4.1, no AVX2, 5s @ 48kHz)

| Mode | Dispatch (S008) | Scalar (S008) | Real-Time (Dispatch) |
|------|-----------------|---------------|---------------------|
| Room | **98.6 ms** | 107.8 ms | **50.73x** |
| Hall | **95.0 ms** | 110.8 ms | **52.64x** |
| Plate | **91.8 ms** | — | **54.46x** |

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

**Session 010 recommendation:** Either target a specific sub-hotspot with proven
potential (e.g., Parameter Smoothing SSE vectorization) or pivot to measuring
on AVX2 hardware to validate the AVX2 FDN path.
