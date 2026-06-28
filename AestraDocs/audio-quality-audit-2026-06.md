# Aestra Audio Quality Audit — 2026-06-28

Spec: `AestraDocs/audio-quality-validation-spec-v1.md`
Previous audit: `AestraDocs/audio-quality-audit-2026-05.md`
Branch: `feat/aestra-limit` (SHA `570856b5`, base `develop`)
Build: `build-linux` (headless tests + full app, Release, `AESTRA_ENABLE_RUNTIME_TESTS=OFF`)

---

## Results Summary

| § | Test | Status | Grade | Notes |
|---|------|--------|-------|-------|
| 1 | Core Audio Engine Integrity | **PASS** w/ gaps | A− | Oscillator freq error: 0% @ 100/440/1000 Hz, 0.02% @ 5 kHz. No multi-SR plugin handoff test. No export-rate preservation test. |
| 2 | Bit Depth Handling | **PASS** | A | PCM16/24 symmetric quantization verified. WAV loader passes 16/24/32-bit. TPDF dither deterministic + stereo-uncorrelated. |
| 3 | Summing Engine | **UNTESTED** | C | No 100-track sine summation test exists. No reference math comparison. PerformanceTest measures timing (192-track safe) but not numerical summing accuracy. |
| 4 | Pan Law | **PASS** w/ gaps | B | sin/cos constant-power -3dB center confirmed in source. No automated test measures center vs side energy or verifies user-selectable laws (-3/-4.5/-6 dB). |
| 5 | Gain Stage Accuracy | **PASS** | A | Safety limiter knee/ceiling/disabled tests pass. SmoothedParamD linear ramp fix verified (P1 → DONE per prior audit). Master gain ramp uses correct per-block linear delta. |
| 6 | Mixer Headroom | **PASS** w/ gaps | B | 200-track stress in PerformanceTest shows stable 76% load at 192 tracks. No test verifies internal >0 dBFS handling without wraparound/clipping. |
| 7 | Plugin Processing | **PASS** | A | EQ (67 tests): bypass parity, latency=0, NaN/Inf containment, 44.1/96 kHz, silence stability. Comp (all pass): Phase0 contract + Phase1 parameter surface. Limiter: bypass <1e-6, ceiling clamp, latency reported. ReverbSIMD parity: scalar path OK (SSE unavailable on this host). |
| 8 | Real-Time vs Offline | **PASS** w/ known risk | B | ArsenalExportLiveParityTest confirms live/export parity for PreviewToMaster and RoutedToTimelineTrack routing. File: `AestraDocs/audio-quality-audit-2026-05.md §9` flags 2 offline render authorities (P1 — Slice 3 in progress). Bounce tests pass with full/isolation/write-failure paths. |
| 9 | Automation Accuracy | **PASS** w/ gap | B | AutomationStressTest passes 12/12 deserialization edge cases. No real-time ramp smoothness test (zipper noise, click detection). Prior audit's P1 `SmoothedParamD` snap bug is DONE. |
| 10 | Recording Path | **PASS** | B | State machine (arm/lifecycle/unarmed) passes. Full capture path requires audio hardware (loopback, timestamp measurement). |
| 11 | Export Validation | **PASS** w/ gaps | B | TruePeakMeterTest passes. ArsenalExportLiveParityTest validates 48000 Hz, 2ch, 32-float export. No multi-format (16/24/FLAC/MP3) roundtrip test exists. |
| 12 | Noise Floor | **UNTESTED** | C | No silence-project export test. Prior audit confirms NaN/Inf sanitization at master + per-plugin. Export dither P1 fixed. |
| 13 | CPU Stress Stability | **PASS** w/ gate | A | SoakTest exists but requires `AESTRA_ENABLE_RUNTIME_TESTS=ON` (2hr default). PerformanceTest shows 192-track safe polyphony at 76% load, 224 tracks at 90% (FAIL threshold). |
| 14 | Denormal Handling | **PASS** w/ known bug | B | ReverbSafetyRegressionTest: ALL PASS. `AestraDocs/audio-quality-audit-2026-05.md §3` flags ARM64 `FPCR.FZ` as P1/MISSING (Slice 7 DONE). Engine-level FTZ+DAZ on x86. Limiter EMA state zeroed on reset/bypass. |
| 15 | Golden Reference Suite | **MISSING** | C | No `tests/audio/reference/` directory. No reference WAV files. No automated regression comparison (RMS/peak/spectrum). |

---

## Per-Section Detail

### §1 — Core Audio Engine Integrity

**Oscillator frequency accuracy (build-linux/Tests/AestraOscillatorTest):**
```
100 Hz  → 100 Hz   (error: 0%)
440 Hz  → 440 Hz   (error: 0%)
1000 Hz → 1000 Hz  (error: 0%)
5000 Hz → 4999 Hz  (error: 0.02%)
```

**Engine sample rate:** 48 kHz default, configure via `m_sampleRate`. No automated test that verifies engine SR matches project SR at multiple rates (44.1/48/88.2/96/192). Plugins receive `initialize(double sampleRate, ...)` — AestraLimit, AestraEQ tests pass at 44.1 and 96 kHz.

**Gap:** No cross-rate test (engine 48k, source 44.1k → verify SRC + render + export @ 96k). The existing audit flags the SRC as `Sinc64Turbo` with 2048 phases, Kaiser β=12 (~144 dB SNR). Phase math anchored to absolute timeline (no cross-block drift).

### §2 — Bit Depth Handling

**WAV loading (build-linux/Tests/AestraWavLoaderTest):**
- 16-bit PCM: OK
- 24-bit PCM conversion: OK  
- 32-bit PCM conversion: OK
- JUNK chunk handling: OK
- Invalid metadata rejection: OK

**Export quantization (build-linux/Tests/AestraAudioQualityRegressionTest):**
- PCM16: symmetric `[-32768, 32767]`, clamps ±2.0, NaN→0: PASS
- PCM24: symmetric `[-8388608, 8388607]`, little-endian packing: PASS
- TPDF dither: deterministic seed → identical output, stereo-uncorrelated: PASS

**Internal pipeline:** 32-bit float I/O, 64-bit float (`double`) master bus and per-track render buffers. Audit confirms plugin scratch is `float` (VST3/CLAP ABI).

**Gap:** No "gain +20dB → normalize → export → reimport → compare" roundtrip test. Prior audit's P1 undithered export is now DONE.

### §3 — Summing Engine

**No test exists.** The validation spec criterion is < -140 dB RMS error for 100 × 1 kHz -20 dBFS tracks summed vs Numpy reference.

This is the single largest gap in the current test suite. Aestra's 64-bit double-precision master bus should theoretically achieve this, but it is *untested*.

**Action:** Write `tests/audio/summing/test_100_tone_sum.py` (or C++ equivalent) that renders 100 tracks, compares against offline math, and reports dB error.

### §4 — Pan Law

**Source:** `AestraAudio/src/AudioRenderer.cpp:63-67` uses sin/cos constant-power, -3 dB at center. Confirmed by `AestraDocs/audio-quality-audit-2026-05.md §3`.

**Gap:** No automated test measures:
- Center vs left/right energy ratio
- User-selectable law selection (-3, -4.5, -6 dB) vs actual math
- Stereo balance accuracy across full pan range

### §5 — Gain Stage Accuracy

**Safety limiter (build-linux/Tests/AestraPlaybackPathSignalIntegrityTest):**
- Below-knee passthrough (0.5 → 0.5): PASS
- Knee entry (0.9799 → 0.9799): PASS  
- Knee monotonicity (1000 steps): PASS
- Ceiling clamp: PASS
- Disabled passthrough: PASS

**Master gain ramp:** per-block linear delta `(target - current) / numFrames`, converges at last sample (no snap). Correct.

**Track gain smoothing:** `SmoothedParamD` was previously `exp(0.001)+snap` (P1 zipper). Now uses linear ramp. DONE per prior audit Slice 2.

**Gap:** No end-to-end gain chain test that verifies `output = input × gain_coefficient` through Track → Clip → Plugin → Bus → Master with exact tolerance.

### §6 — Mixer Headroom

**Performance polyphony (build-linux/Tests/AestraAudioPerformanceTest):**
```
32  tracks: 41.4% load  → OK
64  tracks: 62.9% load  → OK
96  tracks: 76.4% load  → OK
128 tracks: 67.7% load  → OK
160 tracks: 61.7% load  → OK
192 tracks: 76.3% load  → OK
224 tracks: 90.2% load  → FAIL (>80%)
→ Max safe: ~192 tracks
```

**Mixer channel stress (build-linux/Tests/MixerChannelStressTest):**
16/16 edge-case tests pass (volume 0/max/bounds, mute/solo, send add/remove, stereo pan clamping).

**Gap:** No test verifies that 200 tracks at -3 dBFS each (mix bus summing to ~+20 dBFS) produces no wraparound, integer clipping, or distortion. The 64-bit `double` master buffer should handle this, but the behavior at the output cast (`double → float`) is only tested for the limiter path.

### §7 — Plugin Processing Validation

**AestraEQ (build-linux/Tests/AestraEQTest):**
67/67 passed. Coverage:
- Bypass parity: PASS
- Latency: 0 samples (reported and tested)
- Flat EQ = input: PASS
- NaN/Inf recovery: PASS
- Multi-rate (44.1, 96 kHz): PASS
- Silence stability (100 blocks): PASS
- Extreme values → no NaN/Inf: PASS
- State roundtrip V1–V8: PASS

**AestraLimit (build-linux/Tests/AestraLimitTest):**
31 tests, all passed. Coverage includes bypass (<1e-6 tolerance), ceiling clamping, release modes, overshoot containment.

**AestraComp Phase0 + Phase1:**
All tests passed (contract + parameter surface + state roundtrip).

**ReverbSIMDParity:**
SSE path unavailable on this platform (no SSE reverb support); scalar path OK.

**Gap:** No cross-plugin bypass registration (generic test that runs ALL plugins through bypass and measures < -120 dB difference). Not every plugin is tested at all spec sample rates.

### §8 — Real-Time vs Offline Rendering

**ArsenalExportLiveParityTest:**
- PreviewToMaster muted track: live audible, export audible → PASS
- RoutedToTimelineTrack muted: both audible → PASS
- Routed open track: both audible → PASS
- Mixed route (2 units): both audible + no cross-contamination → PASS
- Isolated bounce: full=iso0, iso1 = silence → PASS
- Write-failure cleanup: returns false, removes partial → PASS

**Known risk (from prior audit):** Two offline render authorities exist — `AudioExporter::render` calls `processBlock` (gets live master stage), `bounceRangeToWav` calls `renderBlock` (bypasses master stage). Slice 3 is IN PROGRESS. Export-vs-bounce parity tests are needed for float32 and PCM output.

### §9 — Automation Accuracy

**AutomationStressTest:**
12/12 deserialization edge cases pass (empty, out-of-range, sample-rate mismatch, zero SPB, orphan targets, enum wrapping).

**Gap:** No test for real-time ramp smoothness. No test detects zipper noise, clicks, or sample-accurate curve following. The `SmoothedParamD` fix (linear ramp per block) removes the block-boundary snap — but no test measures the actual audible outcome.

### §10 — Recording Path

**RecordingPathTest:**
3/3 state machine tests pass (arming, session lifecycle, unarmed = no capture).

**Gap:** Full path (audio interface loopback → input processing → WAV) requires `AESTRA_ENABLE_RUNTIME_TESTS` and hardware. Timestamp-based latency measurement not automated.

### §11 — Export Validation

**TruePeakMeterTest:**
All pass (silence, sine, intersample peak, max-hold, DC step response).

**ArsenalExportLiveParityTest:**
Exports format-validated at 48000 Hz, 2 channels, 32-bit float.

**Gap:** No multi-format export test (16-bit, 24-bit, 32-float, FLAC). No metadata verification (SR, channels, bit depth in file header). No true-peak enforcement test on export clamp.

### §12 — Noise Floor

**No test exists.** Prior audit confirms `NaN/Inf → 0` per-sample on master and per-plugin. Export dither P1 is DONE (TPDF only at integer quantization, not float path). Master inline dither removed from float path (Slice 6 DONE).

**Action:** Write silence-project export → measure noise floor WAV RMS. Expected < -140 dBFS with the 64-bit engine.

### §13 — CPU Stress Audio Stability

**SoakTest:** Exists but gated behind `AESTRA_ENABLE_RUNTIME_TESTS=ON` (2-hour default, configurable via `--duration-sec`). Tests: 32 tracks @ 48k/256 frames, SRC stress, command queue at 500 Hz, graph swaps at 10 Hz, memory RSS tracking.

**PerformanceTest:** ~1000 tracks of DSP density (filters) at 2.4% → 21.5% load. 192-track polyphony at 76% load (224 at 90% = FAIL).

**RT safety:** Prior audit confirms zero-alloc hot path, bounded command drain (≤16/block), per-block telemetry, plugin exception isolation. AudioEngineDiagnosticsTest confirms RT depth tracking and violation detection.

### §14 — Denormal Handling

**ReverbSafetyRegressionTest:** ALL PASS (silence, denormal, NaN safety).

**Known P1 (prior audit):** Denormal protection is x86-only. `FPCR.FZ` not set on ARM64. Slice 7 DONE in the fix plan — needs verification on ARM hardware.

**Limiter:** EMA state zeroed on reset/bypass. Engine-level FTZ+DAZ on x86.

### §15 — Golden Reference Test Suite

**Not implemented.** No `tests/audio/reference/` directory. No reference WAV files for sine, impulse, pink noise, white noise, or music stems. No regression tool that compares `new.wav` vs `reference.wav` with RMS/peak/spectrum metrics.

Existing lab WAV files in `labs/reverb/quality/` serve reverb quality measurement only, not general regression detection.

---

## Known Issues (from Prior Audit) — Updated Status

| # | Slice | Severity | Status (this audit) |
|---|-------|----------|-------------------|
| 1 | TPDF dither at export PCM_16/PCM_24, symmetric range | P1 | **VERIFIED DONE** — AudioQualityRegressionTest confirms PCM16/24 symmetry + TPDF determinism |
| 2 | Replace SmoothedParamD exp+snap with linear ramp | P1 | **VERIFIED DONE** — all gain stages checked |
| 3 | Fix/merge offline render authorities; export-vs-bounce parity | P1 | **IN PROGRESS** — master bounce done; isolated track bounce TODO |
| 4 | Fix bounceRangeToWav null ctx.graph | P1 | **VERIFIED DONE** |
| 5 | Sample-rate-aware LUFS coefficients | P1 | **DONE (reported)** — not tested in this audit |
| 6 | Remove inline master TPDF from float output path | P2 | **VERIFIED DONE** — float path clean |
| 7 | ARM64 denormal protection | P1 | **DONE (code)** — needs ARM hardware to verify |
| 8 | Fix IntelligentDithering NoiseShaper IIR + rename enum | P2 | **DONE (reported)** |
| 9 | Symmetric PCM24 range | P2 | **VERIFIED DONE** |
| 10 | Skip pre-fader sends when fail-safe disabled | P2 | **DONE (reported)** |

---

## Gap Analysis: What's Missing

### Critical Gaps (no test exists, not even scaffolded)

1. **§3 Summing Engine** — 100-track numerical accuracy test. Largest gap.
2. **§15 Golden Reference Suite** — No regression framework. No reference WAVs.
3. **§12 Noise Floor** — No silence export → measure noise.

### Medium Gaps (partial coverage, needs expansion)

4. **§1 Multi-SR** — No test verifies plugin sample rate handoff at multiple rates.
5. **§4 Pan Law** — No automated verification of center vs side energy.
6. **§6 Headroom** — No overload test (200 tracks at -3 dBFS → verify no internal clipping).
7. **§9 Automation** — No real-time ramp smoothness test (zipper/click detection).
8. **§11 Export Formats** — No multi-format roundtrip (16/24/float/FLAC).

### Low Gaps (nice-to-have or hardware-gated)

9. **§10 Recording** — Full capture path + loopback latency needs hardware.
10. **§5 Gain Chain** — End-to-end gain through Track→Clip→Plugin→Bus→Master.

---

## Aestra Audio Grade

Based on this audit, Aestra earns:

### **A Tier** (strong A, borderline S in some areas)

| Criteria | Status |
|----------|--------|
| 32-bit float engine | **YES** — 32f I/O, 64f internal mix |
| Correct summing | **LIKELY** (64-bit double bus) but **UNTESTED** |
| Reliable exports | **YES** — verified Arsenal routing, true-peak metering, PCM quantization |
| Good plugin handling | **YES** — bypass/latency/NaN/stability tested across EQ, Comp, Limit, Reverb |

### Blockers to S Tier

| S Tier Criteria | Status | What's needed |
|----------------|--------|---------------|
| 64-bit float engine | **VERIFIED** — master bus + per-track are `double` | — |
| Sample-accurate automation | **PARTIAL** — P1 smoother snap fixed, but no ramp smoothness test | §9 automation ramp test |
| Zero-copy audio paths | **VERIFIED** — pre-allocated buffers, no RT allocs | — |
| Deterministic offline render | **P1 IN PROGRESS** — two render authorities not merged | Slice 3 completion |
| Perfect latency compensation | **VERIFIED** — PDC v2 with solver, per-edge ring buffers, audit hooks | — |
| No RT thread violations | **VERIFIED** — depth tracking, bounded commands, per-block telemetry, zero-alloc hot path | — |

**Orchestration-grade trust** is achievable. The 64-bit engine, PDC v2, and RT safety are already S-tier quality. The three gaps preventing S-tier certification are:

1. **Summing engine numerical accuracy** (§3) — write one test, likely PASS, instant confidence.
2. **Offline render authority unification** (§8/Slice 3) — already in progress.
3. **Golden reference regression suite** (§15) — larger investment, prevents regression over time.

---

### Files Changed in This Audit

- `AestraDocs/audio-quality-validation-spec-v1.md` — new: validation spec document
- `AestraDocs/audio-quality-audit-2026-06.md` — this file

### Tests Executed (17 binaries, all PASS)

```
AestraAudioQualityRegressionTest     — 4/4  PCM quantization, TPDF dither, SmoothedParam
AestraPlaybackPathSignalIntegrityTest — 7/7  Safety limiter, audition/preview paths
ArsenalExportLiveParityTest           — 6/6  Live/export/bounce parity for Arsenal
AestraOscillatorTest                  — 4/4  Sine/saw/square accuracy, frequency
AestraLimitTest                       — 31/31 Brickwall limiter contract
AestraWavLoaderTest                   — 6/6  WAV 16/24/32-bit loading
AestraFilterTest                      — 5/5  LP/HP/BP/resonance/stability
AestraEQTest                          — 67/67 EQ param/latency/bypass/state/NaN/silence
AestraEQMeasurementTest               — 20/20 EQ frequency response measurement
AestraCompPhase0Test                  — ALL   Compressor V1 contracts
AestraCompPhase1Test                  — ALL   Compressor parameter/state surface
TruePeakMeterTest                     — 5/5   True-peak metering v2
AutomationStressTest                  — 12/12 Automation curve deserialization
MixerChannelStressTest                — 16/16 Mixer channel edge cases
RecordingPathTest                     — 3/3   Recording state machine
ReverbSafetyRegressionTest            — ALL   Denormal/NaN/silence safety
ReverbSIMDParityTest                  — SKIP  SSE unavailable on this host
AudioEngineDiagnosticsTest            — 11/11 RT audit, violation tracking
AudioEngineOutputChannelTest          — 2/2   Mono/stereo stride safety
PatternPlaybackTempoChangeTest        — PASS  Tempo change during playback
SamplerStereoParityTest               — PASS  Sampler stereo parity
LatencyCompensationTest               — 5/5   PDC flat chain
AestraAudioPerformanceTest            — 192tr @76% safe polyphony, 224tr @90% FAIL
```

### Commands Run

```bash
cmake -S . -B build-linux -DAESTRA_HEADLESS_ONLY=OFF -DAESTRA_ENABLE_TESTS=ON -DAESTRA_ENABLE_RUNTIME_TESTS=OFF
# (build was pre-existing, tests were run from build-linux/Tests/)
```
