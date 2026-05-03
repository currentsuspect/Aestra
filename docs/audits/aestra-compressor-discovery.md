# Aestra Compressor Discovery

Date: 2026-05-01

## Start State

- Starting branch: `develop`
- Starting SHA: `af11393f7c7f1388393403dc994683427da7689e`
- Remote relation after `git fetch origin develop`: local `develop` was 1 commit ahead and 0 behind `origin/develop`.
- Starting working tree: clean.

## Scope

This is a discovery/specification pass only. No compressor DSP, UI, placeholder code, or production implementation was added.

## Architecture Findings

### Current Native Plugin Path

Native Aestra plugins implement `Aestra::Audio::IPluginInstance` from `AestraAudio/include/Plugin/PluginHost.h`. The common contract covers lifecycle, RT-safe `process()`, normalized parameter descriptors, binary state blobs, editor hooks, latency/tail reporting, and watchdog status.

Built-ins are registered in `AestraAudio/src/Plugin/BuiltInPlugins.cpp` through `InternalPluginRegistry`. The existing built-in list includes:

- `com.Aestrastudios.sampler` / `Aestra Sampler`
- `com.Aestrastudios.eq` / `Aestra EQ`
- `com.Aestrastudios.comp` / `Aestra Comp`
- `com.Aestrastudios.verb` / `Aestra Verb`
- `com.Aestrastudios.delay` / `Aestra Delay`

The plugin scanner merges built-ins into the scanned plugin list in `AestraAudio/src/Plugin/PluginScanner.cpp`, and `PluginManager` creates built-ins through the hybrid factory path. Track insertion uses `EffectChain`, while Arsenal units store plugin IDs and state through `UnitManager`.

### Existing Compressor Surface

There is already a substantial compressor implementation at `AestraAudio/include/Plugin/AestraComp.h`, registered as `com.Aestrastudios.comp` and exposed as `Aestra Comp`.

It currently includes:

- Threshold, ratio, attack, release, makeup, knee, mix, bypass
- Peak/RMS detector mode
- Feed-forward/feedback topology
- Hold
- Auto release
- Range
- Lookahead parameter placeholder
- Stereo link and link law
- Sidechain HPF and LPF
- Sidechain listen
- Output trim
- Style and quality parameters
- Input/output meter atomics and gain reduction readout
- Built-in soft clipping
- Binary state v1/v2 handling

Tests already exist in:

- `Tests/AestraAudio/AestraCompPhase0Test.cpp`
- `Tests/AestraAudio/AestraCompPhase1Test.cpp`
- `Tests/AestraAudio/AestraCompUpgradeTest.cpp`

These tests are registered unconditionally in `Tests/CMakeLists.txt`.

### Arsenal and Track Fit

For normal mixing use, the compressor should be an insert effect on tracks through `EffectChain`, not an Arsenal instrument unit. Arsenal can store and restore plugin instances via `UnitManager`, but current UI loading paths distinguish `loadEffectToSelectedTrack()` from `loadInstrumentToArsenal()`. A compressor belongs in the effect browser and insert chain.

`EffectChain::process()` has a hard-coded built-in compressor sidechain exception for `com.Aestrastudios.comp`, allowing up to four input pointers when sidechain buffers exist. This means internal sidechain support already has a narrow architectural hook, but external routing UX is not a V1 requirement.

### State and Presets

Plugin state is binary per instance:

- `EffectChain::saveState()` stores plugin ID, slot bypass, slot dry/wet, and plugin state blob.
- `UnitManager::saveToJSON()` stores Arsenal unit plugin ID and state as hex.
- `AestraComp` has direct state blobs with magic values.

V1 should preserve `com.Aestrastudios.comp` state compatibility unless Dylan explicitly accepts a breaking reset. If the parameter list is reduced, the implementation should load old state safely and ignore deprecated fields.

### Metering

Global track/master metering uses `MeterSnapshotBuffer`, which has peak, RMS, low, sidechain peak, LUFS, correlation, and clip fields. It does not currently carry plugin-local gain reduction. `AestraComp` exposes atomic getters for gain reduction, input level, and output level, but there is no discovered generic plugin meter bus to connect a custom editor to those values.

V1 UI metering should either read plugin-local atomics from the editor instance or add a small, explicit plugin meter snapshot path. Do not overload track meters for gain reduction.

### Aestra Verb Patterns to Reuse

Useful patterns from `AestraVerb`:

- Implement as an internal `IPluginInstance`.
- Use normalized automatable parameters with display formatting.
- Keep parameter writes atomic and audio-thread reads lock-free where possible.
- Use explicit state magic/versioning.
- Keep lab-only diagnostics/profiling behind compile definitions.
- Add focused safety and quality labs alongside unit-style tests.

### Aestra Verb Patterns Not to Copy Blindly

Do not copy these into the compressor V1:

- Large header-only DSP accumulation.
- Lab/profiling complexity before the core product behavior is settled.
- Mutex-protected rebuild paths near audio processing.
- Highly tuned mode-specific voicing constants before baseline correctness is frozen.
- Reverb-style SIMD/lab scope. Compressor V1 should be scalar, predictable, and cheap first.

### Architecture Risks

- Existing `AestraComp` is broader than the requested V1 and includes parameters that imply unfinished or non-V1 behaviors.
- `kLookahead` exists as a parameter but is noted as TODO; shipping that surface would be misleading.
- `kStyle` and `kQuality` appear in the parameter list but are not clearly implemented as meaningful DSP modes in the inspected code.
- Built-in soft clipping makes the compressor partly a saturator/limiter, which conflicts with the stated V1 avoidance list.
- `RMSDetector::setWindowSize()` can allocate if called with a changed size. The current process path calls it in `process()`. It may not allocate in steady state, but the pattern is risky for RT guarantees.
- `AestraComp` includes `Plugin/AestraEQ.h` to reuse `BiquadFilter` and `designBiquad`, coupling compressor sidechain filtering to EQ internals.
- Compressor-specific gain reduction metering is not integrated into the generic UI meter snapshot path.
- `BuiltInPlugins::compInfo()` reports 4 audio inputs and `hasEditor = true`, but `openEditor()` currently returns false.
- The plugin display name is `Aestra Comp`; the product goal says working name should be `Aestra Compressor` unless an existing internal name wins. This naming decision affects compatibility and user-facing browser text.

## Recommended Location and Likely Files

The next implementation pass should treat the current `AestraComp` as the compatibility anchor but simplify/refactor it toward a clean V1:

- Keep plugin ID: `com.Aestrastudios.comp`
- Prefer user-facing name: `Aestra Compressor`
- Primary implementation file now: `AestraAudio/include/Plugin/AestraComp.h`
- Better future split: `AestraAudio/include/Plugin/AestraComp.h` plus `AestraAudio/src/Plugin/AestraComp.cpp`
- Optional DSP utility split: `AestraAudio/include/DSP/CompressorCore.h` and `AestraAudio/src/DSP/CompressorCore.cpp`
- Registration: `AestraAudio/src/Plugin/BuiltInPlugins.cpp`
- Build lists: `AestraAudio/CMakeLists.txt`, `Tests/CMakeLists.txt`
- Tests: replace or extend existing `AestraComp*Test.cpp` files with clearer V1 tests
- Lab: `Tests/AestraAudio/CompressorMaterialLab.cpp`, output under `labs/compressor/quality/`
- Discovery/freeze docs: `docs/audits/`

## Recommended V1 Product Design

### Identity

Working name: `Aestra Compressor`.

Primary use case: a clean native channel and bus compressor for producers who need quick control over vocals, drums, bass, synths, and mix bus dynamics without leaving Aestra.

Sonic goal: transparent-to-musical gain control with stable gain behavior, smooth automation, clear gain reduction, and no hidden saturation. It should sound controlled and composed rather than hyped.

### Recommended Control Set

Ship V1 with:

- Threshold: -60 dB to 0 dB
- Ratio: 1:1 to 20:1, with a practical stepped or gently curved mapping
- Attack: 0.1 ms to 100 ms
- Release: 10 ms to 1000 ms
- Knee: 0 dB to 24 dB
- Makeup Gain: 0 dB to +24 dB
- Mix: 0% to 100%
- Input Gain: -24 dB to +24 dB
- Output Gain: -24 dB to +24 dB
- Auto Gain: include only if implemented deterministically and documented as estimated makeup
- Sidechain HPF: include if using the existing sidechain filter path without external routing UX
- Bypass

Defer or remove from V1 surface:

- Lookahead
- Feedback topology
- Range
- Hold
- SC LPF
- SC listen
- Link law
- Quality
- Saturation/soft clipper
- External sidechain routing UX

### Modes

Recommendation: no modes in the first shipping implementation.

Reason: the existing `Style` parameter advertises Clean/Punch/Glue/Smooth, but mode behavior is not clearly meaningful in the inspected DSP. Modes should not be labels over minor constant changes. Ship one excellent clean feed-forward compressor first.

If Dylan wants modes in V1 anyway, keep them behaviorally exact:

- Clean: peak/RMS hybrid detector, neutral timing, no coloration.
- Glue: slower attack floor, slightly longer release default, softer knee default, full stereo link.
- Punch: faster release default, slower attack default, less stereo link only if exposed.

These should change defaults or a small detector/time-constant policy only. They should not add saturation, oversampling, lookahead, or hidden limiter behavior in V1.

### Defaults

Recommended defaults:

- Threshold: -18 dB
- Ratio: 4:1
- Attack: 10 ms
- Release: 120 ms
- Knee: 6 dB
- Makeup Gain: 0 dB
- Mix: 100%
- Input Gain: 0 dB
- Output Gain: 0 dB
- Auto Gain: off
- Sidechain HPF: off or 90 Hz if Dylan wants mix-bus friendly defaults
- Bypass: off
- Stereo link: 100% fixed for V1 unless exposed later

## DSP Plan

Use a clean feed-forward compressor architecture for V1.

Recommended high-level model:

1. Apply input gain.
2. Build detector signal from stereo input or internal sidechain input.
3. Optional sidechain HPF before detection.
4. Use a peak/RMS hybrid detector: peak for transient awareness, RMS for steadier musical behavior.
5. Convert detector level to dB with a floor around -120 dB.
6. Smooth level with attack/release coefficients derived from sample rate.
7. Compute gain reduction using threshold, ratio, and soft knee.
8. Smooth gain changes enough to avoid zippering without masking attack behavior.
9. Apply gain reduction and makeup gain.
10. Blend dry/wet for mix.
11. Apply output gain.
12. Sanitize output for NaN/Inf and denormals.

DSP details:

- Detector: feed-forward hybrid. Start with max-linked stereo detector for stable imaging.
- Envelope: attack when detected level rises, release when it falls; coefficients sample-rate independent.
- Knee: standard quadratic soft knee in dB.
- Gain computer: dB-domain, then convert to linear once per sample.
- Makeup: manual in V1; Auto Gain only if deterministic and testable.
- Mix: constant-gain linear wet/dry is acceptable for V1; keep dry path latency-free.
- Stereo: full stereo link for V1; avoid dual-mono image shifts by default.
- Sidechain HPF: one simple RBJ high-pass filter per detector channel, coefficients updated outside the hot path or only when the parameter changes.
- Denormals: explicitly flush tiny envelope/filter/output states to zero.
- NaN/Inf: sanitize inputs, detector levels, coefficients, gain, and outputs.
- Parameter smoothing: use per-sample smoothing for continuous parameters. Step/toggle parameters should change at block boundaries or use simple de-click ramps where audible.
- Real-time safety: no allocation, locks, logging, vector resize, string work, or state serialization in `process()`.
- Latency: 0 samples for V1.

## UI/UX Plan

The UI should be compact, readable, and explicitly about gain behavior.

Recommended layout:

- Top bar: plugin name, bypass, optional Auto Gain toggle.
- Center: large vertical or horizontal gain reduction meter, scaled 0 to -24 dB.
- Side meters: input and output peak/RMS if the meter path supports it.
- Main controls: Threshold, Ratio, Attack, Release as the primary row.
- Secondary controls: Knee, Makeup, Mix, Input, Output, optional SC HPF.
- No mode selector in V1 unless Dylan explicitly chooses modes.

At a glance, the producer should understand:

- How hard the compressor is working.
- Whether input/output levels are balanced.
- Whether the compressor is bypassed.
- The main dynamic behavior: threshold, ratio, attack, release.

Essential visual feedback:

- Gain reduction meter.
- Input/output level indication.
- Clear numeric readouts for the main controls.
- Bypass state.

Can wait:

- Transfer curve display.
- History trace.
- External sidechain monitor.
- Advanced stereo link visualization.
- Preset browser inside the editor.

The visual language should match Aestra’s native plugin direction: premium, dark, precise, modern, and non-nostalgic. Avoid fake rack screws, VU cosplay, tubes, transformers, and decorative analog labels.

## Test Plan

Required implementation tests:

- Silence stability: silence in produces silence out; no NaN/Inf; gain reduction returns to 0.
- Impulse/transient behavior: fast attack reduces transient predictably; slower attack lets initial transient through.
- Steady sine behavior: measured output gain matches threshold/ratio expectation after settling.
- Stereo linking: asymmetric stereo input preserves image under linked mode.
- Threshold/ratio correctness: static gain computer matches expected dB reduction.
- Attack/release behavior: measured envelope timing is within tolerance at multiple sample rates.
- Knee behavior: hard and soft knee curves are continuous and monotonic.
- Mix control: 0% equals dry, 100% equals wet, 50% equals expected blend.
- Bypass parity: bypass output bitwise or near-bitwise equals input.
- Parameter clamp behavior: out-of-range normalized values clamp safely.
- NaN/Inf/extreme input safety: bad input does not poison state or output.
- State roundtrip: save/load restores all V1 parameters and handles older `AestraComp` v1/v2 blobs.
- Automation-safe parameter changes: rapid sweeps do not click, explode, or allocate.
- No allocations on audio path: testable via allocation guard if the project has or adds one.
- Performance sanity: sustained processing remains comfortably below realtime at common block sizes and sample rates.

Recommended compressor material lab:

- Dry drums
- Vocal-ish signal
- Bass pulse
- Chord/pad
- Mix bus
- Transient click/snare
- Silence
- Extreme sweep case

Lab outputs should include rendered WAVs plus a markdown/JSON report under `labs/compressor/quality/`, similar in spirit to the reverb quality/material labs but smaller and less platform-fragile.

## Staged Implementation Plan

### 1. Core DSP Model

Likely files:

- `AestraAudio/include/Plugin/AestraComp.h`
- Optional: `AestraAudio/include/DSP/CompressorCore.h`
- Optional: `AestraAudio/src/DSP/CompressorCore.cpp`
- `AestraAudio/CMakeLists.txt`

Work:

- Decide whether to refactor the existing header-only class or split a reusable DSP core.
- Remove misleading V1 non-features from the active parameter surface.
- Implement clean feed-forward detector, envelope, knee, gain computer, mix, gains, sanitation, and metering.

### 2. Unit Registration and Parameters

Likely files:

- `AestraAudio/src/Plugin/BuiltInPlugins.cpp`
- `AestraAudio/include/Plugin/BuiltInPlugins.h`
- `AestraAudio/include/Plugin/AestraComp.h`

Work:

- Keep `com.Aestrastudios.comp` for compatibility.
- Rename display to `Aestra Compressor` if approved.
- Set `numAudioInputs` intentionally: 2 for V1 without sidechain, 4 if internal sidechain HPF/path remains.
- Ensure parameter IDs are stable and old state migration is explicit.

### 3. State Serialization

Likely files:

- `AestraAudio/include/Plugin/AestraComp.h`
- Tests under `Tests/AestraAudio/`

Work:

- Add/keep state magic versioning.
- Load old blobs safely.
- Ignore deprecated parameters or map them to V1 equivalents.
- Roundtrip all V1 parameters.

### 4. Basic UI

Likely files to discover further in the UI pass:

- Native plugin editor surface in `Source/` or `AestraUI/`
- `Source/Core/AestraContent.cpp`
- Plugin browser/editor opening code

Work:

- Build a native editor only after confirming the existing editor host path.
- Show essential controls and bypass.
- Avoid adding UI if the host/editor pathway is not ready.

### 5. Metering

Likely files:

- `AestraAudio/include/Plugin/AestraComp.h`
- `AestraAudio/include/Models/MeterSnapshot.h` only if a generic plugin meter bus is chosen
- Native plugin editor files once identified

Work:

- Expose gain reduction, input, and output levels as atomics.
- Decide whether plugin editor reads the instance directly or uses a formal plugin meter snapshot path.
- Keep meter smoothing UI-side where practical.

### 6. Tests

Likely files:

- `Tests/AestraAudio/AestraCompPhase0Test.cpp`
- `Tests/AestraAudio/AestraCompPhase1Test.cpp`
- `Tests/AestraAudio/AestraCompUpgradeTest.cpp`
- Optional new: `Tests/AestraAudio/AestraCompressorCoreTest.cpp`
- `Tests/CMakeLists.txt`

Work:

- Convert the old phase tests into V1 contract tests.
- Add missing cases from the test plan.
- Keep hardware-independent tests always registered.

### 7. Compressor Material/Quality Lab

Likely files:

- `Tests/AestraAudio/CompressorMaterialLab.cpp`
- `Tests/CMakeLists.txt`
- `labs/compressor/quality/` generated output, not all necessarily tracked

Work:

- Generate deterministic dry material.
- Render representative settings.
- Write metrics and a short report.
- Avoid making the lab brittle across compilers unless thresholds are intentionally platform-scoped.

### 8. Documentation and Freeze Criteria

Likely files:

- `docs/audits/aestra-compressor-*.md`
- Optional user docs under `docs/`

Freeze criteria:

- V1 control surface matches implementation.
- No misleading parameters.
- Tests pass.
- Material lab evidence reviewed.
- No known audio-thread allocations.
- Bypass parity and state compatibility verified.
- UI metering is readable and responsive if UI ships in the pass.

## Risks and Open Questions

- Should the existing `AestraComp` be simplified in place, or should a clean DSP core be introduced behind the same plugin ID?
- Should the user-facing name change from `Aestra Comp` to `Aestra Compressor` now?
- Should modes exist in V1? Recommendation: no.
- Should Auto Gain ship in V1? Recommendation: only if deterministic and tested; otherwise defer.
- Should Sidechain HPF ship in V1? Recommendation: yes only as internal detector filtering, not external sidechain routing UX.
- Should the built-in soft clipper be removed from compressor V1? Recommendation: yes.
- Should old `Style`, `Quality`, `Lookahead`, `Range`, `Hold`, `Topology`, `SC LPF`, and `SC Listen` parameters be hidden/deprecated or removed from state only?
- What is the intended native plugin editor host path, given `hasEditor = true` but `openEditor()` returns false?
- Does the UI framework already have a plugin editor panel pattern, or should V1 defer custom UI and ship DSP/tests first?
- Should compressor gain reduction metering get a generic plugin meter bus, or should the editor read `AestraComp` atomics directly?
- Does the project already have an allocation guard suitable for audio-thread tests?

## Recommendation for Next Pass

Do not add another compressor plugin. Use the existing `com.Aestrastudios.comp` as the compatibility anchor, but narrow it into a truthful `Aestra Compressor` V1.

The next Codex pass should first make a small design decision with Dylan: no modes, no lookahead, no built-in clipper, no feedback topology, and optional Auto Gain/SC HPF only if they are implemented cleanly. Then refactor or replace the existing `AestraComp` internals with a clean feed-forward core, preserve old state loading, update tests to V1 contract coverage, and only then add UI/metering.

## Final State

- Final branch: `develop`
- Final SHA before commit: `af11393f7c7f1388393403dc994683427da7689e`
- Files changed in this pass: `docs/audits/aestra-compressor-discovery.md`
- DSP code changed: no
- UI code changed: no
