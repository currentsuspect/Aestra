# Aestra EQ Discovery

Date: 2026-05-01

## Start State

- Starting branch: `develop`
- Starting SHA: `fe62232e251eb351a5ac8830557d4b8b5290d5ec`
- Remote relation: local `develop` was 2 commits ahead and 0 behind `origin/develop`.
- Starting working tree: clean.

## Scope

This is a discovery/specification pass only. No EQ DSP, UI, or production code was changed.

---

## Phase 1 — Architecture Findings

### Does Aestra EQ Exist?

Yes. The EQ is a fully registered, substantially implemented plugin with DSP, state, editor, spectrum analyzer, and tests.

### Plugin Identity

| Field | Value |
|---|---|
| Plugin ID | `com.Aestrastudios.eq` |
| Display Name | Aestra EQ |
| Version | 0.1.0 |
| Category | Equalizer |
| Type | Effect |
| Audio Inputs | 4 (sidechain routing slots, unused by EQ DSP) |
| Audio Outputs | 2 |
| Has Editor (info) | `false` (mismatched — editor actually exists) |
| Has Editor (real) | Yes, `AestraEQEditor` in AestraUI |
| Latency | 0 samples |
| Tail | 64 samples |

Registration: `AestraAudio/src/Plugin/BuiltInPlugins.cpp:158` via `InternalPluginRegistry`.

### DSP Structure

- **Topology**: Series biquad filters per channel (RBJ cookbook, `AestraAudio/include/Plugin/AestraEQ.h:102-200`).
- **Bands**: 8 (`kNumBands = 8`), all enabled by default as Bell filters.
- **Filter types** (enum at line 87): Bell, LowCut, HighCut, LowShelf, HighShelf, Notch, BandPass, Tilt.
- **Cascaded stages**: LowCut/HighCut support 12/24/48/72/96 dB/oct via cascaded Butterworth stages (`kMaxFilterStages = 8`). Each stage uses Q=0.7071 (Butterworth).
- **Stereo**: Hard-coded 2-channel processing. Per-channel independent filter state.
- **Coefficient update**: `updateAllFilters()` recomputes all coefficients when `m_filtersDirty` is set. Called from `setParameter()` and at the start of `process()` if dirty.
- **NaN/Inf guard**: `BiquadFilter::process()` checks input and output for NaN/Inf and resets filter state to zero. `setCoeffs()` validates coefficients and falls back to passthrough if unstable.
- **Denormal handling**: Global FTZ/DAZ is set in `AudioEngine.cpp:29` via `_mm_setcsr`. The EQ itself has no per-filter denormal guard.

### Parameter Surface

41 parameters (`kParamCount = 41`):

Per band (5 params x 8 bands = 40):

| Sub | Name | Normalized | Mapping | Automatable |
|---|---|---|---|---|
| 0 | Band N On | 0/1 (threshold 0.5) | bool | yes |
| 1 | Band N Type | 0-1 | round(val * 7) → 0-7 enum | yes |
| 2 | Band N Freq | 0-1 | log 20Hz-20kHz | yes |
| 3 | Band N Gain | 0-1 | linear -18dB to +18dB | yes |
| 4 | Band N Q | 0-1 | linear 0.1-10.0 | yes |

Plus master bypass (param 40): 0/1, threshold 0.5.

Default band frequencies (normalized): 0.04, 0.12, 0.22, 0.36, 0.50, 0.66, 0.80, 0.92 — roughly logarithmically spaced across the spectrum.

### State Serialization

- `EQStateBlob` at `AestraEQ.h:214-220`: magic `0x45510001`, version 1, 41 floats for params, 8 bytes enabled, 8 bytes types.
- `saveState()` serializes the blob.
- `loadState()` validates magic, loads params via `setParameter()`, then restores enabled/types from the blob.
- **No version migration**: if magic doesn't match, load fails. No path for upgrading old state.

### Editor/UI Status

Fully implemented in `AestraUI/Widgets/AestraEQEditor.h/.cpp` (1209 lines):

- Window: 1180 x 660 pixels.
- **Response curve**: Real-time computed from band parameters (1400-point evaluation with Catmull-Rom smoothing). Draws with layered polylines for glow effect.
- **Spectrum analyzer**: Background thread (`analyzerWorkerMain`) performs 160-bin Goertzel analysis on 1024-sample windows. Results smoothed with attack/release envelope. Drawn as bar visualization behind the curve.
- **Draggable graph nodes**: Each enabled band has a draggable node on the curve. Frequency = X, Gain = Y (for gain-bearing types), Q = Y (for cut/notch/bandpass). Hit test radius 18px.
- **Band panels**: 8 band cards at bottom with Freq/Gain/Q knobs, type label, enable dot.
- **Right-click type menu**: Context menu to change band type.
- **Scroll wheel**: Q adjustment on hovered graph node.
- **Title bar**: Shows "AESTRA EQ" / "ADVANCED EQUALIZER", undo/redo chips (non-functional), preset display (non-functional), A/B mode chip (non-functional), S/M/R icons (non-functional).
- **Utility strip**: 7 control blocks — BYPASS, STEREO MODE, ANALYZER, SLOPE, RESOLUTION, DYNAMIC EQ, PANEL — all rendered but mostly non-functional.
- **Input/Output panels**: Knob + level meter — meter is static (hard-coded fill).
- **HPF/LPF guard panels**: Display "24 dB/Oct" and frequency — decorative, not connected to actual band parameters.
- **Dynamic EQ section**: Bottom panel with Threshold/Range/Ratio/Attack/Release/Makeup/Mix knobs, GR meter, sidechain display — all rendered, **none connected to DSP**.
- **Status bar**: Shows "Zero Latency" (correct), "Oversampling: 2x" (false — no oversampling exists), "Max Bands: 8" (correct).

Editor routing: `PluginUIController.cpp:348-354` creates `AestraEQEditor` for `com.Aestrastudios.eq`.

### Test Coverage

3 tests in `Tests/AestraAudio/AestraEQTest.cpp`, registered unconditionally in `Tests/CMakeLists.txt:721-722`:

1. **Bypass passthrough**: Verifies output matches input within 1e-6 when bypassed.
2. **Low cut attenuation**: Band 0 as LowCut at 200Hz, verifies 80Hz tone is attenuated. Also checks for NaN/Inf during processing.
3. **Bell boost**: Band 3 as Bell at 1kHz with +12dB, verifies output RMS exceeds input RMS.

Tests are basic — they confirm the filters work but do not cover edge cases, state roundtrip, smoothing, or stability.

### Metering/Analyzer Status

- **Audio-side analyzer**: `publishAnalyzerFrame()` in `process()` writes mono-summed samples into a double-buffered 1024-sample window. Published via atomic page flip and serial counter. RT-safe (no allocation, no locks).
- **UI-side spectrum**: Background thread reads analyzer windows, performs Goertzel transform, publishes 160-bin magnitude array. Smoothed with attack/release envelope in the render thread.
- **No input/output level metering**: The input/output panels show static meter fills. No actual peak/RMS metering is implemented.
- **No gain reduction metering**: Not applicable for EQ, but the dynamic EQ GR meter is entirely decorative.

---

## Phase 2 — Product Truth Audit

### REQUIRED Issues (correctness/safety problems)

| # | Issue | Location | Risk |
|---|---|---|---|
| R1 | **No parameter smoothing.** `setParameter()` writes directly to atomics. `process()` reads them and recomputes all filter coefficients instantly. Parameter changes (especially gain, frequency, type) cause audio clicks/pops. The compressor has per-sample smoothing; the EQ has none. | `AestraEQ.h:344-361` | High — audible artifacts on any parameter change |
| R2 | **No coefficient interpolation.** When `m_filtersDirty` is set, `updateAllFilters()` snaps all coefficients to new values. Biquad filters with discontinuous coefficient changes produce transient artifacts. | `AestraEQ.h:567-591` | High — audible on any EQ adjustment |
| R3 | **Band type value not bounds-checked on load.** `loadState()` writes `blob->types[i]` directly to the atomic. If the value is >7, `static_cast<FilterType>` is undefined. | `AestraEQ.h:472` | Medium — undefined behavior on corrupted state |
| R4 | **State version not handled.** `loadState()` checks magic but not version. If a future version changes the blob layout, loading old state will silently corrupt. | `AestraEQ.h:465` | Medium — future compat risk |
| R5 | **`m_params` array not initialized.** `m_params` is a `std::array<std::atomic<float>, kParamCount>` with no initializer. At construction, values are indeterminate. `initialize()` doesn't set them. Only `BuiltInPlugins::applyInfo()` calls `setParameter()` for defaults, but direct construction (as in tests) leaves them uninitialized. | `AestraEQ.h:634` | Medium — undefined behavior if used before `applyInfo()` |
| R6 | **Hard-coded 2-channel assumption.** `updateAllFilters()` iterates `for (uint32_t ch = 0; ch < 2; ++ch)` regardless of actual channel count. Filter array is sized for 2 channels. Works for stereo but is fragile. | `AestraEQ.h:568` | Low — currently always stereo |

### RECOMMENDED Issues (low-risk V1 improvements)

| # | Issue | Location | Notes |
|---|---|---|---|
| C1 | **8 bands is more than V1 needs.** Default recommendation is 4-6 bands. 8 bands with 8 cascaded stages = 128 biquad filters per stereo pair. Reducing to 6 bands reduces DSP cost and simplifies the UI. | `AestraEQ.h:228` | Consider reducing to 6 |
| C2 | **Tilt filter type is unusual.** Not standard in most DAW EQs. May confuse users. Not a priority for V1. | `AestraEQ.h:95` | Defer or remove |
| C3 | **BandPass filter type.** Useful but not standard in minimal EQ V1. | `AestraEQ.h:94` | Defer |
| C4 | **Cut slope Q parameter is misleading.** The UI shows Q knob/slider for cut filters, but DSP ignores it (forces Butterworth Q). The UI should show discrete slope selector instead. | `AestraEQ.h:551-558`, editor | Fix for V1 |
| C5 | **Analyzer thread lifecycle.** The spectrum worker thread is spawned in the editor constructor and joined in the destructor. If the editor is created/destroyed frequently, thread churn could be a problem. | `AestraEQEditor.cpp:164-176` | Low risk, acceptable for V1 |
| C6 | **Goertzel per-bin cost.** 160 bins x 1024 samples = 163,840 multiplies per analyzer frame. Could be optimized with FFT, but runs on a background thread so not RT-critical. | `AestraEQEditor.cpp:669-693` | Acceptable for V1 |
| C7 | **`hasEditor()` returns false in PluginInfo.** The EQ info at `BuiltInPlugins.cpp:52` sets `p.hasEditor = false` but an editor exists. Should be `true`. | `BuiltInPlugins.cpp:52` | Fix |

### DEFER Issues (useful later, not needed for V1)

| # | Feature | Notes |
|---|---|---|
| D1 | Dynamic EQ | UI section exists but DSP is entirely absent. Defer to V2. |
| D2 | Mid/Side processing | Title bar shows S/M/R icons but no implementation. Defer. |
| D3 | A/B comparison | Title bar chip rendered but non-functional. Defer. |
| D4 | Preset system | "Modern Hip Hop Mix" preset displayed but no preset load/save. Defer. |
| D5 | Undo/redo | Chips rendered but non-functional. Defer (plugin undo is host-level). |
| D6 | Linear phase mode | Not implemented. Significant DSP complexity. Defer. |
| D7 | Oversampling | Status bar claims 2x but none exists. Remove the false claim; implement later if needed. |
| D8 | Per-band stereo link (L/R, M/S) | Not implemented. Defer. |
| D9 | Analyzer resolution modes | Utility strip shows "High" resolution but no toggle exists. Defer. |
| D10 | Input/Output gain trim | Panels show knobs but they're not connected to parameters. Defer. |

### REJECT Issues (scope creep for V1)

| # | Feature | Reason |
|---|---|---|
| X1 | Sidechain-triggered dynamic EQ | Not a V1 feature. No sidechain DSP path. |
| X2 | Spectrum analyzer resolution toggle | The current Goertzel analyzer is sufficient. Adding resolution modes is scope creep. |
| X3 | Panel layout modes ("Compact") | Utility strip shows "Compact" option. Not needed for V1. |
| X4 | Slope selector in utility strip | The utility strip shows "12 24 36 48" but per-band slope is already in the Q parameter. Redundant UI. |

---

## Phase 3 — Recommended EQ V1 Design

### Band Count

6 bands (reduced from 8). Sufficient for professional mixing work. Reduces DSP cost and UI complexity.

### Band Layout (fixed types for V1)

| Band | Type | Purpose |
|---|---|---|
| 1 | High-Pass | Remove sub-bass rumble |
| 2 | Low Shelf | Bass body/weight |
| 3 | Bell | Sweepable parametric |
| 4 | Bell | Sweepable parametric |
| 5 | High Shelf | Air/brilliance |
| 6 | Low-Pass | Remove hiss/ultrasonic content |

Rationale: Fixed band types eliminate the type parameter and the complex UI it requires. Each band has a clear purpose. Users don't need to configure a bell as a shelf — the layout tells them where to go.

### Parameter Table

**Band 1 — High-Pass**

| Parameter | Name | Range | Units | Default | Automatable | V1? |
|---|---|---|---|---|---|---|
| B1 On | High-Pass Enable | 0/1 | — | 0 (off) | yes | yes |
| B1 Freq | High-Pass Frequency | 20-500 | Hz | 80 | yes | yes |
| B1 Slope | High-Pass Slope | 12/24/36/48 | dB/oct | 24 | yes | yes |

**Band 2 — Low Shelf**

| Parameter | Name | Range | Units | Default | Automatable | V1? |
|---|---|---|---|---|---|---|
| B2 On | Low Shelf Enable | 0/1 | — | 0 (off) | yes | yes |
| B2 Freq | Low Shelf Frequency | 40-1000 | Hz | 200 | yes | yes |
| B2 Gain | Low Shelf Gain | -18 to +18 | dB | 0 | yes | yes |
| B2 Q | Low Shelf Q | 0.1-10.0 | — | 0.707 | yes | yes |

**Band 3 — Bell 1**

| Parameter | Name | Range | Units | Default | Automatable | V1? |
|---|---|---|---|---|---|---|
| B3 On | Bell 1 Enable | 0/1 | — | 1 (on) | yes | yes |
| B3 Freq | Bell 1 Frequency | 80-8000 | Hz | 500 | yes | yes |
| B3 Gain | Bell 1 Gain | -18 to +18 | dB | 0 | yes | yes |
| B3 Q | Bell 1 Q | 0.1-10.0 | — | 1.0 | yes | yes |

**Band 4 — Bell 2**

| Parameter | Name | Range | Units | Default | Automatable | V1? |
|---|---|---|---|---|---|---|
| B4 On | Bell 2 Enable | 0/1 | — | 1 (on) | yes | yes |
| B4 Freq | Bell 2 Frequency | 200-16000 | Hz | 2000 | yes | yes |
| B4 Gain | Bell 2 Gain | -18 to +18 | dB | 0 | yes | yes |
| B4 Q | Bell 2 Q | 0.1-10.0 | — | 1.0 | yes | yes |

**Band 5 — High Shelf**

| Parameter | Name | Range | Units | Default | Automatable | V1? |
|---|---|---|---|---|---|---|
| B5 On | High Shelf Enable | 0/1 | — | 0 (off) | yes | yes |
| B5 Freq | High Shelf Frequency | 2000-20000 | Hz | 8000 | yes | yes |
| B5 Gain | High Shelf Gain | -18 to +18 | dB | 0 | yes | yes |
| B5 Q | High Shelf Q | 0.1-10.0 | — | 0.707 | yes | yes |

**Band 6 — Low-Pass**

| Parameter | Name | Range | Units | Default | Automatable | V1? |
|---|---|---|---|---|---|---|
| B6 On | Low-Pass Enable | 0/1 | — | 0 (off) | yes | yes |
| B6 Freq | Low-Pass Frequency | 1000-20000 | Hz | 18000 | yes | yes |
| B6 Slope | Low-Pass Slope | 12/24/36/48 | dB/oct | 12 | yes | yes |

**Master**

| Parameter | Name | Range | Units | Default | Automatable | V1? |
|---|---|---|---|---|---|---|
| Bypass | Bypass | 0/1 | — | 0 (off) | yes | yes |

Total V1 parameters: 22 (reduced from 41).

### Parameter Mapping Strategy

- **Frequency**: Logarithmic, per-band range (not full 20-20kHz for all bands). Each band has a musically useful sub-range.
- **Gain**: Linear dB, -18 to +18. 0.5 normalized = 0 dB.
- **Q**: Linear 0.1-10.0. Default 0.707 for shelves (Linkwitz-Riley alignment), 1.0 for bells.
- **Slope**: Discrete steps (12/24/36/48 dB/oct). Maps to 1/2/3/4 cascaded biquad stages.
- **Enable**: Boolean, threshold 0.5.

### State Compatibility

- New magic value: `0x45510002` (version 2).
- `loadState()` must handle both v1 and v2:
  - v1 (magic `0x45510001`): Load first 6 bands' params, ignore bands 7-8, map type enum to fixed band layout.
  - v2 (magic `0x45510002`): Load directly.
- Unknown magic: Reject (return false).

---

## Phase 4 — DSP Plan

### Filter Topology

Series biquad processing per channel. Each band is one or more cascaded second-order IIR sections (Direct Form I or II).

For V1, use **Direct Form II Transposed** (DF2T) for better numerical behavior with floating-point:

```
y[n] = b0*x[n] + w1
w1 = b1*x[n] - a1*y[n] + w2
w2 = b2*x[n] - a2*y[n]
```

### Coefficient Calculation

RBJ Audio EQ Cookbook formulae (already implemented). No change needed for the math — the existing `designBiquad()` function is correct.

For cut filters (HP/LP), cascaded Butterworth stages:
- Each stage uses Q = 0.70710678 (Butterworth alignment).
- 1 stage = 12 dB/oct, 2 stages = 24 dB/oct, 3 stages = 36 dB/oct, 4 stages = 48 dB/oct.

### Sample-Rate Changes

`initialize(sampleRate, maxBlockSize)` must:
1. Store new sample rate.
2. Recompute all filter coefficients.
3. Filter state can be preserved (state variables are sample-rate independent), but coefficients must be recalculated.

If `process()` is called with a different sample rate than initialized (shouldn't happen, but defensive), detect via a stored sample rate and reinitialize.

### Smoothing Strategy

**Per-sample parameter smoothing** (matching the compressor pattern):

For each parameter, maintain a smoothed value that converges to the target:

```cpp
smoothed += (target - smoothed) * coeff;
```

Where `coeff = 1.0 - exp(-1.0 / (sampleRate * timeConstant))`.

Time constants:
- Frequency: 5ms (fast enough for automation, smooth enough to avoid zipper noise)
- Gain: 5ms
- Q: 5ms
- Enable: 10ms (crossfade for click-free enable/disable)
- Slope: 20ms (requires coefficient rebuild, needs slower transition)

**Coefficient interpolation**: Rather than interpolating coefficients (which can cause instability), interpolate the *parameters* (frequency, gain, Q) and recompute coefficients from smoothed parameters. This is more expensive but guarantees stable filter equations at every sample.

For V1, use **block-level coefficient update** (recompute every N samples, e.g., every 16 or 32 samples) to reduce CPU cost while maintaining smooth transitions. This is the standard approach in professional EQ plugins.

### Bypass Behavior

Hard bypass: When bypass is active, copy input to output with no processing. No crossfade needed for V1 (crossfade bypass is a polish feature).

The existing bypass implementation (`AestraEQ.h:289-299`) is correct.

### Band Enable/Disable Behavior

When a band is disabled, skip its filter processing entirely (existing behavior at `AestraEQ.h:317`). No crossfade for V1.

When re-enabling, the filter state (w1, w2) should be zero or the previous state. Zero is safest to avoid transients from stale state.

### Numerical Stability

1. **Coefficient validation**: Already implemented in `setCoeffs()` — check for NaN/Inf, fall back to passthrough.
2. **Input sanitization**: Add NaN/Inf check on input samples (matching compressor's `sanitizeSample()`). Replace with 0.0f and reset filter state.
3. **Output clamping**: Not needed for V1 (let the host handle clipping).
4. **Denormal protection**: Global FTZ/DAZ is already set in `AudioEngine.cpp:29`. The EQ runs within the audio engine's callback, so denormals are flushed. No per-filter action needed.
5. **Filter state reset**: If output is NaN/Inf, reset all filter state to zero (already implemented in `BiquadFilter::process()`).

### Real-Time Safety

- **No allocation in `process()`**: Current implementation allocates nothing in the audio path. The `memcpy`/`memset` calls are safe.
- **No locks**: All parameter communication uses atomics with relaxed ordering. Coefficient update uses acquire/release on `m_filtersDirty`.
- **No system calls**: No file I/O, no network, no time queries in the audio path.

### Stereo Behavior

Stereo-linked: Both channels process through the same filter parameters (same coefficients). Filter state is per-channel (independent), which is correct — different channels have different signals.

### Expected Latency

Zero samples. Biquad filters are causal with no lookahead.

---

## Phase 5 — UI/UX Plan

### Visual Identity

Match the Aestra Verb and Aestra Compressor visual language:
- Dark glassmorphism background.
- Purple/cyan accent colors.
- Blueprint-style grid.
- Clean, modern, no fake analog rack styling.
- Consistent knob style (the `drawBlueprintKnob()` function already exists).

### Proposed Layout (1180 x 640)

```
┌──────────────────────────────────────────────────────────────────┐
│  [EQ]  AESTRA EQ                         [Bypass]    [Close]    │  Title bar (50px)
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──┐                                                    ┌──┐    │
│  │IN│    ┌──────────────────────────────────────────┐    │OUT│   │  Curve area (260px)
│  │  │    │                                          │    │  │   │
│  │  │    │         Response Curve + Nodes           │    │  │   │
│  │  │    │                                          │    │  │   │
│  └──┘    └──────────────────────────────────────────┘    └──┘   │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│  [HPF]  [LSh]  [Bell1]  [Bell2]  [HSh]  [LPF]                   │  Band panels (140px)
│   F/S    F/G/Q  F/G/Q    F/G/Q   F/G/Q   F/S                   │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│  Latency: 0 samples    Bands: 6    Version 1.0                  │  Status bar (24px)
└──────────────────────────────────────────────────────────────────┘
```

### Curve Display

- **Response curve**: Total EQ magnitude response computed from all active bands. Drawn as a smooth polyline with glow effect (existing implementation is good).
- **Per-band curves**: Optionally draw individual band responses as faint lines for educational value. Defer to post-V1 polish.
- **Grid**: dB on Y axis (-18 to +18), frequency on X axis (log 20Hz-20kHz). Standard EQ graph.
- **0 dB line**: Highlighted with brighter line.

### Draggable Band Nodes

Each enabled band has a draggable node on the curve:
- **X position**: Frequency.
- **Y position**: Gain (for shelf/bell types). For HP/LP, Y controls slope or stays at center.
- **Scroll wheel on node**: Adjust Q.
- **Right-click on node**: Context menu for band-specific options (if any).

This interaction model already exists in the current editor and works well.

### Knob/Sidebar Controls

Per-band panel below the curve:
- **HPF/LPF bands**: Enable toggle, Frequency knob, Slope selector (discrete buttons: 12/24/36/48).
- **Shelf/Bell bands**: Enable toggle, Frequency knob, Gain knob, Q knob.
- Each knob shows its current value on hover/interaction.

### Band Enable Toggles

Visual indicator: Colored dot (enabled) vs. dim dot (disabled). Click to toggle. Disabled bands are grayed out in the panel and their curve/node disappears.

### Analyzer

**Defer the spectrum analyzer to post-V1.** The current Goertzel analyzer works but adds complexity. V1 should focus on trustworthy DSP. The curve display alone is sufficient for EQ work.

If the analyzer is kept:
- Make it optional (toggle in title bar).
- Remove the background thread; compute on the audio thread via ring buffer + FFT on the UI thread (simpler lifecycle).
- Keep it subtle — faint bars behind the curve, not a dominant visual element.

### Input/Output Meters

Simple peak meters on the input/output panels. Connected to actual audio levels (not static fills). Implementation: track peak per block, smooth with attack/release, display as vertical bar.

### Visual Consistency

- Use the same knob rendering (`drawBlueprintKnob()`), color palette, and layout rhythm as Aestra Compressor and Aestra Verb.
- Title bar: Logo, name, bypass toggle, close button. No undo/redo chips, no preset display, no A/B, no S/M/R icons (all scope creep for V1).
- Remove the utility strip entirely — it's full of non-functional controls.

---

## Phase 6 — Test Plan

### Unit Tests

| Test | Description | Priority |
|---|---|---|
| Flat EQ equals input | All bands bypassed/enabled with 0 gain → output matches input within tolerance | Required |
| Bypass parity | Bypass ON → output equals input exactly (memcpy) | Required |
| HPF attenuates low freq | 100Hz HPF + 80Hz sine → output RMS < input RMS * 0.5 | Required |
| LPF attenuates high freq | 5kHz LPF + 10kHz sine → output RMS < input RMS * 0.5 | Required |
| Low shelf boosts bass | 200Hz shelf +6dB + 100Hz sine → output > input by ~6dB | Required |
| High shelf boosts treble | 8kHz shelf +6dB + 10kHz sine → output > input by ~6dB | Required |
| Bell boost at target freq | 1kHz bell +12dB + 1kHz sine → output > input by ~12dB | Required |
| Bell cut at target freq | 1kHz bell -12dB + 1kHz sine → output < input by ~12dB | Required |
| Coefficient stability at extremes | Freq=20Hz, gain=+18, Q=10 → no NaN/Inf in output | Required |
| Coefficient stability at Nyquist | Freq near sampleRate/2 → no NaN/Inf | Required |
| Sample-rate change | Reinitialize at 44100, 48000, 96000 → process without crash | Required |
| State roundtrip | Set params → saveState → new instance → loadState → verify params match | Required |
| Old state v1 loading | Inject v1 blob → loadState → verify graceful handling | Required |
| Silence stability | Process 1024 frames of silence → output is silence (no denormal drift) | Required |
| NaN/Inf input safety | Feed NaN/Inf samples → verify output recovers to valid audio | Required |
| Extreme gain stability | +18dB on all bands simultaneously → no sustained NaN/Inf | Required |
| No process-path allocation | Verify `process()` makes no heap allocations (if testable) | Recommended |
| Parameter smoothing (future) | Set param → process → verify output transitions smoothly, no clicks | Deferred until smoothing is implemented |

### EQ Quality Lab

A manual/automated lab for subjective and objective quality assessment:

| Source | Description | Purpose |
|---|---|---|
| Silence | Zero input | Verify no drift, no denormal accumulation |
| 1kHz sine | Calibrated tone at -12 dBFS | Verify gain accuracy |
| Sine sweep | 20Hz-20kHz logarithmic sweep, 10 seconds | Verify filter response across spectrum |
| Pink noise | Flat spectrum noise | Verify frequency response shape |
| Vocal-ish | Band-limited signal (300Hz-3kHz fundamental + harmonics) | Verify musical behavior |
| Bass pulse | 60Hz sine burst (100ms on, 400ms off) | Verify transient response and filter ringing |
| Chord/pad | Multi-frequency sustained signal | Verify inter-band interaction |
| Mix bus | Real music excerpt (if available) | Subjective quality check |
| Extreme sweep | All parameters swept from min to max simultaneously | Verify stability under rapid change |

---

## Phase 7 — Implementation Plan

### Stage 1: Clean/Verify EQ DSP Core

- [ ] Switch BiquadFilter to DF2T form for better numerical behavior.
- [ ] Add input sanitization (NaN/Inf → 0, reset state).
- [ ] Initialize `m_params` to default values in constructor.
- [ ] Validate band type bounds on `loadState()`.
- [ ] Handle state version in `loadState()` (v1 → v2 migration).
- [ ] Hard-code 6 bands (or keep 8 but only expose 6 in V1).
- [ ] Remove `Notch`, `BandPass`, `Tilt` filter types from V1 (keep code, hide from UI).
- [ ] Fix `hasEditor` in `PluginInfo` to return `true`.

### Stage 2: Truthful V1 Parameter Surface

- [ ] Define 22 V1 parameters (6 bands x 3-4 params + bypass).
- [ ] Per-band frequency ranges (not full 20-20kHz for all bands).
- [ ] Per-band default values.
- [ ] Parameter descriptors with correct names, ranges, units.
- [ ] Remove or hide unused parameters (types 5-7, bands 7-8).

### Stage 3: Parameter Smoothing

- [ ] Add smoothed parameter storage (matching compressor pattern).
- [ ] Per-sample or per-block smoothing in `process()`.
- [ ] Smooth frequency, gain, Q with 5ms time constant.
- [ ] Recompute coefficients from smoothed parameters.
- [ ] Verify no clicks on parameter changes.

### Stage 4: State Compatibility

- [ ] New state blob format (v2) with version field.
- [ ] `loadState()` handles v1 → v2 migration gracefully.
- [ ] `loadState()` rejects unknown versions.
- [ ] State roundtrip test passes.

### Stage 5: Tests

- [ ] Implement all "Required" tests from Phase 6.
- [ ] Verify all tests pass.
- [ ] Add NaN/Inf recovery test.
- [ ] Add silence stability test.
- [ ] Add state roundtrip test.

### Stage 6: EQ Quality Lab

- [ ] Create `Tests/AestraAudio/EQQualityLab.cpp`.
- [ ] Implement all sources from Phase 6 lab table.
- [ ] Generate WAV output for manual review.
- [ ] Verify frequency response matches expected curves.

### Stage 7: Editor/Metering/Curve Display

- [ ] Simplify editor to V1 layout (remove dynamic EQ section, utility strip, guard panels).
- [ ] Connect input/output meters to actual peak levels.
- [ ] Implement per-band enable toggles.
- [ ] Implement slope selector for HPF/LPF bands.
- [ ] Right-click context menu for band type (if keeping flexible types).
- [ ] Verify graph node drag interaction.
- [ ] Remove false "Oversampling: 2x" claim.
- [ ] Remove non-functional UI elements (A/B, S/M/R, undo/redo, presets).

### Stage 8: Visual Polish

- [ ] Color consistency with Verb/Compressor.
- [ ] Band panel layout refinement.
- [ ] Value display on knob interaction.
- [ ] Tooltip/help text for parameters.
- [ ] Keyboard shortcuts (if applicable).

### Stage 9: Freeze Note

- [ ] Write `docs/audits/aestra-eq-v1-freeze-note.md`.
- [ ] Document final parameter surface, test results, known limitations.
- [ ] Commit as `eq: freeze V1`.

---

## Phase 8 — Summary and Recommendation

### Current EQ State

The Aestra EQ is a substantially implemented 8-band parametric equalizer with working DSP, a rich (if overbuilt) editor, spectrum analyzer, and basic tests. It is **not truthful enough for V1** in its current form because:

1. No parameter smoothing (clicks on any change).
2. The UI exposes features that don't exist (dynamic EQ, oversampling, A/B, stereo mode, presets).
3. State compatibility has no version migration path.
4. The parameter surface is larger than needed (8 bands, 8 filter types, 41 params).

### Recommended V1 Direction

**Reduce scope, add smoothing, fix state compatibility.**

- 6 bands with fixed types (HP, LSh, Bell, Bell, HSh, LP).
- 22 parameters.
- Per-sample parameter smoothing with coefficient recomputation.
- Clean state v2 with v1 migration.
- Simplified editor matching the Verb/Compressor visual language.
- No dynamic EQ, no analyzer, no oversampling, no M/S, no A/B, no presets.

### Risks / Open Questions

1. **Fixed vs. flexible band types**: Fixed types simplify the UI but reduce flexibility. If Dylan wants flexible types, the current 8-type enum can stay — but the UI needs a clean type selector, not the current context-menu approach.
2. **Parameter count**: 22 params is clean, but may be too few if Dylan wants per-band slope for all bands. Could add slope to shelves (making them 2nd-order shelves with adjustable Q).
3. **Analyzer retention**: The current analyzer works and is RT-safe. Keeping it (behind a toggle) adds value with low risk. Removing it simplifies the codebase.
4. **Band count**: 6 vs 8. If 8, the DSP cost is still modest (128 biquad filters max for stereo 8-band with 4-stage slopes). The real argument for 6 is UI simplicity.

### Recommended Next Pass

**Implementation Stage 1-3**: Clean DSP core, truthful V1 parameter surface, parameter smoothing. This is the foundation — everything else depends on trustworthy DSP.

---

*Discovery pass complete. No DSP or UI code was changed. Working tree remains clean.*
