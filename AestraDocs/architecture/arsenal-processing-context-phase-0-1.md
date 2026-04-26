# Arsenal Processing Context — Phase 0/1 (Non-Behavioral Extraction)

## What changed

1. Added explicit route-semantic scaffolding in `UnitManager`:
   - `ArsenalRouteMode` enum (`PreviewToMaster`, `RoutedToTimelineTrack`, `Draft`)
   - Lightweight route helpers mapped from existing `routeId`/`targetMixerRoute`
2. Added a thin `ArsenalProcessingContext` wrapper:
   - centralizes current `UnitManager` snapshot access
   - centralizes current route-mode interpretation helpers
   - does **not** introduce a new DSP graph or engine
3. Refactored `AudioRenderer` Arsenal route checks to use explicit helper names while preserving existing logic.
4. Added two small deterministic tests:
   - `ArsenalRouteModeCompatibilityTest` (renamed/expanded in Phase 2A)
   - `ArsenalProcessingContextRoutingTest` (renamed/expanded in Phase 2A)
   - `ArsenalRouteModeRoundTripTest` (added in Phase 2A)

## What did not change

- No audio routing behavior change
- No render order change
- No export authority change
- No plugin lifecycle order change
- No `.aes` schema/output change
- No UI behavior change
- No removal of `routeId`

## Current route semantics (unchanged)

- `routeId < 0` → `PreviewToMaster`
- `routeId >= 0` → `RoutedToTimelineTrack`

These helpers are naming/guardrail scaffolding over existing behavior only.

## Phase 2A follow-up status

Phase 2A adds an explicit in-memory `routeMode` field while preserving `routeId`
as current render/export authority. `Draft` remains inactive scaffolding and does
not currently mute, bypass, or export-exclude units.

## Why route modes are scaffolding only

The explicit enum documents intent and removes ambiguity around magic integer checks, but it is currently just a typed interpretation of legacy `routeId`. It does not activate `Draft` or any new routing mode behavior in this phase.

## Why `ArsenalProcessingContext` is a wrapper, not a new engine

Phase 0/1 is intentionally non-behavioral. The wrapper only names and centralizes existing access paths to prepare future context ownership separation without duplicating DSP, plugin processing, or graph execution.

## Next safe phase

Proceed to route-mode model expansion only after parity/regression coverage is sufficient (especially routing/export parity assertions), then add bridge semantics incrementally without changing schema or output unexpectedly.
