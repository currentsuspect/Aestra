# Aestra Audio Research Bench

Status: Phase 1 complete (2026-07-07). Phase 2D (full-session engine truth) complete
(2026-07-07) — see §8, which **corrects the scope of F2**: mainline session playback
does not use Sinc64Turbo at all. Phase 2E complete (2026-07-07): isolated-track
bounce kernel unified with mainline (**F6 resolved**, §8.3).
Code: `Tests/Research/` (`SignalLab.h`, `AudioMeasure.h`, `MeasurementCoreSelfTest.cpp`,
`ResamplerQualityAuditTest.cpp`, `SessionResamplingTruthTest.cpp`). CTest labels:
`research`, `audio-research`.
Run: `ctest -L research` (~5 s total, CI-safe, headless).

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
   **Phase 2D correction (§8):** the *mainline* session clip loop is the inline loop in
   `AudioEngine::renderGraph` (AudioEngine.cpp:2066+), which dispatches quality `Sinc64`
   to the **legacy exact-sinc `Sinc64Interpolator`**, not Sinc64Turbo.
   `AudioRenderer::renderClipAudio` (Sinc64 → Sinc64Turbo) serves the isolated-track
   bounce path. Both loops share the phase-accumulator design and the no-anti-aliasing
   property; their per-sample kernels — and therefore their fractional-ratio floors —
   differ.
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
**Phase 2D scope correction (F5, §8): this floor does NOT apply to mainline session
playback or full-mix export**, which use the legacy exact-sinc kernel and measured
146.5–153.9 dB through the whole engine. It DOES apply to the remaining Sinc64Turbo
consumers: the sampler (84.2 dB measured, §8.4) and AuditionEngine. (It also applied
to the isolated-track bounce — measured 87.8 dB end-to-end — until Phase 2E unified
that path's kernel with mainline; F6, resolved.)

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

Can now claim (each backed by a gated measurement in `ResamplerQualityAuditTest` or
`SessionResamplingTruthTest`):

* Clip-playback resampling is level-exact (<0.05 dB gated, <1e-5 dB measured) and
  DC-exact (≤1e-6 gated) across 44.1/48/96 kHz conversions — confirmed end-to-end
  through real sessions (§8.2: gain matches the same-rate control to 0.000000 dB).
* **Mainline session playback and full-mix export** at fractional ratios measure
  ≥140 dB (gated; 146.5–153.9 dB measured) full-band single-tone residual at 1 kHz
  (§8, legacy exact-sinc kernel). The ~88 dB Turbo floor applies to isolated-track
  bounce, sampler, and audition paths only (F2/F5/F6).
* Offline full-mix export matches realtime playback within measured bounds on the
  tested cross-rate sessions (44.1↔48 kHz pairs: nulls ≤ −164 dB RMS, maxErr 3e-8,
  gated at −120 dB; the 96 kHz pairs run the same code path but were not
  export-diffed).
* Transients do not smear ahead of their position (no measurable pre-echo beyond the
  64-frame kernel guard) and impulses land sample-accurately at the mapped position —
  confirmed through real sessions (§8.2).
* A 44.1→48→44.1 round trip of passband material nulls below −90 dB (measured −95.5).
* Upsampling image rejection at the measured 0.9-Nyquist probes: −60.5 dBc (48→96,
  Turbo; −63.3 dBc via the mainline session path) and −68.2 dBc (44.1→96). This is
  probe-specific, not a blanket guarantee — the 44.1→48 transition-band probe
  measures only −21.7 dBc (F4).

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
* ~~The audit does not run the full engine path.~~ **Closed by Phase 2D**:
  `SessionResamplingTruthTest` runs real TrackManager sessions through
  `processBlock`, both bounce flavors, PreviewEngine, and SamplerPlugin (§8).
* AuditionEngine still drives its own position loop and is not separately audited at
  session level (it shares Sinc64Turbo, so F2-class behavior is expected, unverified).
* Mono clips take a separate code path in `renderGraph`
  (`Interpolators::sincInterpolateMono`, AudioEngine.cpp:2200) that Phase 2D's stereo
  fixtures do not exercise; its kernel/quality mapping is unmeasured.
* Phase 2D fixtures are single-track, constant-BPM, clip-at-beat-0 sessions; tempo
  changes, `sourceOffset` (trimmed clips), automation-under-resampling, and multi-clip
  overlaps are not covered.
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
3. ~~**Engine-level resampling truth**~~ — **done** (Phase 2D, §8), and it changed the
   picture: see F5/F6. Its follow-ups are also done: the isolated-bounce kernel is
   unified (Phase 2E, F6 resolved) and the "~88 dB delivered" qualification is
   path-scoped in `Interpolators.h`/`ClipResampler.h`/`SINC64_TURBO_PAPER.md`.
4. **Downsampling anti-alias policy**: cost out a ratio-aware pre-filter for clip
   playback at non-1:1 rates (product decision informed by F1's numbers; see §8.5 for
   the post-Phase-2D framing).
5. **IMD audit** (dual-tone through both engines) and **group delay** (fitTone phase
   across frequencies) — both cheap on the existing infrastructure.
6. **SIMD parity** for Interpolators (scalar vs AVX2/AVX-512 dot products), mirroring
   the existing ReverbSIMDParityTest pattern.

---

## 8. Phase 2D — full-session engine truth (measured 2026-07-07)

`SessionResamplingTruthTest` re-measures resampling through the **actual shipped
paths**, not harness replicas: real `TrackManager` sessions whose clip
`AudioBufferData` declares its true source rate, rendered via
`AudioEngine::processBlock` exactly as the device callback drives it, plus the two
offline bounce flavors, `PreviewEngine`, and `SamplerPlugin`. Conditions: 1.5 s stereo
clips, amplitude 0.5, quality `Sinc64` set the way the app's settings page does,
analysis windows skip 8192 edge frames. Every number below is printed as `[MEASURE]`
by the test.

### 8.1 Which code actually resamples, per user-visible path

| User action | Code path | Sinc64 kernel used |
| --- | --- | --- |
| Realtime playback of a mismatched-rate clip | `processBlock` → `renderGraph` inline clip loop (AudioEngine.cpp:2066–2348) | **legacy `Sinc64Interpolator`** (exact double-precision Kaiser sinc, no LUT) |
| Full-mix export / bounce (`trackId = -1`) | `bounceRangeToWav` → `AudioExporter` → same `processBlock`/`renderGraph` | **legacy `Sinc64Interpolator`** |
| Isolated-track bounce (`trackId ≥ 0`) | `bounceRangeToWav` internal loop (AudioEngine.cpp:3102+) → `AudioRenderer::renderClipAudio` | ~~`Sinc64Turbo`~~ → **legacy `Sinc64Interpolator`** since Phase 2E (kernel table unified with renderGraph) |
| File-browser preview | `PreviewEngine::processRealtime` (own per-voice loop, PreviewEngine.cpp:285+) | **own Cubic Hermite** — ignores the quality setting |
| Sampler note playback (pitch ≠ root) | `SamplerPlugin::process` (SamplerPlugin.cpp:402) | **`Sinc64Turbo`** (hardcoded) |
| Clip audition | `AuditionEngine` (AuditionEngine.cpp:497) | **`Sinc64Turbo`** (not separately measured at session level) |

### 8.2 Full-session results, realtime path (Sinc64 = shipped settings default)

| Clip → session | 1 kHz gain vs control | 1 kHz residual SINAD | Near-Nyquist artifact | Identity null vs legacy replica | Length truth |
| --- | --- | --- | --- | --- | --- |
| 44.1k → 48k | 0.000000 dB | **149.3 dB** | image −21.69 dBc | −153.6 dB RMS, maxErr 8.9e-8 | exact |
| 48k → 44.1k | 0.000000 dB | **146.5 dB** | **alias −1.10 dBc** | −158.1 dB RMS, maxErr 6.0e-8 | exact |
| 96k → 48k | 0.000000 dB | **153.9 dB** | **alias −0.00 dBc** | −162.6 dB RMS, maxErr 1.5e-8 | exact |
| 48k → 96k | 0.000000 dB | **147.1 dB** | image −63.29 dBc | −161.8 dB RMS, maxErr 3.0e-8 | exact |

Control (48k in 48k): net gain exactly the equal-power center pan gain (0.7071 ±1.5e-8),
SINAD 153.9 dB (the float source's own floor), DC 1.3e-9. DC through the chain: exact
(both same-rate and 44.1→48 resampled DC clips measure 0.3535533845 vs expected
0.3535533845). Impulses land on the exact mapped frame with −60 dB spans of 1 frame
(96→48) and 79 frames (48→96), and null to −215.9 dB or below against the replica.

**Isolated vs full-session agreement:** artifact levels agree with the legacy-kernel
replica to <0.001 dB on all four pairs, and the renders null sample-for-sample
(≤9e-8). The full-mix export nulls against the realtime render to −164.4/−175.4 dB RMS
(maxErr 3e-8) with identical artifact levels — cross-rate live/export parity holds.

### 8.3 Findings

**F5 — Mainline session playback and full-mix export use the LEGACY exact-sinc
`Sinc64Interpolator`, and deliver ~147–154 dB, not the ~88 dB Turbo floor.** Phase 1
audited `Sinc64Turbo` because AudioRenderer.cpp dispatches to it; Phase 2D shows the
mainline clip loop is the *other* implementation in `renderGraph`, which computes the
Kaiser-windowed sinc per sample in double precision with per-sample normalization
(AudioEngine.cpp:2326, Interpolators.h:799). Consequences: (a) mainline playback
quality at fractional ratios is far better than F2 suggested; (b) F1 is unchanged —
neither kernel anti-aliases, and the measured alias/image levels of the two kernels
agree within 0.01 dB everywhere except the 48→96 image (legacy −63.3 vs Turbo
−60.3 dBc); (c) the SINC64_TURBO_PAPER's throughput numbers describe a kernel that
mainline playback does not run — a wording follow-up is needed there and in
`Interpolators.h`/`ClipResampler.h`, whose Phase-1 qualification ("delivered ~88 dB")
is now known to be path-specific, not global.

**F6 — RESOLVED (Phase 2E, 2026-07-07): isolated-track bounce resampled with a
different kernel than playback and full-mix export.** As found in Phase 2D:
`bounceRangeToWav(trackId ≥ 0)` rendered through `AudioRenderer::renderClipAudio` →
`Sinc64Turbo`, measuring 87.8 dB SINAD end-to-end vs 149.3 dB for the same 44.1k clip
through the full-mix path — a 61.5 dB quality split between two export flavors of
identical content (level and timing were unaffected). **Fix:** Phase 2E replaced
`renderClipAudio`'s Sinc32/Sinc64 dispatch rows (Turbo → legacy
`Sinc32Interpolator`/`Sinc64Interpolator`), making its kernel table identical to
`renderGraph`'s. The path is offline-only (single call site: the isolated-bounce loop
in `bounceRangeToWav`), so the slower exact-sinc kernels carry no realtime cost.
**Measured after:** isolated bounce 149.3 dB SINAD (was 87.8), agreeing with the
full-mix path to 0.02 dB, level-true to <0.001 dB, and the bounced file **nulls
against the full-mix realtime render at −176.0 dB RMS (maxErr 3e-8)**. The former
KNOWN INCONSISTENCY characterization gate is replaced by unification gates (SINAD
>140 dB, SINAD agreement ±3 dB, null ≤ −120 dB).

**F7 — The file-browser preview has its own Cubic resampler and ignores the
Resampling quality setting.** Measured through `PreviewEngine::processRealtime`:
44.1→48 playback shows the 21 kHz probe's image at **−7.17 dBc** vs delivered level
(matching the engine's Cubic tier: isolated −7.2 dBc, session Cubic −7.17 dBc) with
−5.2 dB HF droop at the probe; 96k preview into a 48k output folds a 30 kHz probe to
18 kHz at full delivered level (no anti-aliasing, same as F1); 1 kHz SINAD 89.5 dB
(Cubic class). This is a *preview*, so Cubic is a defensible CPU choice — but it is
now a measured, documented one instead of an accident of code, and hot near-Nyquist
imaging on previews of 44.1k content is expected behavior today.

**F8 — Sampler pitch-shift is Turbo-class and, like everything else, does not
anti-alias pitch-up.** Pitch-down −5 st: primary lands exactly at 2^(−5/12)·f, image
−66.5 dBc, 1 kHz-equivalent SINAD 84.2 dB (Sinc64Turbo hardcoded at
SamplerPlugin.cpp:402). Pitch-up +7 st of a 21.6 kHz probe folds it to 15.6 kHz at
full level — the aliasing direction, measured but not gated (pitch-up of near-Nyquist
content is a known sampler tradeoff; documented, not endorsed).

### 8.4 User-visible scenarios (updated)

* **Playing or exporting a 44.1k file in a 48k session (and any other mismatch):**
  level-exact, DC-exact, time-exact, ~147+ dB residual floor — clean, except near
  Nyquist: 44.1k content above ~20.1 kHz images at up to −21.7 dBc (F4), and
  downsampled content between the Nyquists aliases at full level (F1: 48k-in-44.1k,
  96k-in-48k).
* **Bouncing a soloed track:** since Phase 2E, identical to the full mix (nulls at
  −176 dB; previously the one path where Phase 1's ~88 dB Turbo floor reached a
  user's rendered file — F6, resolved).
* **Previewing files in the browser:** Cubic tier regardless of settings (F7); 96k
  files preview with audible-in-principle aliasing of >24 kHz content.
* **Pitching samples down** in the sampler: Turbo-class (fine). **Pitching
  near-Nyquist samples up:** aliases (F8).

### 8.5 Recommended anti-alias / unification direction

The engine-level numbers reframe Phase-2 target #4: mainline fractional-ratio SINAD is
not the problem (147+ dB); the two real quality boundaries are **downsampling aliasing
(F1)** — unchanged, present on every measured path — and **path inconsistency (F6/F7)**.
Suggested order: (1) ~~unify the isolated-bounce kernel with mainline~~ — **done**
(Phase 2E, F6 resolved); (2) decide the ratio-aware low-pass policy for downsampling
clips (F1) with the legacy kernel as the baseline — a polyphase pre-filter or
kernel-cutoff scaling both fit the existing per-voice loop, but the CPU cost on the
audio thread is the open product question; (3) fold the preview/sampler paths into
whatever policy lands, or explicitly document them as lower tiers.
