# Arsenal Architecture — Consolidated Summary

> This doc replaces the prior phase-by-phase notes (Discovery, Phase 0/1, 2A, 2B, 2C, 2D, 3) which are preserved in git history. It is the **current** authoritative description of Arsenal route/bridge semantics, processing context, and export/live parity.

---

## 1. What Arsenal is

Arsenal is the in-engine "rack of units" — a flat collection of plugin-backed units, each scheduled by `PatternPlaybackEngine` and rendered through `AudioRenderer`. Each unit is one of:

- A **preview-to-master** unit (audible alongside Timeline, useful for jamming/sound design).
- A **routed-to-timeline-track** unit (its audio injects into a Timeline track at the pre-FX point).
- A **draft** unit (scaffolded — not yet activated as a separate rendering policy).

Snapshots of Arsenal state are published as immutable `AudioArsenalSnapshot` objects (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Models/UnitManager.h:170-186`) and consumed by the audio thread without locks via `std::atomic_load` on a `shared_ptr` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Models/UnitManager.cpp:239-242`).

---

## 2. Route mode vs Bridge mode

Two orthogonal concepts. Keep them separate; they answer different questions.

### `ArsenalRouteMode` — "where does this unit's audio go right now?"

| Mode | Compatibility rule | Effect |
| --- | --- | --- |
| `PreviewToMaster` | `routeId < 0` | Audible on master; also currently participates in offline export (export follows live `processBlock`). |
| `RoutedToTimelineTrack` | `routeId >= 0` | Audible only when the target track path is open; processed through that track's FX + gain. Track mute silences it. |
| `Draft` | scaffolding | Not yet activated as a separate renderer/export policy. Currently behaves as compatibility-mapped from `routeId`. |

`routeId` remains the **render authority** today; `ArsenalRouteMode` is the explicit name. If a serialized `routeMode` disagrees with `routeId`, the loader preserves `routeId`-compatible behavior and logs a non-fatal warning.

### `ArsenalBridgeMode` — "who owns this Arsenal⇄Timeline relationship?"

Defined in `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Models/ArsenalBridgeMode.h`. Metadata-only today; **does not** alter rendering, export, or plugin lifecycle.

Vocabulary (stable names persisted in `.aes`):

- `DraftOnly`
- `PreviewToMaster`
- `LinkedRack`
- `LocalCopy`
- `RenderedAudio`

Persisted via `UnitManager::saveToJSON()` as `"bridgeMode": "<StableName>"`. Unknown values are tolerated on load.

---

## 3. Processing context

`ArsenalProcessingContext` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/ArsenalProcessingContext.h`) is the thin wrapper used by `AudioRenderer` to:

- Resolve the current published Arsenal snapshot.
- Apply route-mode policy helpers (`shouldRenderToTimelineTrack`, `shouldRenderToMasterPreview`).
- Bridge to `PatternPlaybackEngine` for MIDI scheduling.

It owns no allocations and adds no DSP graph — it is purely a snapshot+policy resolver. See `ARCHITECTURE_AUDIT_2026Q2.md` §2.3 for an open concern about per-block construction cost.

---

## 4. Audio flow

1. **MIDI scheduling** — `PatternPlaybackEngine::refillWindow` (non-RT) fills a lookahead ring of `ScheduledEvent`s tagged with `unitId`.
2. **RT dequeue** — `PatternPlaybackEngine::processAudio` fans events into per-unit `MidiBuffer`s via `UnitMidiRoute` (preallocated in `AudioEngine::m_unitMidiBuffers`).
3. **Per-track rendering** — for each Timeline track, `AudioRenderer::renderArsenalUnitsForTrack` invokes `plugin->process(...)` for every Arsenal unit whose `routeMode == RoutedToTimelineTrack` and whose target is this track. Output is summed into the track's `selfBuffer` *before* track FX and gain.
4. **Master-preview rendering** — `AudioRenderer::processArsenalUnits` runs `PreviewToMaster` units once per block, summing directly into the master buffer. Skipped when `ctx.isolatedTrackIndex >= 0`.

Both paths share preallocated `m_pluginBufferF` and `m_silentBufferF` — no per-iteration heap allocations except the discarded `MidiBuffer mOut` (flagged in audit §2.2).

---

## 5. Export / live parity contract

Export currently follows `processBlock` authority — the same code paths render both live audio and offline export. The deterministic regression set lives in:

- `ArsenalExportLiveParityTest`
- `ArsenalExportCurrentPolicyTest`
- `ArsenalRouteModeCompatibilityTest`
- `ArsenalProcessingContextRoutingTest`
- `ArsenalRouteModeRoundTripTest`

Currently proven invariants:

1. `PreviewToMaster` is audible **and** export-participating.
2. `RoutedToTimelineTrack` audio passes through track mute/gain.
3. Master-preview audio bypasses track mute (it never went through the track).
4. `isolatedTrackIndex >= 0` skips the master-preview pass.
5. Mixed-route sessions (Preview + Track-routed units in the same engine) segregate cleanly with no cross-contamination.

---

## 6. Currently unchanged (still TODO)

- `Draft` route mode is modeled but inactive; no mute/export semantics.
- Bridge mode is metadata only; nothing in the engine reads it for routing decisions.
- Per-block `ArsenalProcessingContext` construction and `shared_ptr` refcount churn (audit §2.3).
- `MidiBuffer mOut` lifetime in render loop (audit §2.2).

---

## 7. Predecessor docs (in git history)

Replaced by this file:

- `arsenal-processing-context-discovery.md`
- `arsenal-processing-context-phase-0-1.md`
- `arsenal-route-mode-phase-2a.md`
- `arsenal-route-mode-phase-2b.md`
- `arsenal-bridge-mode-phase-2c.md`
- `arsenal-bridge-mode-phase-2d.md`
- `arsenal-export-live-parity-phase-3.md`

Use `git log -- AestraDocs/architecture/arsenal-*.md` to retrieve the phase-by-phase rationale if needed.
