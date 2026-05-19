# Aestra Audio Quality Audit — 2026-05

Branch: `feature/audio-quality-audit-2026-05`
Base SHA: `2396c8ae` (develop)

Layered grading framework: **(1)** signal integrity, **(2)** timing integrity,
**(3)** numerical integrity, **(4)** UX integrity, **(5)** stability under
stress.

Tags: `STRENGTH`, `RISK`, `BUG`, `MISSING`. Severities `P0`-`P3`.

---

## 0. TL;DR — Highest-impact issues

1. **`P1` — Export PCM_16 / PCM_24 has no dither, no noise shaping**
   (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/IO/AudioExporter.cpp:362-380`).
   Truncation-only quantization is the #1 audible "DAW sounds worse on
   delivery" surface.
2. **`P1` — Track gain smoother is broken across blocks**
   (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioGraphState.h:14-28`,
    `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:355-368`).
   Per-sample one-pole `coeff=0.001` then `snap()` to target every block: the
   smoother never converges inside a block, so fast fader moves step at block
   boundaries.
3. **`P1` — K-weighted LUFS coefficients are hard-coded at 48 kHz**
   (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1456-1464`).
   LUFS readout drifts with sample rate.
4. **`P2` — Inline master TPDF dither is mono and lives in the float path**
   (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1013-1026`).
   Same noise on L+R, applied to a float buffer that has no quantization step.
5. **`P1` — Denormal protection is x86-only**
   (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:36-47`,
    `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioRT.h:27-37`).
   ARM64 `FPCR.FZ` is not enabled.
6. **`P2` — `IntelligentDithering::NoiseShaper` IIR uses input state for output
   feedback coefficients** (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/IntelligentDithering.h:47-57`).
   Currently unused on the live path; latent landmine.
7. **`P2` — PCM_24 conversion uses `* 8388607.0f` (asymmetric)**
   (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/IO/AudioExporter.cpp:370-380`).
   Under-uses negative range, ~0.12 dB headroom loss.
8. **`P3` — Dither RNG re-seeded every block** from `m_globalSamplePos`
   (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:951-953`):
   noise becomes deterministically tonal at static positions.

Each item has a small, RT-safe fix. None require architectural changes,
plugin ABI changes, project format changes, or graph topology changes.

---

## 1. Module Map

`AestraAudio` is the realtime engine; `AudioEngine::processBlock` is the
single audio-thread entry from `AudioDeviceManager` (RtAudio, ALSA, WASAPI,
CoreAudio).

```
processBlock
  ├── ScopedRealtimeAudioThread       (TLS depth → RT misuse detection)
  ├── DISABLE_DENORMALS               (x86 MXCSR FTZ+DAZ; no-op elsewhere)
  ├── applyPendingCommands            (≤16 cmds/block)
  ├── audition / preview / silent fast paths
  ├── renderGraph                     (per-track render → effects → routing)
  │     └── AudioRenderer::renderBlock-style logic in-place
  ├── processArsenalUnits             (PreviewToMaster + pattern playback)
  ├── per-sample output loop
  │     ├── master gain × duck gain
  │     ├── NaN/Inf sanitize
  │     ├── MasterSafetyLimiter
  │     ├── peak / RMS / correlation
  │     ├── inline TPDF dither        (see §6)
  │     ├── K-weighted LUFS biquad    (see §3.4)
  │     ├── std::clamp(±1)
  │     └── double → float store
  ├── fade in/out smoothstep
  ├── metronome
  ├── true-peak meter (4× oversample)
  └── advance m_globalSamplePos
```

Offline rendering goes through `AudioRenderer::renderBlock`
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp`) and
`AudioExporter::render`
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/IO/AudioExporter.cpp`).

---

## 2. Signal Integrity

### STRENGTH — 32f I/O, 64f internal mix

- Master bus is `std::vector<double>` (`m_masterBufferD`).
- Per-track render buffers are `std::vector<std::vector<double>>`.
- Output cast to `float` happens **once** at
  `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1051`.
- Plugin scratch buffers are `float` (VST3/CLAP ABI compatibility).
- No int accumulators anywhere in the render hot path.

### STRENGTH — NaN/Inf containment

- Per-sample sanitize on master output
  (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:964-978`).
- Plugin output sanitize after every plugin call via `sanitizeFloatBuffers`
  (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:36-48`).
- Plugin `process` wrapped in `processPluginNoexcept`
  (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:25-34`).

### BUG / P1 — Export PCM bit-depth conversion is undithered

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/IO/AudioExporter.cpp:362-380`:

```cpp
// PCM_16
float sample = std::clamp(buffer[i], -1.0f, 1.0f);
converted[i] = static_cast<int16_t>(sample * 32767.0f);

// PCM_24
float sample = std::clamp(buffer[i], -1.0f, 1.0f);
const int32_t packed24 = static_cast<int32_t>(sample * 8388607.0f);
```

Plain truncation. No TPDF, no noise shaping. PCM_16 is audibly grainy on
quiet tails (-60 dB and below). Required fix:

- TPDF dither ± 1 LSB, channel-uncorrelated
- Optional noise shaping (off by default at 16-bit unless requested)
- Symmetric range `* 32768.0f`, then clamp to `[-32768, 32767]`
- Symmetric range `* 8388608.0f`, then clamp to `[-8388608, 8388607]`

### MISSING / P2 — No inter-sample-peak control on export clamp

`std::clamp(buffer[i], -1.0f, 1.0f)` happens at PCM write. Inter-sample peaks
that ride between samples will hard-clip after 4× reconstruction at the DAC.
True-peak meter exists (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/TruePeakMeter.h`)
and is wired to export validation
(`Config::validateTruePeak`, `failOnTruePeakExceeded` at
`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/IO/AudioExporter.h:90-107`),
but no automatic attenuation. Acceptable — leave to the user's master chain.

---

## 3. Numerical / DSP Correctness

### STRENGTH — Polyphase Sinc resampling

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/Interpolators.h:507-710`:

- `Sinc64Turbo`: 64-tap, 2048 phases, Kaiser β=12 (~144 dB target SNR)
- Phase interpolation between adjacent quantized phases
- Runtime CPU dispatch: AVX-512 → AVX2+FMA → SSE4.1 → NEON → scalar
- Hot path is one indirect call, zero per-call branches

`Sinc8Turbo`/`Sinc16Turbo`/`Sinc32Turbo` exist as lighter polyphase tiers.
`CubicInterpolator` for lowest tier.

### STRENGTH — Clip resample phase math

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:155-325`:
phase is anchored to absolute timeline math at the start of each block
(`phase = clip.sampleOffset + (start - clip.startSample) * ratio`), then
incremented `phase += ratio` per sample. No cross-block accumulation drift.

### STRENGTH — Pan law

sin/cos constant-power, -3 dB at center
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:63-67`).

### BUG / P1 — K-weighted LUFS coefficients are 48 kHz only

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1456-1464`:

```cpp
const AudioEngine::BiquadCoeff AudioEngine::kKWeightPreFilter = {
    1.53512485958697, -2.69169618940638, 1.19839281085285,
    -1.69065929318241, 0.73248077421585
};
const AudioEngine::BiquadCoeff AudioEngine::kKWeightRLB = {
    1.0, -2.0, 1.0,
    -1.99004745483398, 0.99007225036621
};
```

These are the canonical ITU-R BS.1770-4 48 kHz coefficients. They are used
unconditionally regardless of `m_sampleRate`. At 44.1 kHz the bias is small;
at 88.2/96/176.4/192 kHz it is large and integrated LUFS reads wrong.

Fix: bilinear transform from the analog prototype each time
`setSampleRate` changes, or maintain a per-rate table.

### STRENGTH — LUFS biquad form

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:931-941`:
Direct Form II, double-precision state, FTZ-protected on x86.

### STRENGTH — Master safety limiter

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/MasterSafetyLimiter.h`:
DC blocker (≈30 Hz at 48 kHz) → soft knee from 0.85 → exponential shaping to
0.95 ceiling → ±1.25 hard clamp. NaN/Inf replaced with 0. Documented as
safety, not mastering.

### BUG / P2 — `IntelligentDithering::NoiseShaper` IIR is broken

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/IntelligentDithering.h:47-57`:

```cpp
shapedL = b0 * errorL + b1 * z1L + b2 * z2L - a1 * z1L - a2 * z2L;
...
z2L = z1L; z1L = errorL;
```

`z1L`/`z2L` are loaded with **input** history `x[n-1]`, `x[n-2]`, but the
`-a1·z − a2·z` term is supposed to use **output** history `y[n-1]`, `y[n-2]`.
As written, this collapses to a degenerate FIR. Currently unused on the live
path; fix or remove to prevent accidental wiring.

### RISK / P1 — Denormal protection is x86-only

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:36-47`
`DISABLE_DENORMALS` sets MXCSR only on x86. The helper at
`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioRT.h:27-37` is
also x86-only and is not called from `processBlock` (macro form is used).

ARM64 should set `FPCR.FZ` (bit 24). Any per-sample IIR (DC blocker, LUFS
filter, future reverbs/delays) will run 30–100× slower on long silent tails
without it.

---

## 4. Timing Integrity

### STRENGTH — Snapshot graph, lock-free commands

- `AudioGraphState` is published via index swap with acquire/release
  (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:783-786`).
- Commands flow through `AudioCommandQueue` with bounded RT drain.
- Off-RT PDC solver publishes `SolvedLatencyTopology` via the same
  double-buffered atomic-index pattern
  (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:1042-1055`).

### STRENGTH — Transport edge detection

Edge flags (`m_transportRestartRequested`, `m_transportStopRequested`,
`m_transportHardStopRequested`) ensure stop→play and double-stop within a
single block are not lost
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:156-200`).

### STRENGTH — Loop-split rendering

`renderGraph` is invoked twice across a loop boundary with proper position
re-anchoring; pattern flush is performed on the wrap
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:791-806`).

### STRENGTH — Plugin Delay Compensation (PDC) v2

- Per-track latency reported and stored in `TrackRTState.pluginLatencySamples`.
- Per-edge `EdgeDelayState` buffers double-buffered with retired-allocation
  keep-alive
  (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioGraphState.h:47-68`).
- Solver topology is published via atomic index flip; engine reads with
  acquire. Audit hooks exist for tests
  (`AudioEngine::getLastSolvedLatencyTopology`,
  `AudioEngine::getTrackEdgeDelaySnapshot`).

### RISK / P3 — Compensation buffer fixed at 16384 frames

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioGraphState.h:97`:
`std::array<float, 32768> compensationBuffer{}` — 16384 frames stereo.

At 96 kHz this is ~170 ms; at 192 kHz it is ~85 ms. A heavy linear-phase EQ
on a track can blow past this. Already softened by the per-edge `EdgeDelayState`
ring buffer, which grows off-RT. Leave as-is; documented in `PDC-v2-Design.md`.

---

## 5. Automation Smoothing

### BUG / P1 — Track gain smoother never converges within a block, then snaps

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioGraphState.h:14-28`:

```cpp
struct SmoothedParamD {
    double current{1.0};
    double target{1.0};
    double coeff{0.001};
    inline double next() {
        current += coeff * (target - current);
        return current;
    }
    void snap() { current = target; }
};
```

Used in `AudioRenderer::processTrackEffects`
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:355-368`):

```cpp
state.gainL.setTarget(gainL);
state.gainR.setTarget(gainR);
for (uint32_t i = 0; i < numFrames; ++i) {
    self[i * 2]     *= state.gainL.next();
    self[i * 2 + 1] *= state.gainR.next();
}
state.gainL.snap();
state.gainR.snap();
```

With `coeff=0.001`, a typical 64–1024 sample block does not converge before
`snap()` forces it to target. So a moving fader produces:

```
ramp ramp ramp ramp …  STEP  ramp ramp …  STEP  …
                       ↑ block boundary
```

This is exactly the zipper-replacement-creating-zippers pattern. The master
gain already uses the correct pattern (`linear ramp finishing at target,
no snap`):

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:924-928,1054`:

```cpp
const double gainDelta = (targetGain - currentGain) / static_cast<double>(numFrames);
double gain = currentGain;
...
gain += gainDelta;
```

Fix: use the same linear-ramp pattern for `SmoothedParamD`, or compute the
exponential coefficient from a sample-rate-aware time constant and **remove**
the `snap()`. The first is simplest and matches the master path.

### STRENGTH — Master gain ramp is correct

Master gain ramp (per block): `delta = (target - current) / numFrames`, no
snap, converges exactly at the last sample.

### STRENGTH — Preview ducking smoothing

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:872-901`:
Linear fade with 50 ms time constant; both directions clamped to target.

### RISK / P3 — `ContinuousParamBuffer` reads use bitcast atomics

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/ContinuousParamBuffer.h`:
Per-block read is fine. Per-sample read would be wrong because of partial
update visibility; today we read once per block, set target, then smooth.
Correct.

---

## 6. Dither & Export Pipeline

### BUG / P2 — Master inline TPDF is mono and lives in float path

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1013-1026`:

```cpp
float r1 = m_ditherRng.nextFloat();
float r2 = m_ditherRng.nextFloat();
double noise = (double(r1) - double(r2)) * LSB_24;
L += noise;
R += noise;
```

Same `noise` on L and R: mono dither, reduces dither entropy 3 dB on stereo,
biases stereo image at near-silence. Applied to a float buffer that has no
quantization step downstream (the driver writes 32-bit float).

It also runs **before** the hard `std::clamp` so on borderline samples it can
push into the clipping path.

Fix path:

1. Move dither out of the live master output entirely
2. Apply TPDF only in `AudioExporter::writeSamples<int16_t>` and
   `writeSamples<int32_t>` (the actual quantization step)
3. Use independent random for L and R
4. Optional: add noise shaping (separate fix)

### RISK / P3 — RNG re-seeded every block from `m_globalSamplePos`

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:951-953`:

```cpp
m_ditherRng.setSeed(static_cast<uint32_t>(m_globalSamplePos.load()) ^ 0x9E3779B9);
```

Per-block reseed. At static positions (stopped) the dither sequence becomes
deterministically tonal. Move to once-per-render or once-per-export.

### STRENGTH — True-peak metering on export

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/IO/AudioExporter.h:90-107`:
`validateTruePeak`, `truePeakCeilingdBTP`, `failOnTruePeakExceeded`. Wired to
the same `TruePeakMeter` used at runtime.

---

## 7. RT Safety in Audio Callback

### STRENGTH — Zero-alloc in hot path

- All buffers pre-allocated in `setBufferConfig`
  (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1223-`).
- RT scratch vectors sized to `kMaxTracks` and never resized in `renderGraph`.
- Plugin scratch buffers pre-sized for the worst-case block.
- Send routing uses a pre-sized `PreparedSendRoute` scratch.

### STRENGTH — Bounded command drain

≤16 commands per block keeps callback latency bounded
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:173-200`).

### STRENGTH — Per-block telemetry, no per-sample atomics

Counters (NaN, clip) accumulate locally and `fetch_add` once at block end.

### STRENGTH — Plugin exception isolation

`processPluginNoexcept` catches all exceptions, treats as silence
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:25-34`).

### RISK / P2 — `MetronomeEngine::loadClickSounds` does file I/O guarded by
transport state

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:316-322`
guards file I/O when transport is playing, but not when it is paused mid-render.
Safe today because it's a UI action and the UI calls this off-thread, but the
guard is incomplete. Out of scope for this audit.

---

## 8. System Stability Under Stress

### STRENGTH — Hard-stop / panic path

- `transportHardStopRequested` sets `FadeState::Silent` immediately
- MIDI panic injection into unit MIDI buffers
- Cached sampler voices reset RT-side
- Pattern engine flush
- All NaN/Inf replaced with 0

### STRENGTH — RT misuse detection

`g_realtimeAudioThreadDepth` TLS counter + `reportRealtimeMisuse` hook
publishes telemetry when blocking APIs are called from the audio thread
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/RealtimeThreadGuard.h`).

### STRENGTH — Underrun telemetry

`m_telemetry.incrementUnderruns` on early-out paths and stable-block tracking
for recovery.

---

## 9. Additional Findings from Implementation Prep

These were found while preparing the first implementation slice. They should
shape later iterations because they affect render parity and how trustworthy
quality controls are.

### BUG / P1 — `bounceRangeToWav` calls `AudioRenderer::renderBlock` without `ctx.graph`

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:3100-3108`
constructs an `AudioRenderer::Context` but does not set `ctx.graph`.

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:163`
then does:

```cpp
const auto& graph = *ctx.graph;
```

So `AudioEngine::bounceRangeToWav` can dereference null as soon as it renders a
track through `renderClipAudio`. This path is separate from
`AudioExporter::render`, which currently uses `AudioEngine::processBlock`.

Required fix:

- Set `ctx.graph` in `bounceRangeToWav`, or remove the dependency from
  `AudioRenderer::renderClipAudio`
- Add a narrow bounce regression test with at least one audio clip
- Verify isolated-track bounce still works

### RISK / P1 — There are two offline render authorities with different output stages

`AudioExporter::render` says it uses `AudioRenderer::renderBlock` in comments
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/IO/AudioExporter.cpp:163`),
but actually calls:

```cpp
m_engine.processBlock(m_renderBufferF.data(), nullptr, framesThisBlock, 0.0);
```

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:3013-3150`
`bounceRangeToWav` instead calls `m_rtRenderer.renderBlock`, then writes float
samples directly through miniaudio.

Quality impact:

- `AudioExporter` gets the live master stage: limiter, LUFS accumulation,
  true-peak meter, hard clamp, live dither path, fade state, metronome disable
  policy.
- `bounceRangeToWav` bypasses the live master stage and writes unclamped float
  output.
- Smoother state and plugin/Arsenal behavior can diverge across the two paths.

Required fix:

- Choose one authoritative offline rendering path
- Route both export and bounce through it
- Make master-stage inclusion explicit per format/scope
- Add export-vs-bounce parity tests for float32 and PCM output

### BUG / P1 — Live send gain smoothing has the same exp+snap zipper pattern

The first audit called out `AudioRenderer::processTrackEffects`, but the live
`AudioEngine::renderGraph` path also uses `SmoothedParamD::next()` for track
and send gains, then snaps all smoothers at the block boundary:

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2464-2468`
uses `state.gainL.next()` / `state.gainR.next()`.

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2571-2610`
uses `route.gainL->next()` / `route.gainR->next()` for sends.

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2646-2652`
then snaps both track and send smoothers.

So the smoothing fix must cover:

- `AudioRenderer::processTrackEffects`
- `AudioEngine::renderGraph` track fader/pan smoothing
- `AudioEngine::renderGraph` send gain/pan smoothing
- `SmoothedParamD` tests directly, not only render-output tests

### BUG / P2 — Pre-fader send fail-safe can still route from an empty buffer

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2446-2462`
detects insufficient `preFaderBuffer` capacity and sets `hasPreFaderSend =
false`, but route construction later still does:

```cpp
route.source = send.postFader ? buffer.data() : state.preFaderBuffer.data();
```

for every non-muted send
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2523-2525`).

Normal operation should reserve enough capacity in `setBufferConfig`, but an
oversized callback or bad driver block can turn a fail-safe into an invalid read
instead of silence. This is a stress-path stability issue.

Required fix:

- Skip pre-fader sends when `hasPreFaderSend` was disabled
- Add an oversized-block renderGraph regression if practical

### RISK / P2 — `DitheringMode` is defined twice in the same namespace

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Drivers/AudioDriverTypes.h:15`
defines:

```cpp
enum class DitheringMode { None, Triangular, HighPass, NoiseShaped };
```

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/DSP/IntelligentDithering.h:14-18`
also defines `enum class DitheringMode` in `Aestra::Audio` with different
enumerators:

```cpp
enum class DitheringMode {
    CD_16bit,
    Pro_24bit,
    Float_32bit
};
```

Any translation unit that includes both headers will fail to compile or force
awkward include-order assumptions. This also makes it unclear whether "dither
mode" means driver policy, export bit-depth policy, or the intelligent ditherer
mode.

Required fix:

- Rename the DSP enum, e.g. `DitherBitDepthMode`
- Keep `Drivers::AudioDriverTypes`-style `DitheringMode` for engine policy
- Wire export format explicitly instead of overloading driver policy

### RISK / P2 — High-pass and noise-shaped dither modes are UI/API-visible but not implemented in the live path

`AudioEngine::processBlock` checks only `ditherMode != DitheringMode::None`
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:1013`)
and always applies the same mono 24-bit TPDF noise. `DitheringMode::HighPass`
and `DitheringMode::NoiseShaped` are therefore behavioral aliases for
`Triangular` in the live path.

`QualityPreset::HighFidelity` and `QualityPreset::Mastering` select those modes
in `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/MixerChannel.h:86-91`,
so the public quality preset semantics overstate the actual engine behavior.

Required fix:

- Either implement high-pass/noise-shaped export dither, or remove/gray out the
  modes until implemented
- Keep live float output dither disabled by default; dither belongs at integer
  quantization

### RISK / P3 — `AudioExporter` contains stale master-stage API and unused render buffers

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/IO/AudioExporter.h:240-280`
declares `applyMasterOutputStage`, `m_renderBufferD`, DC blockers, and an
`std::mt19937` dither RNG, but the current implementation writes
`m_renderBufferF` from `processBlock` and never calls `applyMasterOutputStage`.

This is not directly audible today, but it is a maintenance hazard: future
export-quality work can accidentally patch dead code and still leave exports
unchanged.

Required fix:

- Delete or wire the stale master-stage members during the export-dither slice
- Add tests that read actual WAV bytes rather than only exercising helper code

---

## 10. Prioritized Fix Plan

Each slice is independently buildable and testable. None require graph,
project format, or plugin-ABI changes. All are RT-safe.

| # | Slice | Severity | Files | Status |
|---|-------|----------|-------|--------|
| 1 | TPDF dither at export PCM_16/PCM_24 with uncorrelated L/R and symmetric range | P1 | `AudioExporter.cpp` | DONE |
| 2 | Replace `SmoothedParamD` exp+snap with linear-ramp-per-block for track, master, and send gains | P1 | `AudioGraphState.h`, `AudioRenderer.cpp`, `AudioEngine.cpp` | DONE |
| 3 | Fix/merge offline render authorities; make export-vs-bounce parity testable | P1 | `AudioExporter.cpp`, `AudioEngine.cpp`, `AudioRenderer.cpp` | DONE (master bounce only; isolated track bounce TODO) |
| 4 | Fix `bounceRangeToWav` null `ctx.graph` and add clip bounce regression | P1 | `AudioEngine.cpp`, `AudioRenderer.cpp` | TODO |
| 5 | Sample-rate-aware K-weighted LUFS coefficients via bilinear transform | P1 | `AudioEngine.h`, `AudioEngine.cpp` | TODO |
| 6 | Remove inline master TPDF from float output path | P2 | `AudioEngine.cpp` | TODO |
| 7 | ARM64 denormal protection (`FPCR.FZ`) | P1 (ARM only) | `AudioRT.h`, `AudioEngine.cpp` | TODO |
| 8 | Fix `IntelligentDithering::NoiseShaper` IIR (separate input vs output state) and rename its bit-depth enum | P2 | `IntelligentDithering.h` | TODO |
| 9 | Symmetric PCM_24 range (`* 8388608`, clamp `[-8388608, 8388607]`) | P2 | `AudioExporter.cpp` (covered by slice 1) | DONE |
| 10 | Skip pre-fader sends when fail-safe disables `preFaderBuffer` | P2 | `AudioEngine.cpp` | DONE |

Validation per slice:

- Build `AestraAudioCore`, `AudioExporter`-dependent targets
- Run relevant test binaries (audio engine / exporter / smoothed param)
- Bypass parity for #2 (no DSP change when target == current)
- 48 kHz LUFS regression for #3 (must match the literature on the existing rate)
- Export null test for #1 (input float == int->float roundtrip ± dither LSB)

---

## 11. Out of Scope for This Pass

- Linear-phase EQ / mastering-grade limiter (premium DSP scope)
- 64-bit offline render path (deferred; current double-bus achieves 144 dB SNR
  internally already)
- Time-stretch / pitch-shift quality upgrades (separate workstream)
- Sample-accurate plugin parameter automation (PDC v2 currently aligns audio;
  parameter automation is block-rate)
- Plugin oversampling helpers (premium DSP scope)

---

*Audit prepared on `feature/audio-quality-audit-2026-05` at base SHA
`2396c8ae`. All findings reference current code; any pre-existing
documentation that contradicts these findings should be re-validated against
the file/line citations above.*
