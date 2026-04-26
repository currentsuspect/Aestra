# Arsenal Bridge Metadata — Phase 2C (Non-Behavioral Surface)

## Scope

Phase 2C introduces a lightweight Arsenal→Timeline bridge metadata surface and
export/live parity guardrails. It does **not** change audio rendering, export
inclusion, UI behavior, plugin lifecycle, or `.aes` compatibility behavior.

## Route mode vs bridge mode

- **Route mode** (`ArsenalRouteMode`) answers: where does unit audio currently render?
  - `routeId < 0` → preview/master path
  - `routeId >= 0` → timeline-track path
- **Bridge mode** (`ArsenalBridgeMode`) answers: who owns this sound relationship
  between Arsenal and Timeline (draft, linked, copied, rendered, frozen).

These are intentionally separate concepts.

## Bridge metadata surface added

`AestraAudio/include/Models/ArsenalBridgeMode.h` defines metadata-only policy
vocabulary:

1. `DraftOnly`
2. `PreviewToMaster`
3. `LinkedRack`
4. `LocalCopy`
5. `RenderedAudio`
6. `FrozenAudio`

It also provides stable conversion helpers:

- `toString(ArsenalBridgeMode)`
- `arsenalBridgeModeFromString(...)`

This surface is intentionally neutral and does not participate in current
routing decisions.

## Current behavior (unchanged)

- Rendering/export authority remains routeId-compatible routing.
- `Draft` route mode remains inactive scaffolding in runtime behavior.
- `PreviewToMaster` remains audible and export-participating under current
  processBlock authority.
- `RoutedToTimelineTrack` remains timeline-authoritative path.

## Serialization decision

Bridge metadata serialization is **deferred** in Phase 2C.

Reasoning:

- Current `.aes` schema is already carrying route compatibility scaffolding.
- Persisting bridge ownership now would introduce schema/state decisions before
  ownership behavior is implemented.
- Safer sequence: finalize ownership semantics + migration policy first, then
  add additive optional serialization with explicit fallback tests.

Planned migration shape (future):

1. Add optional bridge metadata field under Arsenal unit metadata.
2. Default missing field to current-compatible policy (`DraftOnly`).
3. Treat invalid/unknown values as non-fatal and fallback safely.
4. Add repeated save/load anti-drift assertions before enabling behavior.

## Export/live parity guard

Phase 2C adds `ArsenalExportCurrentPolicyTest` as a current-policy guard:

- documents that preview and track-routed classifications are currently parity-
  aligned between live/offline authority paths at the route decision layer.
- does **not** flip policy to exclude preview from export.

Before changing `PreviewToMaster` export policy, extend deterministic offline
render coverage (existing host: `Tests/Headless/OfflineRenderRegressionTest.cpp`)
with explicit preview inclusion/exclusion assertions tied to final product policy.

## Why this phase intentionally does not change sound

Phase 2C is a metadata and safety phase. It provides vocabulary and guardrails
to reduce migration risk while preserving existing behavior until dedicated
behavior-switch phases are specified and fully regression-covered.
