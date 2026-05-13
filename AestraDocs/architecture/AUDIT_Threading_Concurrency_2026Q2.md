# Threading & Concurrency Audit — 2026Q2

**Status:** Internal — audit findings, no code changed
**Scope:** Every `std::atomic`, every cross-thread `shared_ptr` publish, every mutex, every lock-free queue. Lifetime of RT-published snapshots. Memory-ordering correctness.
**Date:** 2026-05-14
**Auditor:** Cascade
**Companion:** [`AUDIT_RT_Safety_2026Q2.md`](AUDIT_RT_Safety_2026Q2.md), [`ARCHITECTURE_AUDIT_2026Q2.md`](ARCHITECTURE_AUDIT_2026Q2.md), [`PDC-v2-Design.md`](../PDC-v2-Design.md)

> RT-safety asked "what happens on the audio thread". This audit asks "what happens when *two* threads touch the same data". Different lens, overlapping findings.

---

## 1. Executive summary

**Aestra's threading model is well-designed at the macro level, with a few load-bearing patterns and several smaller correctness concerns.**

The macro story:
- **Audio thread** runs `processBlock` and `renderBlock` lock-free against pre-published snapshots.
- **UI thread** mutates the model (TrackManager, PlaylistModel, etc.) under mutexes.
- **Compile thread** (typically UI in practice) runs `compileGraph` under `m_graphMutex` and publishes to the audio thread via the `m_graphStates[2]` double-buffer.
- **Worker threads** (file browser preview decode, autosave, audio export, plugin scan) own their own mutexes and converge on the main thread via condvars or atomic flags.

The micro story has gaps. **2 P0, 6 P1, 4 P2.**

---

## 2. P0 — must fix before v1 Beta

### P0.1 — `std::atomic_load(&shared_ptr)` is C++20-deprecated and used pervasively

**C++20 deprecated** `std::atomic_load`, `std::atomic_store`, `std::atomic_exchange`, `std::atomic_compare_exchange_*` overloads that take `std::shared_ptr<T>*`. They will be **removed** (currently slated for C++26). The replacement is `std::atomic<std::shared_ptr<T>>`.

Aestra uses the deprecated forms heavily:

| File | Sites | Purpose |
|------|------:|---------|
| `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Models/UnitManager.cpp:236, 240` | 2 | Arsenal snapshot publish/consume |
| `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/SamplerPlugin.cpp:124, 184, 193, 212, 218, 238, 267, 426, 463, 627` | 10 | `SampleData` swap on load/normalize/reverse, read on RT path |
| `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Playback/PreviewEngine.cpp:240, 554` | 2 | Active-voice publish from worker, read by `process()` |

**Impact:**
1. **Compiler warning today.** Clang and GCC emit `-Wdeprecated-declarations`. If `-Werror` is ever enabled (it should be), the build breaks.
2. **Future removal.** When the toolchain advances to a compiler that removes the API (very likely in a 2027 LTS), the build silently breaks. Refactoring under deadline pressure is the worst time.
3. **Subtle ABI/ordering nuance.** The deprecated API is a free-function template that internally uses a global hash-table of locks (libstdc++ implementation) or platform-specific atomic primitives. `std::atomic<std::shared_ptr<T>>` is the standardized lock-free-where-possible replacement.

**Remediation:**
1. Add a typedef alias in a shared header: `template<class T> using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;`.
2. Convert each `std::shared_ptr<T> m_x` member that's accessed cross-thread to `AtomicSharedPtr<T> m_x`.
3. Replace `std::atomic_load(&m_x)` with `m_x.load()`, `std::atomic_store(&m_x, p)` with `m_x.store(p)`, `std::atomic_exchange(&m_x, p)` with `m_x.exchange(p)`.
4. Build with `-Wdeprecated-declarations -Werror` to confirm no remaining sites.

**Effort:** 1 day. Mechanical. Test changes are minimal because the semantics are identical.

---

### P0.2 — `calculateLatencyCompensation` writes the active graph state while audio thread reads it

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:3082` builds the latency graph and at the end writes `m_graphStates[activeIdx].maxProjectLatencySamples` and `TrackRTState::compensationDelaySamples` for every track. The audio thread reads these same fields every block.

This is **exactly the G8 gap** documented in [`PDC-v2-Design.md`](../PDC-v2-Design.md) §2:

> **G8** — Recompute touches the active `AudioGraphState` directly. Line 3138 writes `m_graphStates[activeIdx].maxProjectLatencySamples` while audio thread reads the same state. Race on read of `latencyCompensationEnabled` and `maxProjectLatencySamples`. Not catastrophic (single-word reads), but undefined behavior.

The threading audit confirms it as a P0 data race — it is technically UB by the standard, even if it works in practice on x86/ARM with naturally-aligned word writes. ARM with weak memory ordering can in principle reorder against `compensationBuffer` resets.

**Status:** PDC v2 P3 fixes this via double-buffered atomic publish of `SolvedLatencyTopology`. **No new work needed** — but until P3 lands, the race exists.

**Remediation:** ship PDC v2 P3 (already on the critical path).

---

## 3. P1 — fix before v1 Beta but not blockers

### P1.1 — `PlaylistModel::buildRuntimeSnapshot` documents an "audio thread reader" that doesn't exist

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Models/PlaylistModel.h:370-373`:

```cpp
std::unique_ptr<PlaylistRuntimeSnapshot> buildRuntimeSnapshot(const PatternManager& patterns,
                                                              const SourceManager& sources) const {
    // Use shared_lock for readers (audio thread) - allows concurrent reads
    std::shared_lock<std::shared_mutex> lock(m_mutex);
```

The comment claims the audio thread reads via `shared_lock`. But the actual call site (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioGraphBuilder.cpp:52`) is invoked from non-RT graph-build paths, never from `processBlock`. **The comment is wrong**, or — worse — there's a hidden RT call site I missed.

Reality:
- The audio thread reads a **snapshot pointer** (the `PlaylistRuntimeSnapshot` returned here) that was built off-RT.
- `shared_lock` is acquired only off-RT.
- The "shared_lock for audio thread" framing is misleading — even shared locks are not RT-safe (they're still locks).

**Remediation:**
1. Fix the comment to reflect reality: "shared_lock for concurrent non-RT readers".
2. If the snapshot publish-and-consume protocol isn't already lock-free at the boundary (snapshot pointer → RT), document explicitly how it is. (It likely is, via `unique_ptr` ownership transfer to RT thread; need to confirm in graph-build pipeline.)
3. Audit all other comments that say "audio thread" to make sure they're accurate. Misleading docs make future agents (and future you) make wrong calls.

**Effort:** 2 hours to verify and re-comment.

---

### P1.2 — `MixerChannel::m_sendMutex` is a footgun even though current RT path avoids it

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/MixerChannel.cpp:161-165`:

```cpp
// B-005: getSends() must never be called from the audio callback (RT) thread.
// Calling from RT can cause lock contention or deadlock.
std::lock_guard<std::mutex> lock(m_sendMutex);
return m_sends;
```

The mutex protects `std::vector<AudioRoute> m_sends`. Every accessor takes the lock. **The comment cites a past bug** (B-005), suggesting this was learned the hard way.

Current RT path is safe because:
- `compileGraph` (off-RT) reads sends under the mutex.
- Built-output is published to `m_rtTopoEdges` / `AudioGraphState`.
- RT path reads from the published structures, never from `m_sends`.

**The footgun:**
- There is **no compile-time enforcement** that nobody on the RT path ever calls `getSends()`. A future refactor could re-introduce B-005 silently.
- `calculateLatencyCompensation` (PDC v1) calls `getSends()` at line 3198 — that's off-RT today, but PDC v2 P4b ("engine consumes `EdgeSolution::compensationSamples` via per-send ring buffers in `processBlock`") will be very close to the RT boundary. Whoever wires it up needs to *not* call `getSends()` from RT.

**Remediation:**
1. Add `reportRealtimeMisuse("MixerChannel::getSends")` at the top of `getSends()` and every send mutator. Same pattern used in `TrackManager::addChannel` and `EffectChain::insertPlugin`. This converts the comment into an enforced contract.
2. Provide an RT-safe `getSendsSnapshot()` that returns a const-ref to a published vector (built in `compileGraph`), and require RT path to use only that variant.

**Effort:** 1 day.

---

### P1.3 — Worker threads in `RealTimeThreadPool` don't install `ScopedRealtimeAudioThread`

If parallel rendering enables (`m_multiThreadingEnabled.store(true)`), the audio callback dispatches work to worker threads in `m_threadPool`. Those workers run DSP code that may transitively call functions guarded by `reportRealtimeMisuse(...)` — but the workers haven't entered an RT scope, so the guard doesn't fire.

Already P2.2 in the RT-safety audit. Re-flagged here because it's also a threading discipline gap: the RT designation is per-thread, not per-codepath.

**Remediation:** every worker thread that runs DSP must construct `ScopedRealtimeAudioThread` at task entry, identical to the audio callback's discipline.

**Effort:** 4 hours.

---

### P1.4 — `PreviewEngine::process` has a mutex but documents itself as "off-RT only"

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Playback/PreviewEngine.cpp:233-238`:

```cpp
void PreviewEngine::process(float* interleavedOutput, uint32_t numFrames) {
    // WARNING: This function acquires a mutex and is NOT RT-safe.
    // Must not be called from the audio callback thread.
    // If wired into a new audio path, first refactor to remove the
    // mutex in the completion handler below.
    reportRealtimeMisuse("PreviewEngine::process");
```

The audio thread only calls `preview->isPlaying()` (a single atomic read at `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:848-850`), not `process()`. So today there's no RT violation. But:

- The function is named `process()` — every other DSP class's `process()` runs on RT. Naming collision invites accidents.
- The completion path (line 546-552) holds `m_completedPathMutex` while writing `m_completedPathStr`. If preview ever gets wired into the RT mix, this mutex is the first thing that breaks.

**Remediation:**
1. Rename to `processOffRt()` or move into a dedicated `PreviewMixer` class that's documented as non-RT.
2. Replace the `std::string m_completedPathStr` + mutex with an SPSC ring buffer of `std::array<char, 256>` (or similar fixed-size buffer) so the completion signal is lock-free.

**Effort:** half a day.

---

### P1.5 — `m_completionPending` + `m_completedPathMutex` pattern in PreviewEngine

Related to P1.4. The completion handoff is:

```cpp
// In process() — supposedly off-RT today, but the pattern is wrong for RT
{
    std::lock_guard<std::mutex> lock(m_completedPathMutex);
    m_completedPathStr = voice->path;        // ← std::string assignment under mutex
}
m_completionPending.store(true, ...);
```

The pattern: atomic flag signals "data ready", but the data itself is protected by a mutex. **The consumer must take the mutex to read it**. So even though "checking if completed" is lock-free, "getting the completed path" is not.

For an UI-thread consumer this is fine. For an RT-thread consumer this would be broken. Today it's the former, so it works — but the pattern teaches anyone reading it the wrong lesson.

**Remediation:** rolled into P1.4.

---

### P1.6 — Many `memory_order_relaxed` stores publish data that consumers depend on

Spot-checked sample:

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:516-519`:
```cpp
auto cb = m_inputCallback.load(std::memory_order_relaxed);
if (cb) {
    cb(inputBuffer, numFrames, m_inputCallbackData.load(std::memory_order_relaxed));
}
```

This is a function pointer read with relaxed ordering. If the producer thread does `m_inputCallbackData.store(newPtr, relaxed); m_inputCallback.store(cb, relaxed);` then the RT thread can observe a new `cb` with stale `m_inputCallbackData`. Result: the new callback is invoked with the old context pointer → use-after-free or wrong-tenant data.

On x86 this happens to work due to TSO. On ARM (Apple silicon, AWS Graviton, future Linux laptops) it can break.

**Other suspects** (not exhaustive — 543 `memory_order_*` occurrences across 24 files; need a systematic pass):

- Anywhere two relaxed stores on the producer side are correlated and the consumer reads them with relaxed loads.
- Any "publish snapshot pointer; then mutate flags" pattern where the pointer is `release` but the flags are `relaxed`.

**Remediation:**
1. Audit every paired (pointer, payload) store/load. Pointer/handle should be `release` on store, `acquire` on load; payload reads can stay relaxed.
2. For unrelated counters/flags (e.g. `m_blocksProcessed`), `relaxed` is correct.
3. Establish a project convention: "If a load depends on what another load returned, the producer must `release` and consumer must `acquire`."
4. Run TSan on the headless test suite — it catches most of these.

**Effort:** 2 days for the audit pass + TSan run.

---

## 4. P2 — quality-of-life, not blockers

### P2.1 — `applyPendingCommands` drains max 16 per block but doesn't report overflow

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:151`:
```cpp
while (cmdCount < 16 && m_commandQueue.pop(cmd)) { ... }
```

If the UI thread floods commands faster than the audio thread can drain, commands accumulate in the queue. Eventually the queue capacity is hit and `m_commandQueue.push(cmd)` fails on the producer side. Today there's no telemetry on either:
- Audio-thread budget exhaustion (didn't drain because of the 16 cap).
- Producer-side push failure.

**Remediation:** add two counters — `m_commandsDeferredDueToBudget` and `m_commandPushFailures` — visible in telemetry. Surface in a debug HUD.

**Effort:** 2 hours.

---

### P2.2 — `m_graphMutex` is `std::mutex` but could be a spinlock for short critical sections

`m_graphMutex` is held during `compileGraph` (long, may include allocations) — so a sleeping mutex is correct. But it's also held in `bounceRange` for the duration of the entire bounce (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2982`). During bounce, the RT audio callback (which doesn't take the lock) keeps running but no one else can recompile the graph.

This is fine for bounce (long, infrequent). But the *pattern* could mislead future code into holding `m_graphMutex` from the RT thread "for a quick check". Don't.

**Remediation:** rename `m_graphMutex` to `m_offRtGraphMutex` or add a comment "MUST NEVER be acquired from the audio thread." Same family as P1.2.

**Effort:** 15 minutes.

---

### P2.3 — `PatternPlaybackEngine` has `m_mutex` (off-RT) + `m_rtQueue` (lock-free SPSC) — both correct, but the boundary is implicit

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Playback/PatternPlaybackEngine.h:222-228`:

```cpp
// Active instances (scheduler thread only - LOCK REQUIRED if called from RT)
std::vector<PatternInstance> m_activeInstances;
mutable std::mutex m_mutex;

// RT event queue
LockFreeSPSCQueue<ScheduledEvent, 8192> m_rtQueue;
```

The split is clean (mutex protects state that only the off-RT scheduler reads/writes; SPSC queue ferries events to RT). But the comment "LOCK REQUIRED if called from RT" is misleading — if RT calls this, taking the lock **fails the RT contract**. The fix is to never call mutex-protected methods from RT, not to take the lock from RT.

**Remediation:** rewrite the comment as "OFF-RT ONLY — these members are accessed only from the scheduler thread under m_mutex. RT access uses m_rtQueue and dedicated noexcept methods." Same family as P1.1.

**Effort:** 15 minutes.

---

### P2.4 — `GarbageCollector` is a singleton with RT-safe `release()` — verify deferred-deletion ordering guarantees

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/GarbageCollector.h` (already exercised in tests). Pattern:
- RT thread calls `GarbageCollector::instance().release(sharedPtr, "tag")` — the smart pointer is queued for off-RT destruction.
- An off-RT janitor thread (or `drainDeferredResourcesForShutdown`) runs the destructors.

This pattern is critical for things like Sampler `SampleData` swaps. The audit-time question: **is destruction order across release() calls deterministic enough that no destructor needs to reference another already-collected object?**

Most cases: `SampleData` etc. own their own storage and don't reference other GC'd objects, so destruction order doesn't matter. But Arsenal `AudioArsenalSnapshot` references unit plugin instances which may also be GC'd. If a parent snapshot is destroyed before the units it points to are released by the engine, fine. The other direction would be bad — but pointer ownership precludes it.

**Remediation:** doc-only. Add a section in `GarbageCollector.h` explaining the ownership invariants. Spot-check the snapshot-publish callers for any object graphs that could double-collect.

**Effort:** 2 hours.

---

## 5. What's already excellent

| Item | Where |
|------|-------|
| Lock-free SPSC queue for RT commands | `m_commandQueue` in AudioEngine, `m_rtQueue` in PatternPlaybackEngine |
| Double-buffered graph state | `m_graphStates[2]` + `m_activeRenderTrackIndex` |
| Atomic + GC pattern for snapshot publish | UnitManager, SamplerPlugin (despite P0.1's deprecation issue) |
| Mutex hygiene for off-RT code | Every UI-mutable model uses `std::mutex`/`std::shared_mutex` consistently |
| `noexcept` on the RT-safe PatternPlaybackEngine overload | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Playback/PatternPlaybackEngine.cpp:305` |
| `m_completionPending` deferred-completion pattern (for off-RT consumers) | PreviewEngine — pattern is correct for non-RT, just don't extend it to RT |
| Conscious tracking of past bugs (B-005) | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/MixerChannel.cpp:161` — the comment proves the team learns from incidents |
| Test-side concurrency harness | Multiple tests install custom RT-misuse handlers and verify the discipline holds under stress |

---

## 6. Remediation roadmap

| Sprint | Item | Effort | Risk reduction |
|--------|------|--------|----------------|
| **This week** | P0.1 `std::atomic<std::shared_ptr<T>>` migration | 1d | Future-proofs against C++26 removal; fixes -Wdeprecated |
| Week 2 (with PDC v2 P3) | P0.2 graph-state write race | (in PDC v2) | UB removed |
| Week 2 | P1.1, P2.3 comment corrections (PlaylistModel, PatternPlaybackEngine) | 2h | Stop misleading future code |
| Week 2 | P1.2 enforce `getSends()` non-RT contract | 1d | Compile-time guarantee against B-005 reintroduction |
| Week 2 | P1.3 worker-thread `ScopedRealtimeAudioThread` | 4h | RT guard works for all RT-priority threads |
| Week 3 | P1.4, P1.5 PreviewEngine refactor | 0.5d | Future-proof if preview moves to RT |
| Week 3 | P1.6 memory-ordering audit + TSan run | 2d | Catches the kind of bug that ships and crashes 1-in-1000 users |
| Week 4 | P2.1, P2.2, P2.4 polish | 1d | Makes correctness self-documenting |

**Total: ~1 week of focused work**, easily parallelizable with PDC v2 since most of these touch non-RT code.

---

## 7. Tests to add

Forms `ThreadingRegressionSuite`:

1. **`AtomicSharedPtrApiTest`** — `static_assert` that every `m_publishedSnapshot`-like member is `std::atomic<std::shared_ptr<T>>` (post-P0.1).
2. **`GetSendsRtRejectedTest`** — call `getSends()` inside `ScopedRealtimeAudioThread`, assert the misuse handler fires (post-P1.2).
3. **`ThreadPoolWorkerRtScopeTest`** — submit a job to `RealTimeThreadPool` that internally calls a non-RT API; assert misuse handler fires (post-P1.3).
4. **`MemoryOrderingRaceTest`** — fuzz-style: writer thread publishes (data, ptr) pairs; reader thread reads ptr then data; assert data is consistent with ptr's generation. Run under TSan.
5. **`CommandQueueOverflowTelemetryTest`** — flood `m_commandQueue` from the UI side, run RT for N blocks, assert the deferred-due-to-budget counter incremented (post-P2.1).

Pairs with `RTSafetyRegressionSuite` from the RT-safety audit and the "lock the A+" suite from `Path-to-All-A.md` §8.

---

## 8. Open questions

1. **PlaylistModel snapshot lifetime.** Need to confirm the `unique_ptr<PlaylistRuntimeSnapshot>` is published to RT via atomic exchange or some other handoff. If it's via raw pointer + atomic flag, that's another P0 candidate.
2. **`MasterSafetyLimiter` thread-safety.** Member state mutated on the RT thread only (single-writer), parameters set via atomics. Need to confirm no shadow path via UI → settings → safety limiter that races.
3. **`AudioDeviceManager` callback installation.** When the device changes, the callback handoff between old and new audio thread is the kind of place memory ordering bugs hide. Worth a focused look during the next audit pass.
