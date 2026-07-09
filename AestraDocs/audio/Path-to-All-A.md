# Path to All-A — Audio Quality Action Plan

**Status:** Internal execution plan
**Last Updated:** 2026-05-14
**Owner:** Dylan
**Companion docs:** [`implementation/audio_quality_executive_summary.md`](../implementation/audio_quality_executive_summary.md), [`architecture/ARCHITECTURE_AUDIT_2026Q2.md`](../architecture/ARCHITECTURE_AUDIT_2026Q2.md), [`PDC-v2-Design.md`](../PDC-v2-Design.md)

> Every layer that is below A gets a concrete plan with file citations, the test that proves it landed, and a target ship phase. Layers already at A+ get a regression test instead of a feature — to lock them.

---

## 0. Current scorecard (verified against source, 2026-05-14)

| # | Layer | Now | Target | Effort | Phase | Status |
|---|-------|----:|------:|------:|------:|:-------|
| 1 | Signal Integrity | A | A+ | 1d | P2 | Add null-test regression |
| 2 | Resampling Quality | A- | A | 3d | P2 | Sinc64 optimization tasks |
| 3 | Timing Integrity | A+ | A+ | 0.5d | P2 | Lock with sample-accurate test |
| 4 | **Plugin Delay Compensation** | **C** | **A+** | **3–4 wk** | **P2/P3** | **PDC v2 in flight** |
| 5 | Automation Smoothing | A+ | A+ | 0.5d | P2 | Lock with zipper-test |
| 6 | Denormal Handling | A+ | A+ | 0.5d | P2 | Move to thread-entry, lock |
| 7 | Clipping Behavior | B+ | A | 1 wk | P3 | True peak ceiling on master |
| 8 | Intersample Peaks (True Peak) | A- | A | 3–5d | P3 | Wire meter into export |
| 9 | Dithering / Export | A- | A | 3–5d | P3 | Centralize + mandate dither |
| 10 | CPU Efficiency | A+ | A+ | 0.5d | P2 | Lock with budget assertion |
| 11 | Pan Law | A- | A | 2–3d | P3 | Settings UI + persistence |
| 12 | Oversampling (nonlinear DSP) | C | A | 1–2 wk | P3 | Reusable oversampler |

**Total non-locked effort: ~6–8 weeks of focused work.** PDC v2 is the long pole. Everything else can be parallel-tracked or sequenced after.

---

## 1. PDC — Plugin Delay Compensation (C → A+)

**Heaviest item. Active. Has its own design doc.**

### Where we are
- **v1 ships** (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:3081`) — flat-chain compensation only. Correct for the trivial topology (N parallel tracks → master).
- **9 documented gaps** in `PDC-v2-Design.md` §2 (G1–G9). G1 (graph awareness), G2 (sidechain alignment), and G9 (regression coverage) are the priority gates.
- **Currently in:** P0–P1 transition per the design doc. P4 is now split into P4a (solver) and P4b (engine) per your last edit.

### Path to A+ — execute in order
| Phase | Deliverable | Test that proves it | Effort |
|------:|:------------|:--------------------|------:|
| **P1** | `PDCFlatChainTest` against v1 (red→green) + `PDCSolverPurityTest` stub | Both green; v1 behavior unchanged | 2d |
| **P2** | `LatencyGraph` + `SolvedLatencyTopology` types + flat-equivalent solver skeleton | Same tests green; v1 still passes | 3d |
| **P3** | Double-buffered atomic publish of `SolvedLatencyTopology` | All prior tests green + TSan clean | 2d |
| **P4a** | G1 solver: DFS, per-edge compensation, three-color cycle detection, audible-source ID | `PDCBusChainTest` + `PDCBranchingConvergenceTest` green at solver level | 4d |
| **P4b** | G1 engine: per-send ring buffers in `processBlock` consume `EdgeSolution::compensationSamples` | Audio-rendering versions of both tests assert sample-accurate impulse alignment at master | 4d |
| **P5** | G2 sidechain compensation (node-boundary) | `PDCSidechainAlignmentTest` green | 3d |
| **P6** | G3 smooth recompute (sample-hold on increase, crossfade on decrease) | `PDCLatencyChangeMidPlaybackTest` green | 3d |
| **P7** | G4 transport-latched mute stability | `PDCMuteToggleStabilityTest` green | 2d |
| **P8** | G5 oversize buffer path with power-of-two masking | `PDCOversizeBufferTest` green; zero RT allocs | 3d |
| **P9** | G6 dual reported-latency API (`projectAlignmentLatency` + `monitoringLatency`) | `PDCMasterBusReportedLatencyTest` green | 2d |
| **P10** | `LatencyDomain` plumbing (solver-only, no UI) | `PDCDomainExemptionTest` green | 2d |
| **P11** | (Deferred) G7 tail handling on stop — separate doc | — | TBD |

### Acceptance for A+
1. Sample-accurate impulse alignment at master across any DAG topology AestraEngine permits.
2. Sidechain feeders arrive within ±1 sample of the host path.
3. Mute toggle on the highest-latency contributor causes no audible time jump.
4. Latency-change-during-playback (VST3 `kLatencyChanged`, CLAP `plugin.latency`) produces no click, no dropout, no silence smear.
5. Allocation counter shows **zero** RT-side allocations after P5.
6. All 11+ PDC tests green on `ctest --test-dir build-headless`.

### Risks specific to PDC
- **Buffer sizing** — `TrackRTState::compensationBuffer` is fixed at 32 768 floats (≈340 ms @ 48 kHz). P8 fixes via off-RT growth + power-of-two masking. Until P8 lands, plugins reporting >340 ms latency silently mis-align.
- **G8 race** — `calculateLatencyCompensation` writes `m_graphStates[activeIdx].maxProjectLatencySamples` while the audio thread reads it. Resolved by P3's double-buffer flip.

---

## 2. Resampling Quality (A- → A)

### Where we are
- Sinc64 is available and high-quality (`@/home/currentsuspect/Dev/Aestra/AestraAudio` resampler family).
- Open optimization tasks tracked in [`sinc64-optimization-tasks.md`](sinc64-optimization-tasks.md).

### Path to A
1. **Land the queued sinc64 optimizations** from `sinc64-optimization-tasks.md`.
2. **Add a SNR null-test** comparing Sinc64 output against a reference (offline, headless) on canonical signals: sine sweep, white noise, impulse.
3. **Lock alias rejection ≥ 80 dB** at all engine-supported sample-rate conversions (44.1 ↔ 48 ↔ 88.2 ↔ 96 ↔ 176.4 ↔ 192).
4. **Test:** `ResamplerAliasRejectionTest` + `ResamplerSNRRegressionTest` against baseline file (`labs/resampler/results/baseline_sinc.json` already exists).

### Acceptance for A
- ≥ 80 dB alias rejection on all supported SR conversions.
- ≥ 100 dB SNR on a 0 dBFS 1 kHz sine through a non-integer-ratio conversion.
- Both tests run on every CI build and fail on regression vs `baseline_sinc.json`.

---

## 3. Clipping Behavior (B+ → A)

### Where we are
- `MasterSafetyLimiter` exists (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/MasterSafetyLimiter.h`).
- True peak meter is RT-live but **not** wired into the limiter's gain-reduction decision.
- Soft-clip behavior is acceptable but the master can still produce intersample peaks > -1 dBTP.

### Path to A
1. **Add a "True Peak Ceiling" mode** to `MasterSafetyLimiter` that uses `TruePeakMeter`'s 4x oversampled peak (already available via `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/TruePeakMeter.h:73`) instead of sample peak when deciding gain reduction.
2. **Lookahead via existing PDC infrastructure** — once PDC v2 P4b ships, lookahead is "just another delay edge."
3. **Soft-knee curve audit** — replace any hard-clip branches with a documented soft-knee transfer function. Measure THD at 0 dBFS sine input; target ≤ 0.5% at 1 kHz.

### Acceptance for A
- Master output never exceeds the user-set ceiling in dBTP (not just dBFS).
- THD ≤ 0.5% at 0 dBFS sine input.
- `MasterLimiterTruePeakCeilingTest` proves a 0 dBFS pulse train cannot push true-peak above -1.0 dBTP at default settings.

---

## 4. Intersample Peaks / True Peak (A- → A)

### Where we are — **better than the exec summary says**
- `TruePeakMeter` is implemented to ITU-R BS.1770-4 spec with 4× oversampling, Kaiser β ≈ 9.4, ≥ 80 dB stopband rejection (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/TruePeakMeter.h:18-46`).
- It runs every block (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1174-1179`) and publishes via atomics.
- **What's missing:** the meter result is observed but not enforced anywhere.

### Path to A
1. **Wire into export validation** — `AudioExporter` should consult `TruePeakMeter` (or run its own offline instance over the rendered buffer) and either:
   - Block export with a clear error if `truePeak > userCeiling`, OR
   - Insert a brick-wall true-peak limiter pass (preferred, opt-in default at -1.0 dBTP).
2. **Surface in master HUD** — a true-peak readout alongside the sample-peak meter. UI-side only.
3. **Add SIMD path** to `processStereo` if profiling shows the polyphase FIR is hot (currently scalar, `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/TruePeakMeter.h:144-156`).

### Acceptance for A
- Export pipeline cannot produce a file whose true-peak exceeds the user-configured ceiling (default -1.0 dBTP).
- UI displays both sample peak and true peak on the master.
- `ExportTruePeakCeilingTest` renders a known intersample-peak signal and asserts the output file's true peak ≤ ceiling.

---

## 5. Dithering / Export (A- → A)

### Where we are
- Dithering exists in the export path but is not centralized; bit-depth conversion happens in more than one place (audit §3.3 flags the dual compilation of `ProjectSerializer.cpp` — the export pipeline has a similar diffusion).
- 16-bit export without dither is currently possible by accident.

### Path to A
1. **Create `AestraAudio/include/Export/BitDepthConverter.h`** as the single chokepoint for 32-bit-float → {16-bit PCM, 24-bit PCM, 32-bit PCM, 32-bit float} conversion.
2. **Mandatory TPDF dither** for any conversion that reduces precision; document the exception path (32-bit float export) clearly.
3. **Validate at the export-settings boundary** — if user picks 16-bit, dither is on, period. No silent fallback.
4. **Migration:** route every existing call site through `BitDepthConverter`. The audit's recommendation to move `ProjectSerializer` into `AestraAudio` (§3.3) creates the natural home for `Export/` too.

### Acceptance for A
- One and only one TU performs bit-depth conversion.
- `ExportDitherRegressionTest` asserts that 16-bit export of a -60 dBFS sine produces measurable noise floor at the dither level (not zero, not quantization noise).
- `ExportNoSilentDegradationTest` asserts that requesting 16-bit always engages dither.

---

## 6. Pan Law (A- → A)

### Where we are
- Single hard-coded pan law (constant-power, equivalent to -3 dB).
- No user choice; no settings persistence.

### Path to A
1. **Add `PanLawMode` enum** in `AestraAudio/include/Core/PanLaw.h` with `ConstantPower`, `MinusThreeDB`, `MinusFourPointFiveDB`, `MinusSixDB`.
2. **Settings UI** — a single dropdown in audio settings panel. Persist in `audio_settings.conf` (or `aestra_profile.json`).
3. **Apply in `MixerChannel`** at the panner stage. Existing pan implementation is one function — swap in a table lookup keyed by `PanLawMode`.
4. **Backward compatibility:** existing projects open with `ConstantPower` (current behavior); only new projects or explicit user change adopts new law.

### Acceptance for A
- All 4 pan laws produce mathematically correct response at -inf, -3 dB, 0 dB, +3 dB pan positions per their respective specs.
- Settings persist across restarts.
- `PanLawCorrectnessTest` validates each mode against expected analytical gain curves.

---

## 7. Oversampling for Nonlinear DSP (C → A)

### Where we are
- No reusable oversampler. `MasterSafetyLimiter` and `AestraComp` process at native rate — they alias.
- `TruePeakMeter` has its own polyphase FIR but it's purpose-built (4× for peak detection only).

### Path to A
1. **`AestraAudio/include/DSP/Oversampler.h`** — reusable 2× and 4× polyphase oversampler.
   - Initialize/reset off-RT (allocates filter taps).
   - `upsample(in, scratch)` / `downsample(scratch, out)` RT-safe.
   - Quality presets: "realtime" (shorter FIR, faster) and "offline" (longer FIR, ≥ 100 dB rejection).
2. **Apply to `AestraComp`** — gain-shaping nonlinearity runs at 2× internally, downsamples on the way out.
3. **Apply to `MasterSafetyLimiter`** — gain reduction curve at 2× minimum, 4× when "high quality" is set.
4. **Audit `@/home/currentsuspect/Dev/Aestra/AestraAudio` for other nonlinear blocks** (saturation, soft-clip, waveshaping) — apply where appropriate.

### Acceptance for A
- ≥ 20 dB aliasing reduction on a 4 kHz sine through `AestraComp` at default settings, measured at FFT.
- CPU overhead ≤ 5 % per oversampled instance in realtime mode.
- `OversamplerAliasingTest` validates rejection against a chirp signal.
- `OversamplerQualityModeTest` verifies offline mode has ≥ 100 dB stopband.

---

## 8. Locking the A+ layers (keep them A+)

These are at A+ today but have no regression test. Each gets a small headless test that asserts the property they're claimed to have. **One day each, max.**

| Layer | Test | Asserts |
|-------|------|---------|
| Timing Integrity | `TimingSampleAccuracyTest` | Clip start frame matches expected to ±0 samples across 1000-block rolling window |
| Automation Smoothing | `AutomationZipperTest` | Pure parameter step produces no detectable zipper noise (-100 dBFS floor) |
| Denormal Handling | `DenormalRegressionTest` | Engine output never produces a denormal float; FTZ/DAZ verified active |
| CPU Efficiency | `CpuBudgetAssertionTest` | 64-track empty project < 5% CPU on reference hardware (i5-3337U) |
| Signal Integrity | `SignalNullTest` | Mute-bypass through master nulls input to -144 dBFS |

These five tests together form `AudioQualityRegressionSuite` — gate every PR.

---

## 9. Ordered execution plan

### Track A (you, heads-down): PDC v2
Continue per `PDC-v2-Design.md`. P4a is the next deliverable. Don't context-switch off this until P5 is green.

### Track B (parallel, smaller tasks)
Anyone else can pick up — these don't touch the RT path PDC is modifying:

1. **Week 1 (this week):** Lock the A+ layers (§8). Five tests, ~3 days total. Lowest risk, highest leverage. **Do this first — it gives PDC v2 a safety net.**
2. **Week 2:** True Peak export validation (§4). Self-contained.
3. **Week 3:** Dithering centralization (§5). Self-contained.
4. **Week 4:** Pan Law (§6). UI + settings only.

### Track A continued
5. **Weeks 2–5:** PDC v2 phases P4a → P8.
6. **Week 6:** Clipping behavior (§3) — depends on PDC v2 P4b (lookahead infra).

### Track B continued
7. **Weeks 5–7:** Oversampler (§7).
8. **Week 7:** Sinc64 optimizations (§2).

### Convergence
- **Week 8:** All grades ≥ A. Lock with `AudioQualityRegressionSuite` covering all 12 layers. Cut a "v1 Beta audio-quality" tag.

---

## 10. What this gets you

When the suite is green at all-A:

- No professional audio engineer can listen-test Aestra and call out a quality gap that has a known objective fix.
- Every audio-quality property is regression-tested, so future refactors (e.g. AudioEngine god-class split from audit §2.5) can't silently degrade.
- The marketing claim "professional-grade audio quality" becomes defensible.

---

## 11. Open questions

1. **True-peak ceiling default** — -1.0 dBTP (industry standard for streaming) vs -0.3 dBTP (more headroom)? Recommend -1.0 dBTP.
2. **Oversampling default for AestraComp** — on or off? On at 2× recommended; opt-out for ultra-low-latency monitoring.
3. **Pan law migration** — for projects saved under the implicit current law, do we tag them with `panLaw: "ConstantPower"` at first save, or rely on absence == default?
4. **Phase 4 of PDC v2 (G3 smooth recompute)** — historical-sample duplication vs crossfade vs sample-hold? Design doc r1 chose "historical-sample duplication"; revisit if it sounds bad in listening test.
