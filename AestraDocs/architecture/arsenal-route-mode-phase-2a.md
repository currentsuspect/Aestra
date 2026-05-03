# Arsenal Route Mode — Phase 2A (Compatibility Fielding)

## Scope

Phase 2A introduces explicit route-mode fielding and compatibility tests only.
It does **not** introduce new product behavior.

## Current route modes

- `PreviewToMaster`
- `RoutedToTimelineTrack`
- `Draft` (future scaffolding only)

## routeId compatibility rules (current authority)

- `routeId < 0` resolves to `PreviewToMaster`
- `routeId >= 0` resolves to `RoutedToTimelineTrack`
- Current rendering/export behavior is still routeId-driven for parity.
- If serialized `routeMode` disagrees with `routeId`, loader preserves routeId-compatible behavior and logs a non-fatal warning.

## Why `Draft` is scaffolding only in Phase 2A

`Draft` is intentionally modeled but inactive. In this phase it does not mute,
exclude from export, or alter rendering paths. That avoids behavior churn while
preparing explicit mode semantics for a later phase.

## Current export semantics (unchanged)

Offline export follows the live engine render authority. Therefore:

- Track-routed Arsenal units still render via Timeline track path and hit track FX.
- Master-preview Arsenal units still render via preview/master path.

No export-policy split between Preview and Draft is active in this phase.

## Why this is not yet a full route-mode feature

Phase 2A only fields explicit mode data and centralizes route resolution through
`ArsenalProcessingContext`. It does not add linked racks, local copies, frozen/
rendered assets, separate Arsenal graph ownership, or UI mode controls.

## Recommended Phase 2B

1. Define explicit product behavior for each mode (`Draft`, `PreviewToMaster`, `RoutedToTimelineTrack`) and the user-facing transitions.
2. Add a clear Arsenal→Timeline bridge model (linked rack, local copy, rendered/frozen asset) before behavior switches.
3. Add targeted offline parity tests for preview-vs-track path once a stable deterministic harness is available for both paths.

Phase 2B policy contract and terminology are documented in:
`AestraDocs/architecture/arsenal-route-mode-phase-2b.md`.
