# Aestra Verb Beta Freeze Note

Status: FROZEN WITH NOTES

Reviewed from `develop` at `26c7f680200fb80a7b7154d89380a9bba4fbdcfc`.

## Locked Behavior

- Room, Hall, and Plate remain the core material modes.
- Existing public parameters remain stable: Decay, Damping, Predelay, Width, Mix, Bypass, Size, Diffusion, Mod Rate, Mod Depth, and Mode.
- Current modulated FDN topology, pre-diffusion, early reflection network, wet voicing, mode gain compensation, mud cleanup, Plate post-allpass, and Plate resonance cut are locked for beta unless a real bug is found.
- Existing safety clamp and sanitization behavior remains protected.
- SIMD dispatch behavior remains scalar/SSE/AVX2/NEON compatible through the existing `ReverbSIMD` path.

## Allowed Future Polish

- More factory presets.
- Preset artwork and tooltip copy.
- Micro UI spacing.
- Additional listening examples.
- User-facing documentation.
- Factory preset gain staging.

## Not Allowed Without Reopening Freeze

- Major DSP topology rewrite.
- Replacing the Room/Hall/Plate mode system.
- Changing public parameter identity or automation compatibility.
- Changing freeze/render semantics if a future freeze control is added.
- Removing existing safety, material, or SIMD regression coverage.

## Test Evidence

- `cmake --build build-linux --target ReverbSIMDParityTest ReverbSafetyRegressionTest AestraReverbMaterialLab AestraReverbQualityLab AestraReverbBenchmark --parallel`
- `ctest --test-dir build-linux -R 'Reverb(SIMDParity|SafetyRegression|MaterialLab)Test' --output-on-failure`: passed, 3/3.
- `./build-linux/Tests/AestraReverbQualityLab`: passed; refreshed `labs/reverb/quality/reverb_quality_baseline.*`.
- `./build-linux/Tests/AestraReverbMaterialLab`: passed; generated synthetic material and reports.
- `cmake -S . -B build-reverb-freeze -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_UI=OFF -DAESTRA_ENABLE_TESTS=ON -DAESTRA_REVERB_DIAGNOSTICS=ON -DAESTRA_REVERB_PROFILE=ON -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build-reverb-freeze --target ReverbSIMDParityTest ReverbSafetyRegressionTest AestraReverbMaterialLab AestraReverbQualityLab AestraReverbBenchmark --parallel 2`: passed.
- `ctest --test-dir build-reverb-freeze -R 'Reverb(SIMDParity|SafetyRegression|MaterialLab)Test' --output-on-failure`: passed, 3/3.
- `./build-reverb-freeze/Tests/AestraReverbMaterialLab`: passed with diagnostics enabled; non-vocal clamp count was 0.
- `./build-reverb-freeze/Tests/AestraReverbBenchmark`: passed; profile/diagnostics build remained more than 10x realtime on this machine.

## Final Known Limitations

- The plugin code reviewed here does not expose a dedicated AestraVerb freeze parameter. Freeze behavior is therefore not directly auditable in `AestraVerb` beyond general lifecycle/tail/safety behavior.
- Diagnostics-enabled material lab still reports known vocal Room/Plate clamp cases in the synthetic vocal source. The lab classifies them as pre-existing and non-failing; non-vocal sources do not clamp.
- Full host/plugin lifecycle validation beyond the internal built-in instance path was not expanded in this freeze pass.
- Material lab metrics are synthetic evidence, not a replacement for final human listening on production monitors.
