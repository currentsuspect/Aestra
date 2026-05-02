# Arsenal Processing Context Discovery (Architecture-Only)

## Scope

This document records a historical discovery pass. Its snapshot below was captured for context and does not describe
the current branch or any later pull request contents.

## Historical Repository Snapshot

- **Branch:** `develop`
- **Commit:** `7c446483a0c5dc07200cb8b5d3f42044dd71873b`
- **Working tree at start:** clean
- **Working tree then:** one documentation file added (`AestraDocs/architecture/arsenal-processing-context-discovery.md`)

---

## 1. Current Architecture

### 1.1 Arsenal audio flow today

1. **Pattern MIDI generation**
   - `PatternPlaybackEngine` owns scheduling (`refillWindow`) and RT dequeue/routing (`processAudio`).
   - Events are queued as `ScheduledEvent` (unitId, frame offset, note on/off, etc.) and fanned out into per-unit `MidiBuffer`s using `UnitMidiRoute`.
   - Source of notes is `PatternManager` MIDI payloads whose notes carry `unitId`.

2. **Unit MIDI buffer assignment**
   - `UnitManager::getAudioSnapshot()` publishes unit list (`id`, `enabled`, `plugin`, `routeId` from `targetMixerRoute`).
   - Audio thread builds fixed route arrays by pairing snapshot units with preallocated MIDI buffers (`m_unitMidiBuffers` or `m_scratchMidiBuffers`) in snapshot order.

3. **Unit plugin processing**
   - In the active `AudioEngine::renderGraph` path, each enabled unit plugin is processed with:
     - no audio input (`nullptr` or silent input),
     - routed per-unit MIDI buffer,
     - stereo output mixed into either a track buffer or master buffer.
   - `AudioEngine::processArsenalUnits(...)` also exists and is called from `processBlock`, but is currently guarded by `m_patternPlaybackMode`; it runs only in pattern mode.
   - `AudioRenderer::{processArsenalMidi,renderArsenalUnitsForTrack,processArsenalUnits}` exists as parallel/legacy renderer logic; `AudioEngine::processBlock` currently uses `renderGraph`, not those Arsenal passes.

4. **`routeId` behavior**
   - `UnitInfo::targetMixerRoute` is serialized and snapshot-copied to `UnitState::routeId`.
   - In render path:
     - `routeId >= 0`: mixed into that timeline track buffer before track effects/fader/send path completes.
     - `routeId < 0`: mixed directly to master bus path (preview/master style).

5. **Timeline effects order relative to Arsenal output**
   - For `routeId >= 0`, Arsenal unit audio enters track buffer before track insert FX/fader/send mix stage, so timeline FX apply to routed unit output.
   - For `routeId < 0`, unit audio bypasses per-track insert chains and lands in master accumulation.

6. **Offline export participation**
   - `AudioExporter::render()` drives `AudioEngine::processBlock()` with transport forced on and metronome/audition disabled.
   - Therefore Arsenal participation in export follows the same live `processBlock`/`renderGraph` rules:
     - routed-to-track units are included;
     - route-to-master units can also be included if they produce signal.
   - Offline parity test validates exporter parity generally, but not Arsenal-specific parity.

7. **`.aes` round-trip of Arsenal state**
   - `ProjectSerializer::serialize` writes `root["arsenal"] = UnitManager::saveToJSON()`.
   - `ProjectSerializer::load` loads Arsenal before patterns (explicit ordering comment and implementation).
   - `UnitManager` persists ids, route assignment, unit metadata, pluginId, plugin state hex, and restores plugin lifecycle as initialize -> loadState -> activate (if enabled).
   - Existing tests cover Arsenal presence and internal-plugin round-trip of route/plugin state.

---

## 2. Current Safety (Do-Not-Break Constraints)

1. **Realtime safety**
   - Audio-thread paths rely on preallocated buffers, lock-free queues, and immutable snapshots.
   - Pattern scheduling split is explicit: non-RT refill, RT dequeue/process.

2. **Plugin lifecycle/order**
   - Established order in restore flows is `create -> initialize -> loadState -> activate`.
   - Unit enable/disable toggles plugin activation state.

3. **Out-of-process plugin assumptions**
   - Third-party plugins are routed through `HybridPluginFactory` to out-of-process proxy by format (non-internal).
   - Crash behavior falls back to pass-through/bypass semantics; graph/context changes must preserve this contract.

4. **Project load ordering**
   - Arsenal units are loaded before patterns because pattern notes reference `unitId`.
   - Load validation tolerates missing references as warnings, preserving semantics.

5. **Routing/export parity**
   - Export path is intentionally aligned with live engine path (`processBlock`), not a separate renderer authority.
   - Any Arsenal-context split must keep explicit parity rules.

6. **Round-trip semantic stability**
   - `.aes` schema currently stable for arsenal units (`targetMixerRoute`/`timelineLaneAssignment`, plugin state hex, ids, type/group/defaultPatternId).
   - Existing regression tests assume non-destructive handling of missing refs and stable load semantics.

7. **CI/runtime gating**
   - Runtime/integration tests (including Arsenal round-trip/attachment tests) are built but not default-registered unless `AESTRA_ENABLE_RUNTIME_TESTS=ON`.
   - Deterministic/default CI signal remains the always-registered tier.

---

## 3. Gap Analysis vs Target Model

Target model:

**Shared DSP core**  
→ **Timeline Processing Context**  
→ **Arsenal Processing Context**  
→ **Explicit bridge only** (`linked rack`, `copied chain`, `rendered/frozen audio`, `timeline route`)

### Missing pieces

1. **No dedicated `ArsenalProcessingContext` owner**
   - Arsenal state is currently embedded via `UnitManager` snapshot and consumed directly in main render path.

2. **No rack/chain abstraction separate from `UnitInfo`**
   - `UnitInfo` conflates UI/unit metadata, plugin instance ownership, route assignment, and serialization payload.

3. **No explicit bridge model**
   - Routing is implicit via integer route (`routeId`) and “<0 means master”.
   - No explicit domain model for “linked rack”, “local copy”, “rendered/frozen”.

4. **No schema for context relationship modes**
   - `.aes` stores unit routing and plugin blobs but not explicit link/copy/render provenance between Arsenal and Timeline contexts.

5. **Export/render authority rules not explicit for context split**
   - Current behavior is implementation-driven by mixed render path, not by explicit policy object/ruleset.

6. **UI-state clarity gap**
   - “preview to master” vs “timeline authoritative render” is not represented with an explicit mode enum and policy.

7. **Tests missing before refactor**
   - No dedicated tests proving Arsenal route mode semantics across realtime and export for each intended mode.
   - No explicit tests for “no hidden Arsenal audio in export unless bridged”.

---

## 4. Minimal Implementation Plan (Small PR Phases)

### Phase 0 — Naming/model cleanup + architecture guardrails (no sound change)

- Add explicit terminology in code comments/types:
  - “Timeline context” vs “Arsenal context” vs “Bridge mode”.
- Introduce non-functional guardrail type(s), e.g. `ArsenalRouteMode` enum (internal-only mapping to existing route behavior).
- Add assertions/logging hooks around ambiguous `routeId` use (debug-only where safe).
- Add documentation of current authority rules (timeline/export owner).

### Phase 1 — Introduce `ArsenalProcessingContext` thin wrapper (no sound change)

- Add a thin owner object that wraps existing `UnitManager` snapshot + MIDI route buffer preparation.
- Keep same plugin instances, same snapshot payload, same routing outcomes.
- Delegate existing Arsenal preparation logic through wrapper without changing actual render math/order.

### Phase 2 — Explicit Arsenal route modes

- Implement explicit modes while preserving current defaults:
  - `Draft`
  - `PreviewToMaster`
  - `RoutedToTimelineTrack`
  - `LinkedRack`
  - `LocalCopy`
  - `RenderedAudio`
- Initially map modes to legacy behavior to avoid output changes.
- Keep bridging explicit and serializable (mode field + required ids).

### Phase 3 — Regression test expansion before behavior expansion

- Add tests that pin:
  - realtime parity for legacy projects,
  - export parity (especially no unintended Arsenal leakage),
  - route-mode round-trip in `.aes`,
  - bridge semantics for routed-to-track vs preview-to-master.
- Keep runtime-gated tests behind existing runtime flags where required.

### Phase 4 — Expand to real rack chains/macros

- Only after phases 0–3 are green:
  - introduce rack-level chain model decoupled from `UnitInfo`,
  - add linked/local copy behavior with explicit sync policy,
  - add freeze/rendered-asset lifecycle.

---

## 5. Risk Notes

1. **Duplicate processing risk**
   - Arsenal code exists in both `AudioEngine` and `AudioRenderer`; ownership confusion can lead to double-render or drift.

2. **Preview leakage into export**
   - Current implicit route semantics (`routeId < 0`) can unintentionally enter master export path if not policy-gated.

3. **Plugin state divergence**
   - Without explicit link/copy model, timeline and arsenal behaviors can diverge silently after edits.

4. **`routeId` ambiguity**
   - Integer semantics are overloaded (track routing + preview fallback), making intent unclear.

5. **Master-preview UX confusion**
   - Users may not know whether Arsenal output is audition-only or render-authoritative.

6. **Offline mismatch risk**
   - If context split changes render path branching, export/live parity can regress.

7. **Save/load drift**
   - New mode fields must coexist with current schema and preserve old-project behavior.

8. **CPU overhead**
   - Always-live Arsenal units can increase cost if context activation policy is not explicit.

---

## 6. Tests/Validation Run

- Configured low-memory headless build:
  - `cmake -S . -B build-lowmem-discovery -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_TESTS=ON -DAESTRA_LOW_MEMORY_BUILD=ON -DCMAKE_BUILD_TYPE=Release`
- Built with constrained parallelism:
  - `cmake --build build-lowmem-discovery --parallel 2`
- Ran deterministic/default tests:
  - `ctest --test-dir build-lowmem-discovery --output-on-failure -j2`
  - **Result:** `100% tests passed, 0 failed (50 tests)`

---

## 7. Files Inspected (Discovery Set)

- `AestraAudio/include/Models/UnitManager.h`
- `AestraAudio/src/Models/UnitManager.cpp`
- `AestraAudio/include/Core/AudioRenderer.h`
- `AestraAudio/src/AudioRenderer.cpp`
- `AestraAudio/include/Core/AudioEngine.h`
- `AestraAudio/src/Core/AudioEngine.cpp`
- `AestraAudio/include/Core/EngineState.h`
- `AestraAudio/include/Core/AudioGraph.h`
- `AestraAudio/include/Core/AudioGraphState.h`
- `AestraAudio/include/Playback/PatternPlaybackEngine.h`
- `AestraAudio/src/Playback/PatternPlaybackEngine.cpp`
- `AestraAudio/include/Plugin/PluginHost.h`
- `AestraAudio/include/Plugin/EffectChain.h`
- `AestraAudio/src/Plugin/EffectChain.cpp`
- `AestraAudio/include/Plugin/PluginManager.h`
- `AestraAudio/src/Plugin/PluginManager.cpp`
- `AestraAudio/include/Plugin/PluginFactory.h`
- `AestraAudio/src/Plugin/PluginFactory.cpp`
- `AestraAudio/include/Plugin/OutOfProcessPluginInstance.h`
- `AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp`
- `AestraAudio/include/IO/AudioExporter.h`
- `AestraAudio/src/IO/AudioExporter.cpp`
- `AestraAudio/include/IO/OfflineRenderHarness.h`
- `Source/Core/ProjectSerializer.cpp`
- `AestraAudio/include/Models/TrackManager.h`
- `Source/Core/AestraContent.cpp`
- `Tests/Integration/ArsenalInstrumentAttachmentTest.cpp`
- `Tests/AestraAudio/ArsenalParameterStressTest.cpp`
- `Tests/Headless/RumbleArsenalAudibleTest.cpp`
- `Tests/Integration/ProjectRoundTripTest.cpp`
- `Tests/Integration/ProjectRoundTripIntegrityTest.cpp`
- `Tests/Integration/InternalPluginProjectRoundTripTest.cpp`
- `Tests/Integration/ProjectLoadRegressionTest.cpp`
- `Tests/Headless/OfflineRenderRegressionTest.cpp`
- `Tests/CMakeLists.txt`

---

## Recommendation: Proceed or Wait

**Proceed with Phase 0 only** (guardrails + naming/model clarity), then Phase 1 thin-wrapper extraction.  
Do **not** jump directly to behavior changes until explicit bridge semantics and regression tests (Phase 3) are in place.
