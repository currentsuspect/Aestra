# EffectChain Snapshot Publication Design

**Date:** 2026-05-02
**Branch:** `develop` @ `8d878097`
**Scope:** Stage B Design — snapshot-based EffectChain publication for RT-safety.

---

## 1. Executive Summary

This document designs the snapshot architecture that replaces raw mutable `EffectChain` access in audio/render paths. The design is based on the existing `AudioArsenalSnapshot` pattern already proven in the codebase.

**Recommended approach:** Candidate A — Snapshot of plugin slots, phased implementation.

**Key design decisions:**
- `EffectChain` remains mutable on control side.
- Immutable `EffectChainSnapshot` objects are published on each mutation.
- Audio thread reads only the snapshot via `shared_ptr<const EffectChainSnapshot>`.
- Old snapshots retire naturally via `shared_ptr` refcounting; GC adoption follows in later pass.

---

## 2. Current Architecture Trace

### 2.1 Ownership Chain

```
TrackManager
  └─ m_channels: vector<unique_ptr<MixerChannel>>
       └─ MixerChannel
            └─ m_effectChain: EffectChain (by value)
                 └─ m_slots: array<EffectSlot, 10>
                      └─ EffectSlot::plugin: shared_ptr<IPluginInstance>
```

### 2.2 Audio Read Path

```
AudioEngine::processBlock() [line 465]
  └─ renderGraph() [line 735]
       └─ For each track in graph.tracks:
            ├─ track.effectChain->process() [line 2223]
            │    └─ Iterates m_slots directly (raw access)
            └─ track.effectChain is raw EffectChain* from AudioGraphBuilder
```

**Critical line:** `AudioGraphBuilder.cpp:50`
```cpp
trackState.effectChain = &channel->getEffectChain();
```

### 2.3 Mutation Paths

| Mutation | Location | Thread | RT Guard |
|---|---|---|---|
| `insertPlugin()` | EffectChain.cpp:18 | Main | Yes (Stage A) |
| `removePlugin()` | EffectChain.cpp:39 | Main | Yes |
| `movePlugin()` | EffectChain.cpp:49 | Main | Yes |
| `swapPlugins()` | EffectChain.cpp:74 | Main | Yes |
| `clear()` | EffectChain.cpp:137 | Main | Yes |
| `reset()` | EffectChain.cpp:445 | Main | Yes |
| `loadState()` | EffectChain.cpp:348 | Main | Yes |
| `clearAllChannels()` | TrackManager.h:839 | Main | No |
| Async plugin creation | TrackManagerUI.cpp:4781 | Worker | No |

### 2.4 Known Remaining Risks

1. `AudioGraph` holds raw `EffectChain*` — no snapshot.
2. `clearAllChannels()` destroys channels without graph mutex.
3. Async plugin callback inserts from worker thread (not main).
4. No synchronization between slot mutation and audio iteration.
5. GC adoption deferred until snapshot architecture exists.

---

## 3. Candidate Designs Evaluated

### 3.1 Candidate A — Snapshot of Plugin Slots

**Concept:**
- `EffectChain` remains mutable on control side.
- On any mutation (`insertPlugin`, `removePlugin`, etc.), rebuild immutable `EffectChainSnapshot`.
- Publish snapshot via `std::shared_ptr<const EffectChainSnapshot>`.
- Audio thread reads only the snapshot.

**Audio read:**
- `TrackRenderState` stores `shared_ptr<const EffectChainSnapshot>` instead of raw `EffectChain*`.
- `process()` iterates snapshot's slot array.

**Control mutation:**
- After mutating slots, build new snapshot.
- Atomically publish via `m_currentSnapshot.exchange(newSnapshot)`.

**Plugin lifetime:**
- Snapshot holds `shared_ptr<IPluginInstance>` copies.
- Old snapshot's `shared_ptr`s drop when last reference dies.
- No GC needed for basic function; GC optional for deferred destruction.

**RT safety:**
- Audio thread reads immutable snapshot only.
- No mutable access during RT.

**Code churn:**
- Medium — new snapshot types, modified AudioGraphBuilder, modified process path.

**Testability:**
- High — snapshot can be tested independently; audio path verification straightforward.

**Recommendation:** ✅ **Selected**

### 3.2 Candidate B — Snapshot of Whole Mixer Graph

**Concept:**
- Control side builds entire immutable `AudioGraphSnapshot`.
- Audio thread processes only the immutable snapshot.
- All track/channel/effect-chain data in one atomic snapshot.

**Audio read:**
- Entire graph is immutable snapshot.

**Control mutation:**
- Rebuild entire graph on any change.

**Plugin lifetime:**
- Handled by snapshot.

**RT safety:**
- Strong — entire graph immutable.

**Code churn:**
- High — major architectural change.

**Testability:**
- Medium — larger surface area.

**Recommendation:** Consider for Stage C+ if granular snapshot proves insufficient.

### 3.3 Candidate C — Lock-Based Synchronization

**Concept:**
- Add mutex around EffectChain mutation and graph processing.

**Why not:**
- Lock in audio callback path is unacceptable for RT.
- Would add latency and jitter.

**Recommendation:** ❌ Rejected

---

## 4. Recommended Design

### 4.1 New Types Needed

```cpp
// Immutable snapshot of a single effect slot
struct EffectSlotSnapshot {
    std::shared_ptr<IPluginInstance> plugin; // shared ownership
    bool bypassed;                           // copied value (atomic on mutable)
    float dryWetMix;                         // copied value
};

// Immutable snapshot of entire effect chain
class EffectChainSnapshot {
public:
    std::array<EffectSlotSnapshot, EffectChain::MAX_SLOTS> slots;
    uint32_t latencySamples;                 // sum of plugin latencies

    // Factory method: build from mutable EffectChain
    static std::shared_ptr<const EffectChainSnapshot> build(const EffectChain& chain);
};

// AudioGraph uses snapshot instead of raw pointer
struct TrackRenderState {
    // ...
    std::shared_ptr<const EffectChainSnapshot> effectChain; // replaces raw EffectChain*
};
```

### 4.2 Ownership Rules

| Entity | Owner | Notes |
|---|---|---|
| Mutable `EffectChain` | `MixerChannel` | Stays by value, mutated on main thread |
| `EffectChainSnapshot` | `TrackRenderState` via `shared_ptr` | Immutable, published atomically |
| Plugin `shared_ptr` in snapshot | Snapshot holds copy | Audio thread holds via snapshot |
| Plugin `shared_ptr` in mutable chain | EffectSlot | Control side manages |

### 4.3 Publication Rules

1. **When to rebuild snapshot:**
   - After any slot mutation (`insertPlugin`, `removePlugin`, `movePlugin`, `swapPlugins`, `clear`, `reset`, `loadState`).
   - After `EffectChain::prepare()` is called (plugins added/removed).

2. **How to publish:**
   - Build new `shared_ptr<const EffectChainSnapshot>`.
   - Store in `TrackRenderState::effectChain` via atomic exchange or graph rebuild.

3. **How AudioGraph receives it:**
   - `AudioGraphBuilder::buildFromTrackManager()` calls `EffectChainSnapshot::build(chain)` for each channel.
   - Graph is already double-buffered via `EngineState::swapGraph()`.

### 4.4 Thread Rules

- **Mutation:** Main/control thread only — existing RT-misuse guards remain.
- **Worker thread plugin creation:** Must dispatch insertion to main thread, not call `insertPlugin` directly.
- **Audio thread:** Never mutates — reads immutable snapshot only.

### 4.5 Retirement Rules

- **Natural retirement:** Old snapshot's `shared_ptr` refs drop when last reader releases.
- **GC (optional, later):** Old snapshots can be retired through GarbageCollector if deferred destruction is needed.
- **Plugin destructors:** Must not run on RT thread — GC already ensures non-RT destruction.

### 4.6 Shutdown/Project Close Rules

1. Stop transport / pause audio stream.
2. Swap out graph (publish empty or new graph without affected channels).
3. Wait for audio thread to consume old snapshot (double-buffer swap already does this).
4. Destroy mutable `MixerChannel` / `EffectChain` objects.
5. Call `GarbageCollector::drainUntilStable()` to clean up retired snapshots.

### 4.7 Export/Headless Rules

- Must use same snapshot publication rules as live engine.
- No separate unsafe path — export uses `processBlock()` which reads snapshot.

---

## 5. Implementation Plan

### Pass 1 — Snapshot Type + Tests

**Goal:** Add immutable snapshot type without audio graph changes.

**Status:** ✅ IMPLEMENTED (2026-05-02, develop @ 77aec3fe)

**Changes:**
- Added `EffectChainSnapshotSlot` and `EffectChainSnapshot` types to `EffectChain.h` (embedded, not separate file per Pass 1 simplicity).
- Added `EffectChain::createSnapshot()` method.
- Added tests in `Tests/AestraAudio/EffectChainSnapshotTest.cpp`.

**Files:**
- Modify: `AestraAudio/include/Plugin/EffectChain.h`
- Modify: `AestraAudio/src/Plugin/EffectChain.cpp`
- New: `Tests/AestraAudio/EffectChainSnapshotTest.cpp`

### Pass 2 — EffectChain Publication

**Goal:** EffectChain rebuilds/publishes snapshot on mutation.

**Changes:**
- Add `EffectChain::getSnapshot()` method that returns `shared_ptr<const EffectChainSnapshot>`.
- Mutation methods call internal `publishSnapshot()` after modifying slots.
- Tests verify insert/remove/move/swap/loadState all produce consistent snapshots.

**Files:**
- Modify: `AestraAudio/include/Plugin/EffectChain.h`
- Modify: `AestraAudio/src/Plugin/EffectChain.cpp`
- Tests: `EffectChainSnapshotTest.cpp`

### Pass 3 — AudioGraph Consumes Snapshots

**Goal:** Replace raw `EffectChain*` with `shared_ptr<const EffectChainSnapshot>`.

**Changes:**
- Modify `TrackRenderState::effectChain` to use snapshot type.
- Modify `AudioGraphBuilder` to build and store snapshots.
- Modify `renderGraph()` to iterate snapshot slots.
- Tests for channel clear while old graph snapshot exists.

**Files:**
- Modify: `AestraAudio/include/Core/AudioGraph.h`
- Modify: `AestraAudio/src/AudioGraphBuilder.cpp`
- Modify: `AestraAudio/src/Core/AudioEngine.cpp` (render path)
- Tests: `EffectChainSnapshotTest.cpp`

### Pass 4 — Async Insertion Dispatch

**Goal:** Worker-created plugin instances inserted on control thread only.

**Changes:**
- Modify `TrackManagerUI.cpp` async callback to dispatch to main thread.
- No direct `insertPlugin` from worker.

**Files:**
- Modify: `Source/Components/TrackManagerUI.cpp`
- Tests: existing plugin insertion tests

### Pass 5 — Retirement/GC Adoption

**Goal:** Optional GC for old snapshots or plugin refs.

**Changes:**
- If needed, retire old snapshots via `GarbageCollector::release()`.
- Tests for GC behavior with snapshots.

**Files:**
- Modify: `AestraAudio/src/Plugin/EffectChain.cpp` (optional)
- Tests: `GarbageCollectorTest.cpp`

### Pass 6 — clearAllChannels / Project Close Hardening

**Goal:** Safe channel destruction during active graph.

**Changes:**
- Publish empty graph before clearing channels.
- Test for safe destruction during active playback.

**Files:**
- Modify: `AestraAudio/src/Core/AudioEngine.cpp`
- Modify: `Source/Core/ProjectSerializer.cpp`
- Tests: `ProjectLoadRegressionTest.cpp`

---

## 6. Risk and Test Matrix

| Test Case | Target Pass | Description |
|---|---|---|
| Non-RT insert updates snapshot | 2 | Verify snapshot reflects new plugin after insert |
| Non-RT remove updates snapshot | 2 | Verify snapshot reflects removal |
| Audio holds old snapshot during mutation | 3 | Audio processing safe while mutation occurs |
| clearAllChannels safe with active graph | 6 | No dangling pointers after channel clear |
| Async plugin insert dispatched to main | 4 | Worker thread does not directly mutate |
| Project load/unload swaps safely | 6 | Graph swap during load is safe |
| Export uses same snapshot path | 3 | Offline render uses live path |
| Plugin destructor not on RT | 5 | GC/destruction non-RT |
| Bypass/state/load unchanged | 1,3 | Behavioral parity with mutable version |
| Audio output parity before/after | 3 | No DSP/ audio changes from snapshot adoption |

---

## 7. GC Adoption Status

**Still deferred.** GC adoption for EffectChain/plugin objects requires snapshot architecture (Pass 5+). The foundation is now in place with the design — implementation can proceed in smaller passes.

---

## 8. Appendix: Reference Implementation Patterns

### 8.1 AudioArsenalSnapshot Pattern (Existing)

The recommended design mirrors the existing `AudioArsenalSnapshot` pattern:

```cpp
// UnitManager.h
std::shared_ptr<const AudioArsenalSnapshot> getAudioSnapshot() const {
    auto snapshot = std::make_shared<AudioArsenalSnapshot>();
    for (UnitID id : m_unitOrder) {
        const auto* unit = getUnit(id);
        UnitState state{};
        state.plugin = unit->plugin; // shared_ptr copy
        snapshot->units.push_back(std::move(state));
    }
    return snapshot;
}
```

The EffectChain snapshot should follow the same pattern.

### 8.2 EngineState Double-Buffer Pattern (Existing)

```cpp
// EngineState.h
class EngineState {
    AudioGraph m_graphs[2];
    std::atomic<int> m_activeIndex{0};

    const AudioGraph& activeGraph() const noexcept {
        return m_graphs[m_activeIndex.load(std::memory_order_acquire)];
    }

    void swapGraph(const AudioGraph& next) {
        const int inactive = 1 - m_activeIndex.load(std::memory_order_relaxed);
        m_graphs[inactive] = next;
        m_activeIndex.store(inactive, std::memory_order_release);
    }
};
```

EffectChain snapshots are naturally compatible with this pattern — the graph contains snapshots, and the double-buffer swap publishes them atomically.