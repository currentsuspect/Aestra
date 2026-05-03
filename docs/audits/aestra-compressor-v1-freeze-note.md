# Aestra Compressor V1 — Editor Freeze Note

Date: 2026-05-01
Branch: develop
Commit: d98d3847 (pre-polish), visual QA polish on top

## Status

**FROZEN**

The Aestra Compressor V1 editor is frozen. The visual QA pass confirmed the editor reads as Aestra Compressor, has correct primary/secondary control hierarchy, and is consistent with Aestra Verb's premium native direction. No DSP or public parameter changes were made.

## Locked Behavior

- V1 visible control set: Threshold, Ratio, Attack, Release, Knee, Makeup, Mix, Input, Output, Detector HPF, Bypass.
- Local atomics metering route: `getCurrentGainReductionDb()`, `getInputLevel()`, `getOutputLevel()`.
- Gain reduction meter as primary visual feedback (amber horizontal bar, -dB readout).
- Input/output meter as secondary feedback (teal IN, amber OUT).
- No mode selector.
- No deprecated controls (detector modes, topology, hold, auto release, range, lookahead, link law, SC listen, SC LPF, style, quality, saturation, clipper).
- No fake advanced compressor features.
- Plugin ID: `com.Aestrastudios.comp`.
- Display name: `Aestra Compressor`.
- Editor window: 680x508 px.
- Primary knobs (Threshold, Ratio, Attack, Release): 64px, full-opacity teal accent.
- Secondary knobs (Knee, Makeup, Mix, Input, Output, Detector HPF): 48px, dimmed teal accent.

## Allowed Future Polish

- Transfer curve display.
- Preset browser.
- Better animation/meter ballistics.
- Generic plugin meter bus.
- Alternate compact layout.
- Factory presets.
- Mode/voicing pass after V1 freeze.

## Not Allowed Without Reopening V1

- Changing DSP behavior.
- Changing public parameter identity.
- Reintroducing hidden/deprecated controls.
- Adding saturation/lookahead/modes as if they were already part of V1.
- Breaking old state compatibility.

## Test Evidence

```
AestraCompPhase0Test .............   Passed    0.26 sec
AestraCompPhase1Test .............   Passed    0.03 sec
AestraCompUpgradeTest ............   Passed    0.03 sec
AestraCompressorMaterialLab ......   Passed    0.14 sec
AestraEQTest .....................   Passed    0.03 sec
AestraDelayUpgradeTest ...........   Passed    0.04 sec
```

`AestraUI_Core` built successfully. `git diff --check` clean.

## Known Limitations

- No transfer curve display.
- No generic plugin meter bus.
- No preset browser.
- No mode selector (by design).
- No external sidechain UX (by design).
- No clipping/limiting indicator beyond input/output level activity.
- No automated screenshot generation for CI.
- Pre-existing `-Woverloaded-virtual` warnings in other editors (AestraEQEditor, AestraDelayEditor, GenericPluginEditor) — not introduced by this pass.
