# Arsenal Bridge Metadata — Phase 2D (Additive Persistence)

## Scope

Phase 2D persists `ArsenalBridgeMode` as additive unit metadata. It does **not**
change rendering, route authority, export behavior, UI behavior, or plugin lifecycle.

## What was persisted

`UnitInfo` now carries:

- `ArsenalBridgeMode bridgeMode`
- `ArsenalBridgeMode getBridgeMode() const`

`UnitManager::saveToJSON()` persists:

- `"bridgeMode": "<StableName>"`

where `<StableName>` is one of:

- `DraftOnly`
- `PreviewToMaster`
- `LinkedRack`
- `LocalCopy`
- `RenderedAudio`
- `FrozenAudio`

## Why this is additive

- Existing projects without `bridgeMode` continue to load.
- Unknown/invalid `bridgeMode` values do not fail load.
- Routing/export authority remains unchanged.

No existing fields were removed or made incompatible.

## Fallback behavior

When loading a unit:

1. If `bridgeMode` is present and valid string: use that value as metadata.
2. If `bridgeMode` is missing: fallback by current route compatibility.
3. If `bridgeMode` is invalid/non-string: log warning and fallback by current route compatibility.

Route-compatible fallback used in Phase 2D:

- `routeId < 0` -> `PreviewToMaster`
- `routeId >= 0` -> `LinkedRack`

This fallback is metadata-only and does not alter route behavior.

## Invalid value behavior

Invalid or unknown bridge tokens are non-fatal and warned. Load proceeds with safe fallback.

## Route mode vs bridge mode (still separate)

- `ArsenalRouteMode` + `routeId`: current render-destination interpretation.
- `ArsenalBridgeMode`: ownership/intention metadata for future Arsenal→Timeline bridges.

`routeId` remains routing authority. `bridgeMode` is not authoritative for audio decisions.

## Why bridgeMode still does not affect audio

No `AudioRenderer`, `ArsenalProcessingContext` routing predicates, offline export path,
or process-block behavior was changed in this phase.

## Future behavior targets

`bridgeMode` now provides persisted vocabulary for future implementation phases:

- `LinkedRack`
- `LocalCopy`
- `RenderedAudio`
- `FrozenAudio`

These modes are not active processing features yet; they remain planned ownership semantics.
