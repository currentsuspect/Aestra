# Arsenal Route Mode — Phase 2B (Behavior Contract + Policy Guardrails)

## Scope

Phase 2B defines behavior contracts and policy tests only. It does **not** change
runtime routing, export semantics, serializer authority, plugin lifecycle, or UI.

## Current route-mode behavior (unchanged)

### `ArsenalRouteMode::Draft`

- Exists as explicit scaffolding in the in-memory model.
- Current behavior remains compatibility-driven through `routeId`.
- It is not activated as a separate renderer/export policy in this phase.

### `ArsenalRouteMode::PreviewToMaster`

- Compatibility mapping: `routeId < 0`.
- Current behavior is audible and export-participating because offline export
  follows live `processBlock` authority.
- This caveat is intentional in current builds and unchanged here.

### `ArsenalRouteMode::RoutedToTimelineTrack`

- Compatibility mapping: `routeId >= 0`.
- Arsenal unit audio enters Timeline track path before Timeline track FX.
- Participates in live playback and offline export.
- This is currently the only arrangement-authoritative route mode.

## Future desired behavior (not enabled in Phase 2B)

- `Draft`: workshop-only state, non-authoritative for final arrangement/export.
- `PreviewToMaster`: live audition behavior may remain audible, but final export
  policy should be explicit (likely excluded unless enabled).
- `RoutedToTimelineTrack`: remains authoritative Timeline-owned signal path.

## Route mode vs bridge mode

- **Route mode** answers: where does unit audio render *right now* in engine routing?
- **Bridge mode** answers: how Arsenal content is related to Timeline ownership and persistence.

These concerns are distinct and must not be conflated during migration.

## Bridge-mode contract vocabulary (policy-only)

Phase 2B standardizes terminology without activating runtime behavior:

1. `DraftOnly`
2. `PreviewToMaster`
3. `LinkedRack`
4. `LocalCopy`
5. `RenderedAudio`
6. `FrozenAudio`

No production bridge enum is introduced in this phase to avoid premature runtime coupling.

## Final target model (architectural intent)

- **Arsenal** = sound design / audition workshop context
- **Timeline** = arrangement + export authority
- **Shared DSP core** = single processing implementation reused by both contexts

## Explicitly not changed in Phase 2B

- No audible routing change.
- No export behavior change.
- No `.aes` compatibility break.
- No removal of `routeId`.
- No activation of `Draft` behavior.
- No linked/local/rendered/frozen bridge implementation.

## Export policy TODO guard

Before changing `PreviewToMaster` export behavior, add explicit offline-render
regression coverage proving expected inclusion/exclusion behavior under both live
and offline authorities. Until that test exists, keep current export parity.
