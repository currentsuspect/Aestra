# Plugin & Effect-Chain Lifetime Audit

**Date:** 2026-05-02
**Branch:** `develop` @ `4dc25b33`
**Scope:** Discovery-only — no behavioral changes, no GC adoption, no ownership refactors.

---

## 1. Executive Summary

**Current risk level: High**

Plugin and effect-chain ownership in Aestra uses a mix of value semantics, `unique_ptr`, `shared_ptr`, and raw pointers. The audio thread accesses plugin instances through raw `EffectChain*` pointers stored in the immutable `AudioGraph` snapshot, and through `shared_ptr<IPluginInstance>` copies in Arsenal snapshots. The primary danger is that **mutation paths (insert/remove/clear/load) are not synchronized with the audio thread's iteration of plugin slots**. There is no mutex, lock-free barrier, or snapshot mechanism protecting `EffectChain::m_slots` during concurrent `process()` calls.

**Immediate GC adoption is NOT recommended.** The underlying ownership model needs architectural hardening first — specifically, snapshot-based publication of plugin slot state to the audio thread — before deferred destruction via `GarbageCollector` would be safe. Adopting GC now would mask the real problem (concurrent mutation without synchronization) rather than solve it.

**Next steps:** The recommended path is (A) add test instrumentation to detect concurrent mutation, (B) introduce snapshot architecture for effect-chain slot state, then (C) adopt GC for retired plugin instances.

---

## 2. Ownership Map

### 2.1 EffectChain (Track Insert Effects)

| Aspect | Detail |
|---|---|
| **Owner object** | `MixerChannel` |
| **Owned resource** | `EffectChain m_effectChain` (by value) |
| **Ownership mechanism** | Value member — `EffectChain` is embedded in `MixerChannel` |
| **Storage** | `std::array<EffectSlot, 10> m_slots` (fixed-size, no heap vector) |
| **Plugin storage** | Each `EffectSlot` holds `PluginInstancePtr plugin` = `std::shared_ptr<IPluginInstance>` |
| **Who can mutate** | Main/UI thread: `insertPlugin()`, `removePlugin()`, `movePlugin()`, `swapPlugins()`, `clear()` |
| **Who can read/process** | Audio thread: `process()` iterates `m_slots`, reads `slot.plugin`, `slot.bypassed`, `slot.dryWetMix` |
| **Audio thread visible?** | **Yes** — via raw `EffectChain*` in `AudioGraph::TrackRenderState::effectChain` |
| **Export/headless visible?** | **Yes** — same `processBlock()` path |

**Key files:**
- `AestraAudio/include/Plugin/EffectChain.h` — class definition, `EffectSlot` struct (lines 20–26, 232–239)
- `AestraAudio/src/Plugin/EffectChain.cpp` — implementation (465 lines)
- `AestraAudio/include/Core/MixerChannel.h:243` — `EffectChain m_effectChain` value member
- `AestraAudio/include/Core/AudioGraph.h:64` — `EffectChain* effectChain{nullptr}` raw pointer
- `AestraAudio/src/AudioGraphBuilder.cpp:50` — `trackState.effectChain = &channel->getEffectChain()`

### 2.2 MixerChannel (Channel Container)

| Aspect | Detail |
|---|---|
| **Owner object** | `TrackManager` |
| **Owned resource** | `MixerChannel` instances |
| **Ownership mechanism** | `std::vector<std::unique_ptr<MixerChannel>> m_channels` |
| **Who can mutate** | Main thread: `createChannel()`, `removeChannel()`, `clearAllChannels()` |
| **Who can read/process** | Audio thread via raw `EffectChain*` pointers baked into `AudioGraph` |
| **Audio thread visible?** | **Indirectly** — the `EffectChain` embedded in each `MixerChannel` is accessed by the audio thread |
| **Export/headless visible?** | **Yes** |

**Key files:**
- `AestraAudio/include/Models/TrackManager.h:1362` — `std::vector<std::unique_ptr<MixerChannel>> m_channels`
- `AestraAudio/include/Models/TrackManager.h:839–845` — `clearAllChannels()`

### 2.3 PluginManager (Plugin Factory/Registry)

| Aspect | Detail |
|---|---|
| **Owner object** | Singleton |
| **Owned resource** | Plugin creation infrastructure only — does NOT own instances |
| **Ownership mechanism** | `std::vector<std::weak_ptr<IPluginInstance>> m_activeInstances` (observation only) |
| **Who can mutate** | Main thread: `createInstance()`, `createInstanceById()`, `shutdown()` |
| **Who can read/process** | Main thread: `getActiveInstances()`, `getActiveInstanceCount()` |
| **Audio thread visible?** | **No** — PluginManager is not accessed from the audio thread |
| **Export/headless visible?** | **No** — only used for instance creation |

**Key files:**
- `AestraAudio/include/Plugin/PluginManager.h:47` — class definition
- `AestraAudio/src/Plugin/PluginManager.cpp:105–151` — `shutdown()`
- `AestraAudio/src/Plugin/PluginManager.cpp:157–181` — `createInstance()`

### 2.4 UnitManager (Arsenal Units)

| Aspect | Detail |
|---|---|
| **Owner object** | `TrackManager` (by value) |
| **Owned resource** | `std::unordered_map<UnitID, UnitInfo> m_units` |
| **Plugin storage** | `UnitInfo::plugin` = `std::shared_ptr<IPluginInstance>` |
| **Who can mutate** | Main thread: `createUnit()`, `removeUnit()`, `attachPlugin()`, `clear()` |
| **Who can read/process** | Audio thread via `AudioArsenalSnapshot` (immutable `shared_ptr` snapshot) |
| **Audio thread visible?** | **Yes** — via `getAudioSnapshot()` returning `shared_ptr<const AudioArsenalSnapshot>` |
| **Export/headless visible?** | **Yes** — same snapshot path |

**Key files:**
- `AestraAudio/include/Models/UnitManager.h:299–305` — storage
- `AestraAudio/include/Models/UnitManager.h:167–176` — `AudioArsenalSnapshot`
- `AestraAudio/src/Models/UnitManager.cpp:214–234` — `getAudioSnapshot()`
- `AestraAudio/include/Core/AudioEngine.h:682` — `std::atomic<UnitManager*> m_unitManager`

### 2.5 IPluginInstance (Plugin Interface)

| Aspect | Detail |
|---|---|
| **Owner object** | Whoever holds the `shared_ptr<IPluginInstance>` |
| **Concrete types** | `VST3PluginInstance`, `CLAPPluginInstance`, `OutOfProcessPluginInstance`, `SamplerPlugin`, `AestraEQ`, `AestraComp`, `AestraVerb`, `AestraDelay`, `RumbleInstance` |
| **Destructor behavior** | VST3: releases COM objects; CLAP: destroys plugin + closes library; OOP: stops worker + sends IPC shutdown; Built-in: no-op |
| **Shutdown path** | `IPluginInstance::shutdown()` is virtual — each format implements its own |

**Key files:**
- `AestraAudio/include/Plugin/PluginHost.h:93–299` — `IPluginInstance` interface
- `AestraAudio/src/Plugin/VST3Host.cpp:97` — VST3 destructor
- `AestraAudio/src/Plugin/CLAPHost.cpp:113` — CLAP destructor
- `AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:287` — OOP destructor

---

## 3. Mutation Paths

### 3.1 EffectChain Plugin Insertion

| Path | File:Line | Operation | Calling Thread | Audio Concurrent Read? | Lock/Barrier? | Destruction? | Risk |
|---|---|---|---|---|---|---|---|
| `AddPluginCommand::execute()` | `PluginCommands.h:18` | `insertPlugin(slot, plugin)` | Main (via CommandHistory) | **Yes** — `process()` reads slots | **None** | No — plugin just inserted | **High** |
| `PluginUIController::loadPluginToSlot()` | `PluginUIController.cpp:284` | `insertPlugin(slot, instance)` | Main | **Yes** | **None** | No | **High** |
| `TrackManagerUI` drag-drop | `TrackManagerUI.cpp:4781` | `insertPlugin(slot, instance)` | Async callback (main thread) | **Yes** | **None** | No | **High** |
| `AestraContent::loadEffectToSelectedTrack()` | `AestraContent.cpp:2906` | `insertPlugin(slot, instance)` via command | Main | **Yes** | **None** | No | **High** |
| `EffectChain::loadState()` | `EffectChain.cpp:348` | Creates + inserts all plugins | Main (project load) | **No** (loading state) | **None** | No | **Medium** |

### 3.2 EffectChain Plugin Removal

| Path | File:Line | Operation | Calling Thread | Audio Concurrent Read? | Lock/Barrier? | Destruction? | Risk |
|---|---|---|---|---|---|---|---|
| `RemovePluginCommand::execute()` | `PluginCommands.h:45` | `removePlugin(slot)` | Main | **Yes** — `process()` reads slots | **None** | **Yes** — if last `shared_ptr` | **High** |
| `MixerViewModel::removeInsert()` | `MixerViewModel.cpp:872` | `removePlugin(slot)` | Main | **Yes** | **None** | **Yes** | **High** |
| `PluginUIController::removePluginFromSlot()` | `PluginUIController.cpp:323` | `removePlugin(slot)` | Main | **Yes** | **None** | **Yes** | **High** |
| `EffectChain::clear()` | `EffectChain.cpp:137` | Nulls all slots | Main | **Yes** | **None** | **Yes** — drops all shared_ptrs | **High** |

### 3.3 EffectChain Plugin Move/Swap

| Path | File:Line | Operation | Calling Thread | Audio Concurrent Read? | Lock/Barrier? | Destruction? | Risk |
|---|---|---|---|---|---|---|---|
| `EffectChain::movePlugin()` | `EffectChain.cpp:49` | `std::move` + null source | Main | **Yes** | **None** | No (transfer) | **Medium** |
| `EffectChain::swapPlugins()` | `EffectChain.cpp:74` | `std::swap` on two slots | Main | **Yes** | **None** | No | **Medium** |
| `MixerViewModel::moveInsert()` | `MixerViewModel.cpp:832` | move or swap | Main | **Yes** | **None** | No | **Medium** |

### 3.4 Project Load/Unload

| Path | File:Line | Operation | Calling Thread | Audio Concurrent Read? | Lock/Barrier? | Destruction? | Risk |
|---|---|---|---|---|---|---|---|
| `ProjectSerializer::load()` | `ProjectSerializer.cpp:1085` | `clearAllChannels()` | Main | **Yes** — audio may be playing | **None** | **Yes** — destroys all channels + effect chains | **High** |
| `AestraContent::resetToDefaultProject()` | `AestraContent.cpp:2546` | `clearAllChannels()` | Main | **Yes** | **None** | **Yes** | **High** |
| `EffectChain::loadState()` | `EffectChain.cpp:348` | Recreates all plugins in chain | Main | **Yes** (if called during playback) | **None** | **Yes** — replaces existing plugins | **High** |
| `UnitManager::loadFromJSON()` | `UnitManager.cpp:522` | `clear()` + recreate all units | Main | **Yes** (via snapshot) | **Snapshot** — new snapshot published atomically | **Yes** — old snapshot refcount drops | **Low** |

### 3.5 AudioEngine Shutdown

| Path | File:Line | Operation | Calling Thread | Audio Concurrent Read? | Lock/Barrier? | Destruction? | Risk |
|---|---|---|---|---|---|---|---|
| `AudioEngine::panic()` | `AudioEngine.cpp:2606` | `reset()` all effect chains | Main (holds `m_graphMutex`) | **No** — stream should be stopped | **m_graphMutex** | No (deactivate+reactivate) | **Low** |
| `PluginManager::shutdown()` | `PluginManager.cpp:105` | Clears weak_ptrs, stops scanner | Main | **No** | **m_mutex** (internal) | No — does not destroy instances | **Low** |
| `AudioEngine::drainDeferredResourcesForShutdown()` | `AudioEngine.cpp:408` | `GarbageCollector::drainUntilStable()` | Main (post-stream-close) | **No** | N/A | **Yes** — destroys deferred resources | **Low** |

### 3.6 Arsenal Unit Plugin Paths

| Path | File:Line | Operation | Calling Thread | Audio Concurrent Read? | Lock/Barrier? | Destruction? | Risk |
|---|---|---|---|---|---|---|---|
| `UnitManager::attachPlugin()` | `UnitManager.cpp:422` | Sets `shared_ptr` on unit | Main | **Yes** (via snapshot) | **Snapshot** — next `getAudioSnapshot()` sees new plugin | No (transfer in) | **Low** |
| `UnitManager::removeUnit()` | `UnitManager.cpp:291` | Erases from map | Main | **Yes** (via snapshot) | **Snapshot** — old snapshot still holds ref | **Yes** — if last ref | **Low** |
| `UnitManager::clear()` | `UnitManager.cpp:287` | Clears map | Main | **Yes** (via snapshot) | **Snapshot** | **Yes** — drops all refs | **Low** |
| `loadInstrumentToArsenal()` | `AestraContent.cpp:2959` | Creates + attaches plugin | Main | **Yes** (via snapshot) | **Snapshot** | No | **Low** |

---

## 4. Audio Process Reachability

### 4.1 Timeline Mode (`renderGraph`)

```
AudioDriver callback
  └─ AudioEngine::processBlock()                    [AudioEngine.cpp:465]
       ├─ applyPendingCommands()                     [line 513, SPSC drain]
       ├─ renderGraph(graph, numFrames)              [line 735]
       │    ├─ Topological sort of tracks            [line 1723]
       │    └─ Per-track loop:                       [line 1806]
       │         ├─ Clip rendering                   [line 1910]
       │         ├─ Arsenal unit render (per-track)  [line 2080]
       │         │    └─ unit.plugin->process()      [line ~2210]
       │         ├─ EffectChain::process()           [line 2223]
       │         │    └─ For each slot (0..9):
       │         │         ├─ Read slot.plugin (shared_ptr)
       │         │         ├─ Read slot.bypassed (atomic)
       │         │         ├─ Read slot.dryWetMix (atomic)
       │         │         └─ plugin->process()
       │         ├─ Volume/Pan smoothing             [line 2260]
       │         └─ Send routing                     [line 2355]
       ├─ processArsenalUnits()                      [line 769, if pattern mode]
       │    ├─ unitManager->getAudioSnapshot()       [line 2699]
       │    └─ For each unit in snapshot:
       │         └─ unit.plugin->process()           [line 2783]
       └─ Master gain/limiter/dither                 [line 882]
```

### 4.2 Export/Headless Path

```
AudioExporter::render()                             [AudioExporter.cpp:147]
  └─ engine.processBlock(renderBuffer, nullptr)     [same as live path]
       └─ (identical call chain as above)
```

Export uses `processBlock()` directly — the same code path as live playback. The `bounceRangeToWav()` alternative uses `AudioRenderer::renderBlock()` which does NOT call `EffectChain::process()` (only gain/pan smoothing).

### 4.3 Key Observation

The `EffectChain*` pointer in `AudioGraph::TrackRenderState` (line 64 of `AudioGraph.h`) is a **raw pointer** to the `EffectChain` embedded in a `MixerChannel`. The `AudioGraph` is swapped atomically via `EngineState::swapGraph()`, but the `EffectChain` object it points to is the **live mutable object** — not a snapshot. This means:

1. The audio thread reads `m_slots` directly from the live `EffectChain`.
2. The main thread can call `insertPlugin()`/`removePlugin()` on the same `EffectChain` concurrently.
3. There is no synchronization between these accesses.

---

## 5. GC Suitability Analysis

| Resource | GC Suitability | Reasoning |
|---|---|---|
| `EffectChain` slot `shared_ptr<IPluginInstance>` | **Needs snapshot architecture first** | The audio thread reads `slot.plugin` directly from the live object. GC would help with deferred destruction of retired instances, but only if the slot mutation itself is made safe (e.g., via atomic `shared_ptr` swap or snapshot publication). |
| `MixerChannel` lifetime | **Do not GC** | `MixerChannel` is a large composite object with value members (`EffectChain`, `MixerBus`). It is owned by `unique_ptr` in a vector. GC is not the right tool — proper synchronization of `clearAllChannels()` with the audio graph is needed. |
| Arsenal `UnitInfo::plugin` | **GC suitable later** | Arsenal already uses snapshot-based publication (`AudioArsenalSnapshot`). Old snapshots hold `shared_ptr` refs. GC could safely retire old snapshot objects. Current `shared_ptr` refcounting handles this adequately for now. |
| `PluginManager::m_activeInstances` | **Do not GC** | These are `weak_ptr` observations, not ownership. No GC needed. |
| Plugin instance destructors (VST3 COM, CLAP, OOP) | **Needs snapshot architecture first** | Plugin destructors can be expensive (COM release, IPC shutdown). Deferring them via GC is desirable, but only after the audio-thread visibility of the raw `EffectChain*` is resolved. |
| `AudioGraph` snapshot resources | **Do not GC** | `AudioGraph` is double-buffered and swapped atomically. Old graphs are destroyed after swap. This is already safe. |

---

## 6. Specific Danger Signs

### 6.1 Raw Pointer Mirror — EffectChain in AudioGraph

**File:** `AestraAudio/include/Core/AudioGraph.h:64`
```cpp
EffectChain* effectChain{nullptr};
```

Set in `AudioGraphBuilder.cpp:50`:
```cpp
trackState.effectChain = &channel->getEffectChain();
```

The audio thread accesses the live `EffectChain` object through this raw pointer. If the owning `MixerChannel` is destroyed (via `clearAllChannels()`), this pointer becomes dangling. The `AudioGraph` double-buffer swap does not protect against this because `clearAllChannels()` does not acquire `m_graphMutex`.

### 6.2 Vector Mutated While Audio Iterates — EffectChain Slots

**File:** `AestraAudio/src/Plugin/EffectChain.cpp:206–293`

`EffectChain::process()` iterates `m_slots` (a `std::array<EffectSlot, 10>`). Each `EffectSlot::plugin` is a `shared_ptr<IPluginInstance>`. The main thread can call `insertPlugin()` or `removePlugin()` which does `std::move` on the `shared_ptr` and sets it to `nullptr`. This is a data race on the `shared_ptr` control block.

In practice, `shared_ptr` atomic refcounting prevents a crash in most implementations, but:
- The `std::move` in `removePlugin()` may leave the slot in a moved-from state while the audio thread reads it.
- The `if (!plugin) continue;` check in `process()` handles the null case, but there is no formal memory barrier.

### 6.3 Plugin Destruction During Possible Audio Visibility

**File:** `AestraAudio/src/Plugin/EffectChain.cpp:39–47`

`removePlugin()` returns the `shared_ptr` by value. If the caller drops it immediately (e.g., `RemovePluginCommand::execute()` does not store the return value in the undo path), the plugin destructor runs on the main thread while the audio thread may still be calling `plugin->process()` through a previously-cached reference.

The `RemovePluginCommand` does store the removed plugin for undo (`m_removedPlugin`), which keeps it alive until the command is destroyed. But `MixerViewModel::removeInsert()` calls `removePlugin()` via the command, and the command lives in the `CommandHistory` — so the plugin survives until the command is garbage-collected from history. This is an indirect safety mechanism, not a deliberate synchronization barrier.

### 6.4 Project Load Replaces Objects While Audio May Process

**File:** `Source/Core/ProjectSerializer.cpp:1085`
```cpp
trackManager->clearAllChannels();
```

This calls `m_channels.clear()` which destroys all `unique_ptr<MixerChannel>` instances. Each `MixerChannel` destructor destroys its `EffectChain` member, which drops all `shared_ptr<IPluginInstance>` references. If the audio thread is still processing (e.g., transport was playing when load was triggered), it may be iterating the old `AudioGraph` which contains raw `EffectChain*` pointers to now-destroyed objects.

The `AudioGraph` double-buffer swap does not help here because `clearAllChannels()` does not trigger a graph swap — it just destroys the underlying objects.

### 6.5 Async Plugin Creation Callback

**File:** `Source/Components/TrackManagerUI.cpp:4781–4807`

The drag-drop handler uses `createInstanceByIdAsync()` with a callback that calls `chain.insertPlugin()`. This callback runs on the factory's async thread (likely a worker thread), not the main UI thread. If the audio thread is simultaneously calling `chain.process()`, this is a concurrent mutation of `m_slots` without synchronization.

### 6.6 Export/Headless Path Differences

Export uses `processBlock()` directly (same as live). The `bounceRangeToWav()` path uses `AudioRenderer::renderBlock()` which does NOT call `EffectChain::process()`. This means bounced audio does not include insert effects — only gain/pan smoothing. This may be intentional (bounce is for clip rendering, not full mixdown), but it is a behavioral difference from live playback.

---

## 7. Recommendation

### Stage A: Tests & Instrumentation (No behavioral changes)

1. **Add RT-misuse detection for EffectChain mutation.** In debug builds, mark the audio thread and assert if `insertPlugin()`/`removePlugin()`/`clear()` are called while the audio thread may be in `process()`. This can be done with a thread-ID check + atomic flag.

2. **Add a soak test that mutates effect chains while audio processes.** Create a test that rapidly inserts/removes plugins while `processBlock()` runs. This will surface data races under ThreadSanitizer.

3. **Audit `clearAllChannels()` call sites.** Ensure transport is stopped or audio graph is invalidated before calling `clearAllChannels()`. Add assertions or graph-flush logic.

### Stage B: Snapshot Architecture (If needed after Stage A)

1. **Snapshot-based effect chain publication.** Similar to `AudioArsenalSnapshot`, create an immutable snapshot of effect-chain slot state (plugin pointers, bypass, dry/wet) that the audio thread reads. The main thread builds the snapshot and atomically publishes it. The audio thread reads only the snapshot.

2. **Double-buffer EffectChain state.** Use `EngineState`-style double-buffering for per-track effect chain state. The inactive buffer is written by the main thread, then atomically swapped.

3. **Synchronize `clearAllChannels()` with audio graph.** Either stop the audio stream before clearing, or publish an empty graph and wait for the audio thread to consume it before destroying the old channels.

### Stage C: GC Adoption (After Stage B)

1. **Retire old EffectChain snapshots through GC.** Once snapshot architecture is in place, old snapshots containing `shared_ptr<IPluginInstance>` can be retired through `GarbageCollector::release()`.

2. **Retire old plugin instances through GC.** Plugin destructors (especially VST3 COM release) can be expensive. Deferring them to the non-RT collector prevents the main thread from blocking on COM teardown.

3. **Arsenal snapshots are already GC-suitable.** The `AudioArsenalSnapshot` pattern is a good candidate for GC adoption since it already uses `shared_ptr` publication.

### Stage D: Shutdown & Project-Load Hardening

1. **Flush audio graph before `clearAllChannels()`.** Publish an empty graph and wait for the audio thread to consume it (e.g., by spinning on `m_activeRenderTrackIndex` or using a condition variable).

2. **Deactivate plugins before destroying them.** Call `plugin->deactivate()` before dropping the last `shared_ptr`. This gives plugins a chance to release resources gracefully (especially important for VST3/CLAP).

3. **Drain GC queue after project load.** After loading a new project, call `GarbageCollector::drainUntilStable()` to clean up old plugin instances from the previous project.

---

## 8. Stage A Implementation Notes (2026-05-02)

The following guardrails were implemented in this pass:

### 8.1 RT-Misuse Guards Added

The following `EffectChain` mutation methods now call `reportRealtimeMisuse()` at their entry points:

- `insertPlugin()` — returns `false` if called from RT context
- `removePlugin()` — returns `nullptr` if called from RT context
- `movePlugin()` — returns `false` if called from RT context
- `swapPlugins()` — returns `false` if called from RT context
- `clear()` — early returns if called from RT context
- `reset()` — early returns if called from RT context
- `loadState()` — returns `false` if called from RT context

In debug builds, if a handler is installed via `setRealtimeMisuseHandler()`, it is called. Otherwise, an assertion fires. The guards use the existing `RealtimeThreadGuard` infrastructure.

**Location:** `AestraAudio/src/Plugin/EffectChain.cpp`

### 8.2 Mutation Contract Documented

The class-level documentation in `EffectChain.h` was updated to state:

- Slot mutation is NON-RT only.
- Slot mutation must not occur concurrently with `process()`.
- Worker-created plugin instances must be handed back to the main/control thread before insertion.
- The future fix is snapshot-based publication (Stage B); this pass adds debug-time guards only.

**Location:** `AestraAudio/include/Plugin/EffectChain.h` lines 28–47

### 8.3 Async Callback Risk Documented

The async plugin creation callback in `TrackManagerUI.cpp` (line 4781) runs on a worker thread and calls `EffectChain::insertPlugin()` directly. This is a known risk: mutation should occur on the main/control thread.

A comment was added noting:
- The callback runs on a worker thread, not main thread
- This is a documented risk
- Future fix: route through main-thread dispatch (Stage B)

**Location:** `Source/Components/TrackManagerUI.cpp` lines 4776–4781

### 8.4 clearAllChannels Not Fixed

`TrackManager::clearAllChannels()` destroys `MixerChannel` (and its `EffectChain`) without acquiring `AudioEngine::m_graphMutex`. This requires cross-component coordination that cannot be safely fixed without the snapshot architecture (Stage B/C). This remains a documented risk.

### 8.5 Tests Added

Four new test cases were added to `GarbageCollectorTest.cpp`:

- `effectChainInsertPluginRejectsRealtimeContext()` — verifies `insertPlugin()` is rejected from RT context
- `effectChainRemovePluginRejectsRealtimeContext()` — verifies `removePlugin()` is rejected from RT context
- `effectChainClearRejectsRealtimeContext()` — verifies `clear()` is rejected from RT context
- `effectChainNonRtInsertRemoveStillWorks()` — verifies normal non-RT mutation still works

**Location:** `Tests/AestraAudio/GarbageCollectorTest.cpp`

### 8.6 GC Adoption Status

**No GC adoption was added for EffectChain or plugin instances in this pass.** The RT-misuse guards do not route objects through GarbageCollector. GC adoption is still deferred until Stage B (snapshot architecture) is implemented.

### 8.7 What Was NOT Changed

- No snapshot architecture implemented (Stage B deferred)
- No GC adoption for plugin/effect-chain objects
- No DSP behavior changes
- No plugin audio output changes
- No locks added to EffectChain (the guards are assertion-only)
- No main-thread dispatch mechanism added (documented as risk only)

---

## Appendix A: Complete File Index

### EffectChain
- `AestraAudio/include/Plugin/EffectChain.h` — class definition, EffectSlot struct
- `AestraAudio/src/Plugin/EffectChain.cpp` — implementation (465 lines)

### MixerChannel
- `AestraAudio/include/Core/MixerChannel.h` — class definition, owns EffectChain by value
- `AestraAudio/src/Core/MixerChannel.cpp` — `processAudio()` calls `m_effectChain.process()`

### AudioGraph
- `AestraAudio/include/Core/AudioGraph.h` — `TrackRenderState::effectChain` raw pointer
- `AestraAudio/src/AudioGraphBuilder.cpp:50` — sets raw EffectChain pointer

### AudioEngine
- `AestraAudio/include/Core/AudioEngine.h` — EngineState, graph mutex, UnitManager pointer
- `AestraAudio/src/Core/AudioEngine.cpp` — `processBlock()`, `renderGraph()`, `processArsenalUnits()`, `panic()`, `compileGraph()`

### PluginManager
- `AestraAudio/include/Plugin/PluginManager.h` — singleton, weak_ptr tracking
- `AestraAudio/src/Plugin/PluginManager.cpp` — `createInstance()`, `shutdown()`

### Plugin Hosts
- `AestraAudio/include/Plugin/PluginHost.h` — `IPluginInstance` interface
- `AestraAudio/include/Plugin/VST3Host.h` / `src/Plugin/VST3Host.cpp` — VST3 implementation
- `AestraAudio/include/Plugin/CLAPHost.h` / `src/Plugin/CLAPHost.cpp` — CLAP implementation
- `AestraAudio/include/Plugin/OutOfProcessPluginInstance.h` / `src/Plugin/OutOfProcessPluginInstance.cpp` — OOP proxy

### Built-in Plugins
- `AestraAudio/include/Plugin/BuiltInPlugins.h` / `src/Plugin/BuiltInPlugins.cpp` — registration
- `AestraAudio/include/Plugin/SamplerPlugin.h` / `src/Plugin/SamplerPlugin.cpp`
- `AestraAudio/include/Plugin/AestraEQ.h` / `src/Plugin/AestraEQ.cpp`
- `AestraAudio/include/Plugin/AestraComp.h` / `src/Plugin/AestraComp.cpp`
- `AestraAudio/include/Plugin/AestraVerb.h` / `src/Plugin/AestraVerb.cpp`
- `AestraAudio/include/Plugin/AestraDelay.h` / `src/Plugin/AestraDelay.cpp`
- `AestraPlugins/AestraRumble/include/RumbleInstance.h` / `src/RumbleInstance.cpp`

### UnitManager / Arsenal
- `AestraAudio/include/Models/UnitManager.h` — UnitInfo, UnitState, AudioArsenalSnapshot
- `AestraAudio/src/Models/UnitManager.cpp` — implementation (646 lines)

### TrackManager
- `AestraAudio/include/Models/TrackManager.h` — owns MixerChannel vector, UnitManager
- `AestraAudio/src/Models/TrackManager.cpp` — implementation

### Commands
- `AestraAudio/include/Commands/PluginCommands.h` — AddPluginCommand, RemovePluginCommand

### UI Integration
- `AestraUI/Widgets/PluginUIController.h` / `.cpp` — UI plugin loading/removal
- `Source/Components/MixerViewModel.cpp` — `removeInsert()`, `moveInsert()`
- `Source/Components/TrackManagerUI.cpp:4768–4807` — async drag-drop plugin load

### Serialization
- `Source/Core/ProjectSerializer.cpp:662–665` — effect chain save
- `Source/Core/ProjectSerializer.cpp:1268–1278` — effect chain load
- `Source/Core/ProjectSerializer.cpp:1085` — `clearAllChannels()` during load

### Audio Export
- `AestraAudio/src/IO/AudioExporter.cpp:147–167` — `render()` uses `processBlock()`
- `AestraAudio/src/Core/AudioEngine.cpp:2800–2916` — `bounceRangeToWav()` uses `AudioRenderer`
