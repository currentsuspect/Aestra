# Aestra Verb Beta Freeze Note

Status: FREEZE REOPENED — Group B1/B2/B4 delivered (2026-06-08)

Reopened and delivered: 6 new modes (Cathedral, Chamber, Bright Hall, Ambience, Scoring, Smooth Plate),
post-reverb EQ (kLowCut + kHighCut), freeze button (kFreeze), state format v3→v4 with backward migration.

Reviewed from `develop` at `26c7f680200fb80a7b7154d89380a9bba4fbdcfc`.

## Current Parameter Set (v4)

14 parameters: Decay, Damping, Predelay, Width, Mix, Bypass, Size, Diffusion, Mod Rate, Mod Depth,
Mode (9 modes), Low Cut, High Cut, Freeze.

## Locked Behavior

- The modulated 8-line FDN topology, pre-diffusion, early reflection network, wet voicing, mode gain
  compensation, mud cleanup, Plate post-allpass, and Plate resonance cut are locked unless a real bug is found.
- Existing safety clamp and sanitization behavior remains protected.
- SIMD dispatch behavior remains scalar/SSE/AVX2/NEON compatible through the existing `ReverbSIMD` path.
- State format v4 is now the canonical version. v1/v2/v3 states load with new params at defaults.

## Allowed Future Polish

- More factory presets (aim for 40–60).
- Preset artwork and tooltip copy.
- User preset save/load.
- Preset navigation (◀ ▶) + A/B compare + double-click reset.
- Mix Lock.
- Micro UI spacing.
- Additional listening examples.
- User-facing documentation.

## Not Allowed Without Reopening Freeze

- Major DSP topology rewrite.
- Changing public parameter identity or automation compatibility.
- Removing existing safety, material, or SIMD regression coverage.
- Adding new modes beyond the current 9 (mode system expansion).

## Test Evidence

- `cmake --build build-linux --target ReverbSIMDParityTest ReverbSafetyRegressionTest AestraReverbMaterialLab AestraReverbQualityLab AestraReverbBenchmark --parallel`
- `ctest --test-dir build-linux -R 'Reverb(SIMDParity|SafetyRegression|MaterialLab)Test' --output-on-failure`: passed, 2/2.
- `./build-linux/Tests/AestraReverbQualityLab`: passed; quality baseline measured for Room/Hall/Plate.
- `./build-linux/Tests/AestraReverbMaterialLab`: passed; all regression checks passed, zero clamps.

## Final Known Limitations

- Diagnostics-enabled material lab still reports known vocal Room/Plate clamp cases in the synthetic vocal source. The lab classifies them as pre-existing and non-failing; non-vocal sources do not clamp.
- Full host/plugin lifecycle validation beyond the internal built-in instance path was not expanded.
- Material lab metrics are synthetic evidence, not a replacement for final human listening on production monitors.
