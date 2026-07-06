# Aestra Audio Research Bench

Status: Phase 1 complete (2026-07-07).
Code: `Tests/Research/` (`SignalLab.h`, `AudioMeasure.h`, `MeasurementCoreSelfTest.cpp`,
`ResamplerQualityAuditTest.cpp`). CTest labels: `research`, `audio-research`.
Run: `ctest -L research` (~1.5 s total, CI-safe, headless).

The Research Bench is a test-side scientific measurement layer. It is not a product
feature, does not touch production DSP, and adds nothing to the audio callback path.
Its job is to replace adjectives ("mastering grade") with numbers, and to keep those
numbers pinned by regression gates whose thresholds cite the measurement that
justified them.

---

## 1. What is measured, and how

All analysis runs in double precision over float buffers; every generator and every
measurement is a pure deterministic function of its arguments (noise uses a fixed-seed
xorshift64\*, never `<random>` distributions).

| Measurement | Definition (implementation: `Tests/Research/AudioMeasure.h`) |
| --- | --- |
| Peak | max \|sample\| over a frame window |
| RMS | sqrt(mean(sample²)); dBFS via 20·log10 |
| DC offset | arithmetic mean over the window |
| Null difference | per-sample diff of two buffers: max abs error, RMS error (linear + dB), first mismatching frame/channel, length-mismatch flag |
| Tone amplitude / phase | 3-parameter least squares (a·sin + b·cos + c) at a **known** frequency, solved from the 3×3 normal equations. Exact for integer-cycle windows |
| Residual "SINAD" | fitted-tone RMS ÷ post-subtraction residual RMS, in dB. The residual is full-band (noise + distortion + aliasing), so for single-tone stimuli this is a THD+N-style figure. It is **not** a spectrum-analyzer SINAD; we deliberately do not quote it as one |
| THD | per-harmonic tone fits at k·f0 (below Nyquist only): sqrt(Σ harmonic²)/fundamental. Aliased harmonics are not attributed |
| Impulse stats | peak value/position, contiguous span above a relative threshold (default −60 dB), pre-span RMS (pre-ring), post-span RMS (tail) |
| Stereo correlation | Pearson correlation of L vs R, mean-removed; defined as 0 when either channel has ~zero variance |

Failure output is forensic by construction: sample rate, channel count, frame counts,
peaks, RMS, max error, first mismatching frame/channel, and an expected/actual sample
snippet around the mismatch. No failure requires listening to an audio file.

### Self-validation

`MeasurementCoreSelfTest` (50 checks) proves the measurement layer against closed-form
math before it measures any DSP: sine RMS = A/√2, square-wave THD against the discrete
Fourier series aₙ = A/(32·sin(πn/128)) for a 128-sample period, dual-tone component
separation, impulse localization, correlation ±1/0 cases, injected-corruption
localization. Measured float-quantization floor of the tone fit: **154.9 dB** on a
float-rounded sine — this is the bench's own noise floor; nothing below it is claimed.

---

## 2. The two resamplers in AestraAudio

This audit measured **both** resampling engines, because they are different code with
different behavior:

1. **Production clip-playback path** — the `Interpolators` family
   (`AestraAudio/include/DSP/Interpolators.h`: Cubic, Sinc8/16/32/64Turbo) driven by a
   `phase += ratio` accumulator. Call sites: `AudioRenderer.cpp` (clip mixing),
   `AuditionEngine.cpp`, `SamplerPlugin.cpp`. Kernel cutoff sits at the **source**
   Nyquist; there is no ratio-aware anti-alias filtering.
2. **Streaming `SampleRateConverter`** (`DSP/SampleRateConverter.h`, polyphase,
   ratio-aware cutoff — it does anti-alias when downsampling). **As of this audit it
   has zero production call sites** (tests/benchmarks only).

The audit harness replicates the AudioRenderer inner loop exactly (function-pointer
dispatch + phase accumulator), so the interpolator numbers below describe what clip
playback actually does. Conditions: 1 s stereo signals, amplitude 0.5, Release x86-64
Linux build, analysis windows skip 256 edge frames. All numbers from
`ResamplerQualityAuditTest` (labels `[MEASURE]`/`[REF]` in its output).

---

## 3. Resampler audit results (measured 2026-07-07)

### 3.1 Production interpolator path, Sinc64Turbo

| Pair | DC error | 1 kHz gain err | 1 kHz residual SINAD | Near-Nyquist artifact | Impulse span (−60 dB) | Impulse tail RMS |
| --- | --- | --- | --- | --- | --- | --- |
| 44.1→48 | 4.5e-9 | < 1e-5 dB | 87.8 dB | image −21.7 dBc (probe 21 kHz) | 45 frames | 5.2e-6 |
| 48→44.1 | 2.5e-9 | < 1e-5 dB | 88.4 dB | **alias −1.1 dBc** (probe 23 kHz) | 37 frames | 3.0e-6 |
| 48→96 | 3.0e-8 | < 1e-5 dB | 89.9 dB | image −60.5 dBc (probe 21.6 kHz) | 79 frames | 4.4e-6 |
| 96→48 | < 1e-9 | < 1e-5 dB | 154.2 dB | **alias −0.0 dBc** (probe 30 kHz) | 1 frame | ~0 |
| 44.1→96 | 4.2e-9 | < 1e-5 dB | 87.9 dB | image −68.2 dBc (probe 19.845 kHz) | 86 frames | 5.3e-6 |

Additional measurements:

* **Round trip** 44.1→48→44.1 (Sinc64 both ways, 1 kHz): interior RMS error
  **−95.5 dB**, max abs error 5.25e-5.
* **Transient burst**: pre-burst smear beyond the 64-frame guard is 0.0 (3.1e-10 on
  44.1→96); body RMS preserved within 0.15%.
* **Impulse position**: lands on the rate-mapped frame ±1 in all pairs.
* **Cubic contrast** (44.1→48): 1 kHz gain error −0.000045 dB, SINAD 89.5 dB, but the
  23.1 kHz image is only **−7.2 dBc** — cubic is level-true at low frequencies and much
  dirtier near Nyquist.

### 3.2 Streaming SampleRateConverter, quality `Sinc64`

| Pair | Length deficit (of expected) | DC error | 1 kHz gain err | 1 kHz residual SINAD | Near-Nyquist artifact |
| --- | --- | --- | --- | --- | --- |
| 44.1→48 | 34 frames | 3.7e-9 | 0.0002 dB | **44.3 dB** | image −41.4 dBc |
| 48→44.1 | 29 frames | 3.2e-9 | 0.0005 dB | **39.7 dB** | alias −57.8 dBc |
| 48→96 | 63 frames | < 1e-9 | < 1e-5 dB | 127.0 dB | image −72.5 dBc |
| 96→48 | 15 frames | 3.0e-8 | < 1e-4 dB | 141.7 dB | alias −117.1 dBc |
| 44.1→96 | 69 frames | 1.1e-9 | 0.0002 dB | **43.9 dB** | image −47.6 dBc |

Length deficits are single-shot history priming and match `getLatency()` (32 input
frames) × ratio; they are not data loss in streaming use.

### 3.3 External references, same probes (diagnostics, not gated)

| Pair | libsamplerate SINC_BEST | soxr VHQ | Aestra interp (Sinc64) | Aestra streamSRC (Sinc64) |
| --- | --- | --- | --- | --- |
| 44.1→48 (image 23.1 kHz) | −73.7 dBc | −78.4 dBc | −21.7 dBc | −41.4 dBc |
| 48→44.1 (alias 21.1 kHz) | −163.7 dBc | −200.7 dBc | −1.1 dBc | −57.8 dBc |
| 48→96 (image 26.4 kHz) | −83.9 dBc | −88.1 dBc | −60.5 dBc | −72.5 dBc |
| 96→48 (alias 18 kHz) | −143.5 dBc | −160.0 dBc | −0.0 dBc | −117.1 dBc |
| 44.1→96 (image 24.255 kHz) | −81.9 dBc | −82.9 dBc | −68.2 dBc | −47.6 dBc |

These columns are an external context anchor, not a like-for-like product comparison:
the references are offline converters with long filters and no real-time constraint,
while the production path is a real-time per-sample interpolator. They run only when
libsamplerate/soxr are present at configure time (optional, same pattern as
`ResamplerWarTest`).

---

## 4. Findings

**F1 — Production downsampling does not anti-alias (characterized, by design).**
The interpolator kernel cuts off at the *source* Nyquist, so when a clip plays into a
lower engine rate (48 kHz clip in a 44.1 kHz session; 96→48), content between the two
Nyquists folds back essentially unattenuated: measured −1.1 dBc (48→44.1) and −0.0 dBc
(96→48). For typical program material the energy that high is small, but this is a
real quality boundary and the biggest measured gap vs external references. The audit
pins this behavior with a characterization gate so any future anti-aliasing change is
a deliberate, test-updating decision. Whether to add ratio-aware filtering is a
product/CPU decision, out of scope for this phase (RT budget: the path runs per-voice
on the audio thread).

**F2 — Sinc64Turbo's real-world fractional-ratio floor is ~88 dB, not the header's
"~144dB SNR".** Measured 1 kHz residual SINAD: 87.8–89.9 dB at fractional ratios vs
154.2 dB at exact 2:1 (where outputs land on input samples). The floor matches the
2048-phase nearest-phase LUT quantization (worst phase error 1/4096 sample ≈ −89 dB at
1 kHz). The 144 dB figure describes the kernel design target (stopband), not the
delivered fractional-ratio SINAD. 88 dB is still ~16 bits — clean — but the header
comment overstates it; phase-error-induced residual also grows with signal frequency
(not yet swept — see blind spots).

**F3 — Streaming `SampleRateConverter` delivers only ~40–44 dB SINAD at fractional
ratios** (39.7–44.3 dB measured; 127–142 dB at integer ratios), far below the
"mastering grade"/"reference grade" wording in its header. Its 256-phase bank without
inter-phase interpolation is the prime suspect but under-predicts the measured loss
(~65–70 dB would be expected from phase rounding alone at 1 kHz); the mechanism is
unattributed and is the top Phase-2 target. **No user hears this today — the class has
no production call sites** — but it must not be wired into export/recording paths in
its current state.

**F4 — 44.1↔48 imaging sits in a wide transition band.** The 44.1→48 image probe (21
kHz, 0.952 of source Nyquist — the only placement whose image is observable below the
48 kHz output Nyquist) measured −21.7 dBc where libsamplerate/soxr manage −73.7/−78.4
dBc on the identical probe. At 0.9 Nyquist (the 96 kHz pairs) the kernel reaches −60.5
to −68.2 dBc. In plain terms: the last ~10% of the band below Nyquist is not cleanly
protected by the 64-tap kernel.

**What measured clean:** DC preservation (≤3e-8 error everywhere), passband level
accuracy (<1e-5 dB interp, <5e-4 dB streaming), impulse position exactness, bounded
impulse spread with no post-ring, zero acausal transient smear beyond the kernel
half-width, and a −95.5 dB round trip 44.1→48→44.1.

No production code change was made: F1 is a design boundary requiring a product
decision, F2 is a documentation-accuracy issue, F3 is in dead code, F4 is a filter-
design tradeoff. Nothing met the bar of "test proves a defect in shipped behavior".

---

## 5. Claims Aestra can and cannot make

Can now claim (each backed by a gated measurement in `ResamplerQualityAuditTest`):

* Clip-playback resampling is level-exact (<0.05 dB gated, <1e-5 dB measured) and
  DC-exact (≤1e-6 gated) across 44.1/48/96 kHz conversions.
* Clip-playback resampling at fractional ratios has a ≥84 dB (measured ~88 dB)
  full-band single-tone residual floor at 1 kHz, and ~154 dB at integer ratios.
* Transients do not smear ahead of their position (no measurable pre-echo beyond the
  64-frame kernel guard) and impulses land sample-accurately at the mapped position.
* A 44.1→48→44.1 round trip of passband material nulls below −90 dB (measured −95.5).
* Upsampling image rejection is at least −55/−60 dBc at 0.9-Nyquist probes.

Cannot claim (and must not, until measured otherwise):

* "Mastering-grade/144 dB" resampling as a blanket statement (contradicted by F2/F3).
* Any anti-aliasing quality for downsampled clip playback (F1: there is none).
* Transparent near-Nyquist upsampling for 44.1→48 (F4: −21.7 dBc at 21 kHz).
* Any THD+N/SINAD figure in the spectrum-analyzer sense (the bench measures a
  full-band least-squares residual, stated as such).
* Anything about frequency-dependent behavior above 1 kHz probes / below near-Nyquist
  probes, group delay, or IMD — not yet measured.

---

## 6. Known blind spots

* Single-frequency probes only (1 kHz passband + one near-Nyquist probe per pair). No
  frequency-response sweep of the resamplers yet (the sweep generator exists and is
  validated, but no sweep-based analysis is wired up).
* No windowed-FFT/spectrum analysis — all frequency-domain statements come from
  known-frequency least-squares fits. Good for targeted probes, blind to unexpected
  spurs at unpredicted frequencies (the full-band residual would catch their energy
  but not name them).
* Phase response / group delay not measured (fitTone returns phase; nothing asserts
  it). No claim is made.
* IMD (dual-tone) not applied to the resamplers yet, only validated in the self-test.
* The audit drives `Interpolators` directly through a replica of the AudioRenderer
  loop; it does not run the full engine path (TrackManager session with a
  mismatched-rate clip). Engine-level SRC truth at the session level is covered
  separately (and more coarsely) by `SampleRateBufferTruthTest`.
* AuditionEngine and SamplerPlugin share the interpolator family but drive positions
  with their own loops; those loops are not separately audited.
* One dev machine, Release x86-64 Linux. SIMD variants (AVX2/AVX-512/NEON sinc paths)
  not cross-checked by this test (ReverbSIMDParityTest-style parity for interpolators
  is a gap).

## 7. Recommended Phase 2 targets

1. **Attribute F3** (streaming SRC ~40 dB fractional-ratio floor): instrument the
   polyphase index math; check whether phase truncation, ratio accumulation, or
   history handling dominates; decide fix-or-retire (it has no callers).
2. **Frequency-dependent SINAD sweep** for the production path: phase-quantization
   error grows with frequency, so quantify SINAD vs frequency (100 Hz–20 kHz), not
   just at 1 kHz — this decides whether ~88 dB @1 kHz is ~68 dB @10 kHz.
3. **Engine-level resampling truth**: play a mismatched-rate clip through a real
   TrackManager session and re-run the same measurements end-to-end (extends
   SampleRateBufferTruthTest with research-bench metrics).
4. **Downsampling anti-alias policy**: cost out a ratio-aware pre-filter for clip
   playback at non-1:1 rates (product decision informed by F1's numbers).
5. **IMD audit** (dual-tone through both engines) and **group delay** (fitTone phase
   across frequencies) — both cheap on the existing infrastructure.
6. **SIMD parity** for Interpolators (scalar vs AVX2/AVX-512 dot products), mirroring
   the existing ReverbSIMDParityTest pattern.
