# Aestra EQ V1 Implementation

Date: 2026-05-01

## Final V1 Parameter Surface

23 parameters total (22 band params + bypass).

| ID | Name | Short | Unit | Default (norm) | Range | Step | Band |
|---|---|---|---|---|---|---|---|
| 0 | High-Pass Enable | HP On | — | 0.0 | 0–1 | 1 | HPF |
| 1 | High-Pass Frequency | HP Freq | Hz | 0.392 (80 Hz) | 20–500 | 0 | HPF |
| 2 | High-Pass Slope | HP Slp | dB/oct | 0.333 (24) | 12/24/36/48 | 3 | HPF |
| 3 | Low Shelf Enable | LS On | — | 0.0 | 0–1 | 1 | LSh |
| 4 | Low Shelf Frequency | LS Freq | Hz | 0.370 (200 Hz) | 40–1000 | 0 | LSh |
| 5 | Low Shelf Gain | LS Gain | dB | 0.5 (0 dB) | -18–+18 | 0 | LSh |
| 6 | Low Shelf Q | LS Q | — | 0.061 (0.707) | 0.1–10.0 | 0 | LSh |
| 7 | Bell 1 Enable | B1 On | — | 1.0 | 0–1 | 1 | Bell1 |
| 8 | Bell 1 Frequency | B1 Freq | Hz | 0.430 (500 Hz) | 80–8000 | 0 | Bell1 |
| 9 | Bell 1 Gain | B1 Gain | dB | 0.5 (0 dB) | -18–+18 | 0 | Bell1 |
| 10 | Bell 1 Q | B1 Q | — | 0.091 (1.0) | 0.1–10.0 | 0 | Bell1 |
| 11 | Bell 2 Enable | B2 On | — | 1.0 | 0–1 | 1 | Bell2 |
| 12 | Bell 2 Frequency | B2 Freq | Hz | 0.607 (2000 Hz) | 200–16000 | 0 | Bell2 |
| 13 | Bell 2 Gain | B2 Gain | dB | 0.5 (0 dB) | -18–+18 | 0 | Bell2 |
| 14 | Bell 2 Q | B2 Q | — | 0.091 (1.0) | 0.1–10.0 | 0 | Bell2 |
| 15 | High Shelf Enable | HS On | — | 0.0 | 0–1 | 1 | HSh |
| 16 | High Shelf Frequency | HS Freq | Hz | 0.765 (8000 Hz) | 2000–20000 | 0 | HSh |
| 17 | High Shelf Gain | HS Gain | dB | 0.5 (0 dB) | -18–+18 | 0 | HSh |
| 18 | High Shelf Q | HS Q | — | 0.061 (0.707) | 0.1–10.0 | 0 | HSh |
| 19 | Low-Pass Enable | LP On | — | 0.0 | 0–1 | 1 | LPF |
| 20 | Low-Pass Frequency | LP Freq | Hz | 0.926 (18000 Hz) | 1000–20000 | 0 | LPF |
| 21 | Low-Pass Slope | LP Slp | dB/oct | 0.0 (12) | 12/24/36/48 | 3 | LPF |
| 22 | Bypass | BYP | — | 0.0 | 0–1 | 1 | Master |

## Fixed Band Model

| Band | Type | Purpose |
|---|---|---|
| 0 | High-Pass (LowCut) | Sub-bass removal |
| 1 | Low Shelf | Bass body |
| 2 | Bell | Parametric 1 |
| 3 | Bell | Parametric 2 |
| 4 | High Shelf | Air/brilliance |
| 5 | Low-Pass (HighCut) | Hiss removal |

## Frequency Mapping

Each band has its own logarithmic frequency range:

| Band | Min Hz | Max Hz | Formula |
|---|---|---|---|
| HPF | 20 | 500 | `20 * 25^norm` |
| LSh | 40 | 1000 | `40 * 25^norm` |
| Bell1 | 80 | 8000 | `80 * 100^norm` |
| Bell2 | 200 | 16000 | `200 * 80^norm` |
| HSh | 2000 | 20000 | `2000 * 10^norm` |
| LPF | 1000 | 20000 | `1000 * 20^norm` |

## Deprecated/Hidden Features

The following features from the old 8-band EQ are NOT exposed in V1:

- **Bands 6–7**: Not exposed. Internal array still has 8 slots for editor compatibility.
- **Type parameter**: Band types are fixed. The type parameter (sub-index 1 in old layout) is not in V1 descriptors.
- **Notch, BandPass, Tilt filter types**: Not exposed. FilterType enum still exists internally.
- **Dynamic EQ**: Entirely removed from DSP. Editor section still renders (decorative only).
- **A/B comparison**: Not implemented.
- **Mid/Side**: Not implemented.
- **Analyzer**: Still functional (double-buffered mono FFT via Goertzel). No changes.
- **Oversampling**: Status bar claim removed. None exists.
- **Presets**: Not implemented.

## State Compatibility

### V2 State Format

```
struct EQStateBlobV2 {
    uint32_t magic;     // 0x45510002
    uint32_t version;   // 2
    float params[23];   // kV1ParamCount
};
```

### V1 Migration

Old V1 state (magic 0x45510001, 188 bytes) is migrated:

1. For each of the 6 bands: if the old band type matches the target fixed type, migrate frequency/gain/Q. Otherwise load defaults.
2. Bands 6–7 are ignored.
3. Bypass is migrated from old param index 40.
4. Corrupt/unknown blobs fail safely (return false).

### Backward Compatibility

- Plugin ID remains `com.Aestrastudios.eq`.
- `loadState()` handles both V1 and V2 blobs.
- `saveState()` always writes V2.
- Unknown magic values are rejected.

## Smoothing Strategy

- **Time constant**: 5ms (`coeff = 1 - exp(-1 / (sampleRate * 0.005))`)
- **Block size**: 16 samples per smoothing update
- **Smoothed parameters**: All continuous params (frequency, gain, Q/slope). Enable and bypass are not smoothed.
- **Coefficient rebuild**: Every 16-sample block, coefficients are recomputed from smoothed values for enabled bands only.
- **No coefficient interpolation**: Parameters are smoothed, then coefficients computed from smoothed values. This guarantees stable filter equations at every update.

## DSP Architecture

- **Topology**: Series biquad processing per channel.
- **Form**: Direct Form I (unchanged from original).
- **Coefficients**: RBJ Audio EQ Cookbook.
- **HPF/LPF slope**: Cascaded Butterworth stages (Q=0.707). 12/24/36/48 dB/oct = 1/2/3/4 stages.
- **Shelf/Bell**: Single biquad stage each.
- **Stereo**: Shared coefficients, independent per-channel filter state.
- **Latency**: 0 samples.
- **NaN/Inf handling**: Input sanitization in BiquadFilter. Coefficient validation. Filter state reset on instability.
- **Denormal handling**: Global FTZ/DAZ set in AudioEngine.

## Test Results

24/24 tests pass:

| Test | Description |
|---|---|
| Direct construction safe defaults | Params initialized without applyInfo() |
| V1 descriptor count is 23 | getParameters() returns 23 params |
| No type params in V1 descriptors | No "Type" in descriptor names |
| hasEditor returns true | Plugin metadata correct |
| getParameterCount returns 23 | Correct param count |
| Latency is 0 samples | Zero latency confirmed |
| Bypass parity | Bypassed output equals input |
| Flat EQ equals input | 0 dB gain bands pass audio unchanged |
| HPF attenuates 40 Hz | High-pass removes low frequencies |
| LPF attenuates 10 kHz | Low-pass removes high frequencies |
| Low shelf boosts 80 Hz (+12 dB) | Shelf gain applied correctly |
| High shelf boosts 10 kHz (+18 dB) | Shelf gain applied correctly |
| Bell boost at 500 Hz (+12 dB) | Parametric boost works |
| Bell cut at 500 Hz (-12 dB) | Parametric cut works |
| Extreme values no NaN/Inf | All bands at extremes, no instability |
| Sample rate 44.1k | Works at 44.1 kHz |
| Sample rate 96k | Works at 96 kHz |
| State V2 roundtrip | Save/load preserves all params |
| Legacy V1 state migration | Old 8-band state loads safely |
| Corrupt state fails safely | Invalid blobs rejected |
| Short state fails safely | Truncated blobs rejected |
| NaN input recovers | NaN input doesn't permanently corrupt |
| Silence stability | 100 blocks of silence stays silent |
| Smoothing no discontinuity | Param changes don't produce NaN/Inf |

## Known Limitations

- **Editor not simplified**: Still shows 8 bands, dynamic EQ section, utility strip, guard panels, and fake A/B/S/M/R elements. Needs a V1 simplification pass.
- **Analyzer still active**: Double-buffered Goertzel spectrum analyzer is functional but not V1-priority.
- **No flexible band types**: V1 has fixed types per band. Type parameter is hidden.
- **No input/output gain trim**: Not in V1 surface.
- **No dynamic EQ**: Removed/deferred.
- **No oversampling**: None exists.
- **No M/S**: None exists.
- **No A/B**: None exists.
- **No presets**: None exists.

## Files Changed

| File | Change |
|---|---|
| `AestraAudio/include/Plugin/AestraEQ.h` | Full rewrite: 6-band V1 DSP, 23-param surface, per-block smoothing, state V2 with V1 migration, input sanitization, DF2T biquad |
| `AestraAudio/src/Plugin/BuiltInPlugins.cpp` | `hasEditor = true`, `version = "1.0.0"`, `numAudioInputs = 2` |
| `AestraUI/Widgets/AestraEQEditor.h` | `kNumBands = 6`, BandControl with V1 param IDs |
| `AestraUI/Widgets/AestraEQEditor.cpp` | V1 param IDs, per-band frequency mapping, slope display for cut bands |
| `Tests/AestraAudio/AestraEQTest.cpp` | Full rewrite: 24 V1 contract tests |

## Next Recommended Pass

**Editor simplification**: Remove dynamic EQ section, utility strip, guard panels, fake A/B/S/M/R. Simplify to 6-band layout with proper per-band controls and curve display. This is the remaining visual work before EQ V1 freeze.
