# Audio Integrity Infrastructure — Survey & Plan

**Status:** in progress · **Owner:** agent session 2026-07-06 · **Baseline:** develop @ 80fbbf87

Mission: make it impossible for future PRs to silently change the sound, break
render/playback parity, add unsafe audio-thread behavior, or hide timing
regressions. This document records what already exists (with source
locations), the gaps, and the PR plan. Claims below are backed by the cited
files — nothing here is aspirational.

---

## 1. What already exists (survey findings)

### Render paths — the ground truth

- **Realtime path:** `RtAudioDriver::rtAudioCallback` (`AestraAudio/src/Linux/RtAudioDriver.cpp:248`)
  → `AestraAudioController::audioCallback` (`Source/Core/AestraAudioController.cpp:407`)
  → `AudioEngine::processBlock` (`AestraAudio/src/Core/AudioEngine.cpp:593`).
  After the engine block, the device callback **additively mixes** input
  monitoring (`TrackManager::mixInputMonitoring`) and browser preview
  (`PreviewEngine::processRealtime`) into the same output buffer
  (`AestraAudioController.cpp:429-437`).
- **Offline export path:** `AudioEngine::bounceRangeToWav` (`AudioEngine.cpp:3096`)
  → `AudioExporter::render` (`AestraAudio/src/IO/AudioExporter.cpp:83`), which
  drives **the same `AudioEngine::processBlock`** in a loop with metronome and
  audition disabled and transport forced playing (`AudioExporter.cpp:206-233`).
  Isolated track bounce (trackId ≥ 0) still uses an older renderBlock path
  (`AudioEngine.cpp:3120`, marked TODO in source).
- Consequence: playback/export parity is testable by driving `processBlock`
  directly ("realtime style") vs `bounceRangeToWav` on the same session.

### Existing tests (registered in `Tests/CMakeLists.txt`)

| Test | What it actually covers | Label |
|---|---|---|
| `GoldenReferenceTest` (`Tests/AestraAudio/GoldenReferenceTest.cpp`) | Analytic golden: 3 sines + impulse through engine `processBlock`, RMS/peak/correlation vs computed reference | `audio;quality;golden;s-tier` |
| `ExportBounceParityTest` | Master bounce ≈ sum of isolated track bounces (bounce self-consistency, **not** realtime-vs-offline) | `audio;quality;export;s-tier` |
| `AudioPurityAuditTest` | End-to-end purity truth test (no hidden master processing) | `audio;quality;audit;purity;s-tier` |
| `PreviewAuditionGainParityTest` | Preview/audition gain behavior | `audio;preview;audition;gain;regression` |
| `TrackManagerMasterDuckingAuditTest` | Preview→master ducking behavior (owner's audit, PR #419) | `audio;quality;regression;playback;metering` |
| `OfflineRenderRegressionTest` + `Tests/Headless/*` | Offline renderer, PDC suite | headless |
| `AudioEngineDiagnosticsTest` | Pokes `recordRTViolation` manually | `audio;diagnostics;rt-safety` |
| `SampleRateConverterTest` | SRC unit test — **gated experimental/unstable, not in default CI** | `experimental;unstable` |

### RT-safety infrastructure

- `ScopedRealtimeAudioThread` marks the audio thread; **placed in
  `processBlock`** (`AudioEngine.cpp:594`), so both realtime and offline export
  run under the guard.
- `reportRealtimeMisuse(api)` is called by ~20 non-RT APIs to self-report if
  invoked from the audio thread (`EffectChain.cpp`, `AudioEngine.cpp`,
  `TrackManager.h:105`, `GarbageCollector.h:203`, `PatternPlaybackEngine.cpp:145`).
- `RTGuard.h` defines thread-local violation ring buffers +
  `recordRTViolation()` — **but nothing hooks it**: there is no operator
  new/delete trap, no mutex wrapper. The only caller is the test poking it
  manually (`AudioEngineDiagnosticsTest.cpp:73`).
- `AudioTelemetry` has `rtAllocationViolations` / `rtLockViolations` /
  `rtLogViolations` counters — **no producer increments them** (verified by
  grep; only the header defines them).

### Callback timing (deadline) infrastructure

- `AestraAudioController::audioCallback` already measures per-callback cycles
  and publishes `lastCallbackNs`, `maxCallbackNs`, and increments `overruns`
  when duration exceeds the buffer deadline (`AestraAudioController.cpp:443-459`).
- `DriverStatistics` declares `averageCallbackTimeUs` / `maxCallbackTimeUs` /
  `cpuLoadPercent`; **filled by WASAPI drivers on Windows**
  (`WASAPISharedDriver.cpp:897`), **not filled by the Linux RtAudioDriver**
  (callback at `RtAudioDriver.cpp:248` only counts callbacks/underruns).
- `AudioEngineDiagnostics` struct (`include/Core/AudioEngineDiagnostics.h`)
  declares a full multi-tier timing model — **no writer exists anywhere**
  (grep: only header, CMake, and its test reference it). It is aspirational.
- Missing everywhere: an **average** (no total-time accumulator) and a
  test that validates the deadline math.

### Preview/audition isolation — structural facts

- Preview audio is mixed **post-engine, in the device callback only**
  (`AestraAudioController.cpp:434-437`). The offline export path never runs
  that code, so exported audio structurally cannot contain preview *signal*.
- BUT: preview *ducking* is computed **inside `processBlock`**
  (`AudioEngine.cpp:925-971`) from `preview->isAudiblyPlaying()`, and
  `AudioExporter` forces `setTransportPlaying(true)`. **Candidate bug: an
  audible preview during an offline export attenuates the export by the duck
  gain.** Unproven until tested — PR-2 writes this test first.

## 2. Gap analysis → PR plan

Small PR series, every PR independently useful, all off `develop`:

| PR | Content | Mission areas |
|---|---|---|
| **PR-1** | Golden-audio harness: shared diff/report header (max abs err, RMS, first-mismatch frame+channel, peak diff, SR, length), 3 new cases — silence/empty project, impulse via master, multi-track gain+pan+FX — plus reference-update policy doc. Reuses the analytic-expectation approach of `GoldenReferenceTest` (no binary fixtures). | 1, 7 |
| **PR-2** | Realtime-vs-offline parity: same session through direct `processBlock` loop vs `bounceRangeToWav`; probes for limiter/master gain/dither/transport fades; **duck-during-export** regression test (fix minimally if it fails). | 2, 6 |
| **PR-3** | RT-safety: test-binary-level operator new/delete trap that records violations while `isRealtimeAudioThread()`; render a session under the trap and assert zero allocations; grep-based static audit script + findings table with classifications. | 3 |
| **PR-4** | Deadline diagnostics consolidation: add average accumulation (total ns counter) to `AudioTelemetry`, fill Linux `DriverStatistics` timing, test validating avg/max/budget/over-budget math. No new parallel system. | 4 |
| **PR-5** | Sample-rate & buffer truth: 96 kHz end-to-end render test (impulse/sine where wrong SR is obvious), odd buffer-size sweep (e.g. 64/193/512/1024), stale-buffer leak check, render-length-in-samples assertions. | 5 |
| **PR-6** | Preview isolation boundary doc + any residual regression tests not covered by PR-2's duck test. | 6 |

Vertical slice inside PR-1 (built first, before anything else): one
deterministic silence render + one comparison + one CTest registration.

## 3. Tolerance policy (PR-1, applies everywhere)

- Comparisons run on float32 output promoted to double.
- **Analytic cases** (silence, impulse, sine): max abs error ≤ 1e-6 unless the
  case documents engine-legitimate processing (pan law, fades) which the
  expectation models explicitly, mirroring `GoldenReferenceTest`'s
  pan-law/trim handling.
- **Measured baselines on develop @ 80fbbf87** (Linux, GCC, Release): silence and
  impulse render **bit-exact** (max abs error = 0); the 3-track gain+pan+
  bypassed-FX mix errs at one float ulp (5.96e-8, RMS −159.8 dB). Test
  thresholds (1e-6 / −120 dB) leave headroom for cross-platform FP/SIMD
  reassociation only — they are tripwires, not shrugs.
- **Cross-path parity** (PR-2): RMS diff ≤ −100 dB and max abs ≤ 1e-4 after
  trimming documented transport fades; any bit-exact match is reported as such.
- Failures always print: max abs error, RMS error (dB), first mismatching
  frame + channel, peak difference, sample rate, rendered length in
  samples — enough to diagnose without opening audio files.

## 4. How golden references are generated/updated

References are **analytic** (computed at test runtime from closed-form
signals), not stored waveforms — nothing binary in git, nothing to regenerate.
If a legitimate engine change shifts expected output (e.g. a deliberate pan-law
change), update the expectation math in the test alongside the engine change,
in the same PR, with the reasoning in the commit message.

## 5. What this protects / does not protect

**Protects:** the summing/master path (silence in = silence out, unity
routing, pan law, multi-track summing), realtime-vs-export equivalence,
allocation-freedom of the render path under test, callback deadline
accounting, sample-rate/buffer-boundary correctness, preview isolation.

**Does not protect:** perceptual quality of DSP algorithms (a "better verb"
still needs ears), plugin-internal behavior beyond bypass/summing, hardware
driver behavior (tests run headless), Windows/macOS driver timing paths
(only compile-checked in CI).

## 6. Confirmed findings (PR-2)

### BUG (fixed): preview ducking contaminated offline exports
- **Evidence:** `RealtimeExportParityTest / Export_Immune_To_Preview_Ducking`
  — with a real `PreviewEngine` audibly playing (pumped exactly as the device
  callback pumps it) and 12 dB ducking configured, `bounceRangeToWav` produced
  a file attenuated by exactly −12 dB relative to the clean export.
- **Mechanism:** duck gain is computed inside `processBlock`
  (`AudioEngine.cpp:925-971`) from `preview->isAudiblyPlaying()`;
  `AudioExporter` forces `setTransportPlaying(true)`, so the duck engaged
  during offline rendering.
- **Fix (minimal):** `AudioExporter::render` now saves the configured duck
  depth, sets it to 0, snaps the duck smoother to unity
  (`AudioEngine::resetPreviewDuckForOfflineRender()` — also kills an
  already-engaged duck's ~120 ms release tail), renders, and restores the
  configured depth. Mirrors the exporter's existing metronome/audition
  save-disable-restore. After the fix the ducked-preview export is
  **bit-exact** with the clean export.

### Steady-state realtime/export parity: BIT-EXACT
- `RealtimeExportParityTest / Realtime_vs_Export_Parity`: the realtime
  `processBlock` output and the offline `bounceRangeToWav` output for the
  same 2-track gain+pan session are **identical to the bit** (max abs error
  = 0) on the compared overlap.

### Finding (documented, not fixed): fresh-engine param-application latency
- On a **freshly constructed** engine, values newly written to
  `ContinuousParamBuffer` do not reach the mix until after the first
  `processBlock` (measured via RMS trajectory: first export block at unity
  gain, all later blocks converged). At the exporter's 4096-frame block size
  that is the first ~85 ms; at realtime block sizes it is ≤ ~11 ms.
- **Classification: acceptable by design / test-only impact.** Real exports
  run on the app's long-lived engine whose mixer state was applied long ago.
  The parity test warms the engine (`warmupEngine`) to model that reality.
  Left as a documented sharp edge for anyone constructing a fresh engine and
  exporting immediately.

## 7. RT-safety audit findings

See `AestraDocs/rt-safety-audit.md` (PR-3).
