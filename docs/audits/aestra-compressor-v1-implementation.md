# Aestra Compressor V1 Implementation

Status: implemented for `com.Aestrastudios.comp`.

## Final V1 Control Surface

- Threshold
- Ratio
- Attack
- Release
- Makeup Gain
- Knee
- Mix
- Bypass
- Input Gain
- Output Gain
- Detector HPF

The user-facing name is now `Aestra Compressor`. The plugin ID remains `com.Aestrastudios.comp` and is the compatibility anchor.

## Deprecated And Hidden Parameters

The old broad compressor state included detector mode, topology, hold, auto release, range, lookahead, stereo link, link law, sidechain HPF/LPF/listen, output trim, style, quality, and hidden soft clipping behavior.

V1 hides or ignores the non-V1 controls:

- Detector mode: V1 uses one clear peak detector.
- Topology: V1 is feed-forward only.
- Hold, auto release, range: ignored.
- Lookahead: ignored; V1 latency is zero samples.
- Stereo link/link law: fixed linked stereo detection.
- SC LPF and SC listen: ignored.
- Style and quality: ignored.
- Soft clipping/saturation: removed from the compressor path.

Old SC HPF maps to the V1 Detector HPF because it is internal detector filtering, not external sidechain UX.

## DSP Model

The V1 compressor is a scalar zero-latency feed-forward compressor:

- input sanitization for NaN/Inf/extreme samples;
- input gain before detection and compression;
- linked stereo peak detection using the maximum detector level;
- optional one-pole detector HPF with coefficients updated outside the sample loop;
- sample-rate-derived attack/release envelope;
- threshold/ratio/knee gain computer in dB;
- makeup gain after gain reduction;
- dry/wet mix;
- output gain after mix;
- denormal flushing on output.

`process()` performs no vector resizing, state serialization, logging, locks, or heap allocation. The previous `RMSDetector::setWindowSize()` process-path allocation risk was removed with the RMS detector.

## State Compatibility

Current saves use the existing compressor V2 magic with a V3 version field and the legacy 22-float storage shape. V1 parameters roundtrip through this blob.

Old V1 blobs load the original first 8 controls and default the new V1 input/output/Detector HPF controls. Old V2 blobs load the first 8 controls, map old output trim to Output Gain, map old SC HPF to Detector HPF, and ignore deprecated behavior fields. Invalid or truncated blobs fail without mutating into an unsafe state.

## Tests

Updated tests:

- `AestraCompPhase0Test`: V1 DSP behavior.
- `AestraCompPhase1Test`: public parameter surface, clamping, state, invalid state, and process-path buffer-risk contract.
- `AestraCompUpgradeTest`: old V1/V2 blob compatibility and plugin identity.

Covered behavior includes silence, bypass parity, static gain reduction, hard/soft knee behavior, attack, release, 44.1/48/96 kHz consistency, mix positions, input/output/makeup gain behavior, linked stereo imaging, NaN/Inf/extreme sample handling, normalized clamping, state roundtrip, and old blob loading.

## Material Lab

Baseline files:

- `labs/compressor/quality/compressor_quality_baseline.md`
- `labs/compressor/quality/compressor_quality_baseline.json`

Materials include silence, sine tone, bass pulse, transient/snare, vocal-ish sustain, chord/pad, simple mix bus, and an extreme sweep case.

## Known Limitations

- No external sidechain UX.
- No lookahead.
- No saturation, clipper, or limiter.
- No modes, style, or quality choices.
- No auto gain.
- No advanced stereo link controls.
- No transfer curve display.
- No custom premium UI in this pass.
- No generic plugin meter bus yet.
- Hot output can exceed 0 dBFS by design.

## Deferred Features

- Modes.
- Auto gain.
- External sidechain UX.
- Lookahead.
- Saturation/clipper as a separate explicit processor if needed.
- Advanced stereo link controls.
- Transfer curve display.
- Custom premium UI.

## Validation Pass Results

Validation pass commit scope: Compressor V1 quality tests, reproducible material lab, and documentation only. No DSP or public parameter changes were made.

Tests added or strengthened:

- `AestraCompPhase0Test` now directly verifies that Detector HPF reduces low-frequency detector triggering.
- `AestraCompPhase1Test` now verifies built-in metadata: display name `Aestra Compressor`, plugin ID `com.Aestrastudios.comp`, and Dynamics effect classification.
- `AestraCompressorMaterialLab` was added as a deterministic, hardware-free lab target.

Material lab target:

- Target: `AestraCompressorMaterialLab`
- Source: `Tests/AestraAudio/AestraCompressorMaterialLab.cpp`
- CTest name: `AestraCompressorMaterialLab`
- Outputs:
  - `labs/compressor/quality/compressor_quality_baseline.md`
  - `labs/compressor/quality/compressor_quality_baseline.json`

Material lab cases:

- silence
- sine tone
- bass pulse
- snare/transient
- vocal-ish sustained signal
- chord/pad
- simple mix bus
- extreme sweep

Metrics emitted:

- peak in/out
- RMS in/out
- max gain reduction
- average gain reduction
- clipping count
- NaN/Inf count
- bypass parity result
- max absolute sample
- sanity result

Validation commands run:

```bash
cmake -S . -B build/headless -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/headless --target AestraCompPhase0Test AestraCompPhase1Test AestraCompUpgradeTest AestraCompressorMaterialLab AestraEQTest AestraDelayUpgradeTest --parallel 2
ctest --test-dir build/headless -R "AestraComp|AestraCompressorMaterialLab|AestraEQTest|AestraDelayUpgradeTest" --output-on-failure
python -m json.tool labs/compressor/quality/compressor_quality_baseline.json
```

Results:

- Compressor tests: passed.
- Compressor material lab: passed and regenerated the baseline files.
- Nearby built-in effect tests (`AestraEQTest`, `AestraDelayUpgradeTest`): passed.
- JSON baseline validation: passed.

Quality issue classification:

- REQUIRED: none found in DSP.
- RECOMMENDED: keep the reproducible lab target in CI-facing test builds.
- DEFER: custom editor, generic plugin meter bus, transfer curve display, auto gain, external sidechain UX, advanced stereo link controls.
- REJECT: modes, saturation, lookahead, analog modeling, and any public parameter expansion for V1.

Final quality decision:

The Compressor V1 core is ready for the UI/metering pass. Remaining work should focus on presentation, metering integration, and workflow polish without changing the V1 DSP contract.

## UI And Metering Pass

Status: implemented around the existing `AestraCompEditor` route. No DSP or public parameter changes were made.

Files changed:

- `AestraUI/Widgets/AestraCompEditor.h`
- `AestraUI/Widgets/AestraCompEditor.cpp`

Editor path discovered:

- `PluginUIController::openPluginEditor()` routes `com.Aestrastudios.comp` to `AestraCompEditor`.
- `AestraUI/CMakeLists.txt` already builds `AestraCompEditor` into `AestraUI_Core`.
- `AestraComp::hasEditor()` returns true, while `openEditor()` returns false. That mismatch only affects the native `PluginEditorWindow` path; the in-app Aestra UI route uses `PluginUIController` and hosts `AestraCompEditor` directly.
- No generic plugin meter bus exists for compressor gain reduction. Existing track meters use `MeterSnapshotBuffer`; Compressor V1 reads plugin-local atomics instead.

Final editor behavior:

- Title: `AESTRA COMPRESSOR`.
- Bypass is shown as an explicit `ACTIVE` / `BYPASSED` button.
- Visible controls are exactly the V1 surface:
  - Threshold
  - Ratio
  - Attack
  - Release
  - Knee
  - Makeup
  - Mix
  - Input
  - Output
  - Detector HPF
  - Bypass
- Deprecated controls are not rendered: detector modes, topology, hold, auto release, range, lookahead, link law, SC listen, SC LPF, style, quality, saturation, and clipper controls remain hidden.

Meter integration:

- The editor safely casts the plugin instance to `AestraComp` and reads `getCurrentGainReductionDb()`, `getInputLevel()`, and `getOutputLevel()`.
- Meter smoothing is UI-side in `AestraCompEditor::onUpdate()`.
- The UI refreshes meters at roughly 30 Hz with `repaint()`.
- The audio path is unchanged: no locks, allocations, routing changes, or DSP writes were added.

Build and validation:

```bash
cmake -S . -B build/full-fast -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=OFF -DAESTRA_ENABLE_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/full-fast --target AestraUI_Core --parallel 2
cmake --build build/headless --target AestraCompPhase0Test AestraCompPhase1Test AestraCompUpgradeTest AestraCompressorMaterialLab AestraEQTest AestraDelayUpgradeTest --parallel 2
ctest --test-dir build/headless -R "AestraComp|AestraCompressorMaterialLab|AestraEQTest|AestraDelayUpgradeTest" --output-on-failure
```

Results:

- `AestraUI_Core`: built successfully.
- Compressor tests and material lab: passed.
- Nearby built-in effect tests (`AestraEQTest`, `AestraDelayUpgradeTest`): passed.

Known UI limitations:

- No transfer curve yet.
- No generic plugin meter bus yet.
- No preset browser inside the compressor editor.
- No mode selector by design.
- No external sidechain UX by design.
- No clipping/limiting indicator beyond input/output level activity.

Next recommended pass:

Freeze the V1 editor contract after a visual QA pass with screenshots. If desired, add a generic plugin meter bus later, but do not block Compressor V1 on it because local atomic metering is sufficient for the current editor.

## Visual QA And Editor Freeze Pass

Status: visual QA complete, editor frozen for V1. No DSP or public parameter changes were made.

### Visual QA Summary

Layout structure:

- Window: 680x508 px (increased from 470 for vertical breathing room).
- Title bar: 58 px with `AESTRA COMPRESSOR` (amber/teal, 17px) and subtitle.
- Bypass button: top-right, `ACTIVE` / `BYPASSED` toggle.
- Close button: top-right, 26x26 px (increased from 24x24 for clickability, aligned with Verb).
- Gain reduction meter: full-width, 80 px horizontal bar, amber fill, `-XdB` readout, tick labels at 0/-12/-24.
- Input/Output level meters: side-by-side below GR, 28 px, teal (IN) / amber (OUT).
- Separator line between meter section and control grid.
- Control grid: 5x2, primary row (Threshold, Ratio, Attack, Release, Knee) with 64px knobs and 108px cells, secondary row (Makeup, Mix, Input, Output, Detector HPF) with 48px knobs and 86px cells.

Visual hierarchy:

- Primary controls (Threshold, Ratio, Attack, Release) have larger knobs (64px), full-opacity teal accent, larger labels (8.5px, 0.78 alpha).
- Secondary controls (Knee, Makeup, Mix, Input, Output, Detector HPF) have smaller knobs (48px), dimmed teal accent (0.72x), smaller labels (8.0px, 0.64 alpha).
- Gain reduction meter is the dominant visual element — correct for a compressor.
- Input/output meters are secondary — useful but not competing with GR.
- Bypass is visible but not obnoxious.

Meter behavior:

- GR meter: reads `getCurrentGainReductionDb()`, clamped 0–48 dB, normalized to 0–24 dB display range, amber horizontal bar with tick marks.
- Input/Output meters: read `getInputLevel()` / `getOutputLevel()`, converted to dB-normalized display.
- All meters: UI-side smoothing at ~30 Hz, no audio-thread impact.
- Metering route: plugin-local atomics, no generic plugin meter bus.

Consistency with Verb:

- Both editors share: panel/surface/inset background pattern, `AESTRA <NAME>` title format, close button style, title-bar drag, teal accent (Verb uses purple/gold, Comp uses amber/teal — each has its own palette).
- Comp now has: primary/secondary knob hierarchy, separator line, larger close button — aligned with Verb's premium native direction.
- Verb has features Comp does not need: presets, mode selector, analysis panels — these are not appropriate for a compressor V1.

Stale labels or old concepts:

- None found. All labels match V1 surface. No deprecated control names visible.

### Polish Changes Made

- Window height: 470 → 508 px for vertical breathing room.
- Primary knob size: 56px → 64px (Threshold, Ratio, Attack, Release, Knee).
- Secondary knob size: new 48px (Makeup, Mix, Input, Output, Detector HPF).
- Primary/secondary visual hierarchy: accent opacity, label size, label alpha, arc thickness, pointer thickness.
- Title font: 16px → 17px for Verb alignment.
- Close button: 24x24 → 26x26 for Verb alignment.
- GR meter tick label contrast: 0.60 → 0.72 alpha.
- GR meter section label contrast: 0.78 → 0.82 alpha.
- Level meter label contrast: 0.76 → 0.82 alpha.
- Separator line between meters and controls.
- Knob radius calculation: now derived from knobRect size instead of fixed constant.

### Editor Freeze Contract

Locked for Compressor V1:

- V1 visible control set: Threshold, Ratio, Attack, Release, Knee, Makeup, Mix, Input, Output, Detector HPF, Bypass.
- Local atomics metering route: `getCurrentGainReductionDb()`, `getInputLevel()`, `getOutputLevel()`.
- Gain reduction meter as primary visual feedback.
- Input/output meter as secondary feedback.
- No mode selector.
- No deprecated controls.
- No fake advanced compressor features.
- Plugin ID remains `com.Aestrastudios.comp`.
- Display name remains `Aestra Compressor` unless Dylan changes branding.
- Editor window size 680x508.
- Primary/secondary knob hierarchy (64px/48px).

Allowed future polish:

- Transfer curve display.
- Preset browser.
- Better animation/meter ballistics.
- Generic plugin meter bus.
- Alternate compact layout.
- Factory presets.
- Mode/voicing pass after V1 freeze.

Not allowed without reopening V1:

- Changing DSP behavior.
- Changing public parameter identity.
- Reintroducing hidden/deprecated controls.
- Adding saturation/lookahead/modes as if they were already part of V1.
- Breaking old state compatibility.

### Tests And Builds Run

```bash
cmake -S . -B build/full-fast -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=OFF -DAESTRA_ENABLE_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/full-fast --target AestraUI_Core --parallel
cmake -S . -B build/headless -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/headless --target AestraCompPhase0Test AestraCompPhase1Test AestraCompUpgradeTest AestraCompressorMaterialLab --parallel
ctest --test-dir build/headless -R "AestraComp|AestraCompressorMaterialLab" --output-on-failure
ctest --test-dir build/headless -R "AestraEQTest|AestraDelayUpgradeTest" --output-on-failure
git diff --check
```

Results:

- `AestraUI_Core`: built successfully (pre-existing `-Woverloaded-virtual` warnings in other editors, not introduced by this pass).
- `AestraCompPhase0Test`: passed.
- `AestraCompPhase1Test`: passed.
- `AestraCompUpgradeTest`: passed.
- `AestraCompressorMaterialLab`: passed.
- `AestraEQTest`: passed.
- `AestraDelayUpgradeTest`: passed.
- `git diff --check`: clean.

Screenshot status:

- No automated screenshot path exists for NUI editors. Manual screenshots require running the full Aestra application with audio hardware.

### Remaining UI Limitations

- No transfer curve display.
- No generic plugin meter bus.
- No preset browser.
- No mode selector (by design).
- No external sidechain UX (by design).
- No clipping/limiting indicator beyond input/output level activity.
- No automated screenshot generation for CI.

### Next Recommended Pass

Transfer curve display or preset browser. Both are allowed future polish and do not require reopening the V1 freeze. A generic plugin meter bus would benefit all built-in plugins but is not blocking Compressor V1.
