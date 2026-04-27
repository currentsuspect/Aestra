# Arsenal Export/Live Parity — Phase 3 Harness Hardening

## Scope

Phase 3 hardens regression coverage for **current** Arsenal live/output behavior
and export parity. It intentionally does not change route semantics, bridge
semantics, renderer behavior, schema, or UI.

## What behavior is currently proven

Using deterministic tests (`ArsenalExportLiveParityTest` +
`ArsenalExportCurrentPolicyTest`):

1. `routeId < 0` (PreviewToMaster) is audible in live output.
2. `routeId < 0` currently participates in offline export.
3. `routeId >= 0` (RoutedToTimelineTrack) remains audible when track path is open.
4. Track-routed Arsenal audio is processed through track path controls (mute/gain),
   consistent with being injected before/within Timeline track FX/gain stage.
5. Master-preview Arsenal audio bypasses track path controls (track mute does not
    silence preview path).
6. Current isolated-track policy guard remains documented:
    master-preview pass is skipped when `isolatedTrackIndex >= 0`.
7. Mixed-route permutations (PreviewToMaster + RoutedToTimelineTrack units
    running simultaneously in the same engine session) are correctly
    segregated: both routing paths produce audio without cross-contamination.
    Proven via `ArsenalExportLiveParityTest` Case 4 (live + export).

All of the above are current-policy assertions, not final product-policy claims.

## What behavior remains unproven

- A dedicated offline-render assertion that directly exercises isolated-track
  bounce with Arsenal MIDI scheduling through `AudioEngine::bounceRangeToWav`
  (the `ctx.isolatedTrackIndex` path, distinct from the general `AudioExporter`
  path which never isolates tracks).
- FX-specific ordering proof against non-gain effects in a minimal deterministic
  fixture (current tests prove path difference via track gain/mute controls).
- Product-policy target where PreviewToMaster may be excluded from final export.

## Required proof before changing PreviewToMaster export policy

Before changing PreviewToMaster export inclusion/exclusion:

1. Add explicit offline-export regression cases that verify expected inclusion
   and exclusion outcomes under the chosen policy.
2. Include isolated-track export coverage in that same harness.
3. Keep parity checks against live process path to prevent hidden divergence.

## Why no metadata/UI work was added

Phase 3 is focused exclusively on behavior regression safety around current
audio/export paths. Metadata and UI surfaces were intentionally left untouched
to avoid mixing policy hardening with feature-surface expansion.
