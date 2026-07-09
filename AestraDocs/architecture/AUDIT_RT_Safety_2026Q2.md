# RT-Safety Deep Audit — 2026Q2

**Status:** Internal — audit findings, no code changed
**Scope:** Every code path reachable from `AudioEngine::processBlock`, validated against AGENTS.md §10 (forbidden RT operations).
**Date:** 2026-05-14
**Auditor:** Cascade
**Companion:** [`ARCHITECTURE_AUDIT_2026Q2.md`](ARCHITECTURE_AUDIT_2026Q2.md) (cross-module audit), [`PDC-v2-Design.md`](../PDC-v2-Design.md) (RT-path consumer)

> The arch audit treated RT safety as one section. This doc is the deep dive: every plausible violation surface, with file/line evidence, and a clear P0/P1/P2 ranking.

---

## 1. Executive summary

**Aestra's RT discipline is good but has one critical hole and several locked-but-undefended doors.**

- The RT thread guard (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/RealtimeThreadGuard.h:39-46`) is well-designed and used consistently in non-RT methods to reject misuse early.
- `AudioRenderer.cpp` contains **no** locks, sleeps, logging, exception sites, file I/O, or `std::string` operations — clean.
- The `AudioRenderer` and DSP paths are **almost** allocation-free; the residual sites are bounded, well-documented, or tied to specific concerns.

**The critical hole** is that the RT-violation reporting handler (`g_realtimeMisuseHandler`) is **never installed in production code** — only test code installs it. Release builds will silently swallow RT violations with no log, no metric, no crash. We have no telemetry on whether production has ever hit one.

**Findings count:** 4 P0, 6 P1, 3 P2.

---

## 2. P0 — must fix before v1 Beta

### P0.1 — No RT-violation handler installed in production

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/RealtimeThreadGuard.h:23-37`:

```cpp
inline bool reportRealtimeMisuse(const char* apiName) noexcept {
    if (!isRealtimeAudioThread()) {
        return false;
    }
    if (auto handler = g_realtimeMisuseHandler.load(std::memory_order_acquire)) {
        handler(apiName);
    } else {
#ifndef NDEBUG
        assert(false && "Non-real-time API called from the audio thread");
#endif
    }
    return true;
}
```

**The only callers of `setRealtimeMisuseHandler` are test files:**
- `@/home/currentsuspect/Dev/Aestra/Tests/AestraAudio/MixerChannelScratchBufferTest.cpp:208`
- `@/home/currentsuspect/Dev/Aestra/Tests/AestraAudio/GarbageCollectorTest.cpp:185, 235, 376, 432, 447, 467, 489`
- `@/home/currentsuspect/Dev/Aestra/Tests/AestraAudio/EffectChainSnapshotTest.cpp:166`

No production code installs a handler. So in release builds:
- The early-return inside the misusing function still happens (good — operation is rejected).
- But **no log, no counter, no crash report.** A field user hitting this never tells us.
- In debug builds: `assert(false)` fires, but only the developer sees it.

**Impact:** We ship blind. The arch audit flagged this from a different angle (`AudioThreadConstraints.h` violation counters are dead). Same problem.

**Remediation:**
1. At engine startup, install a default handler that logs to the engine's telemetry counter (`Telemetry::incrementRealtimeMisuse(apiName)`) and writes one rate-limited entry to the crash-report breadcrumb buffer.
2. Add a release-mode test that boots `AudioEngine`, deliberately triggers a misuse via a controlled non-RT call inside `ScopedRealtimeAudioThread`, asserts the counter incremented.
3. Plumb the counter into the planned crash reporter (Phase 4 roadmap item).

**Effort:** 1 day. Trivial in scope, high in audit confidence.

---

### P0.2 — Plugin exceptions unhandled on Linux

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/VST3Host.cpp:62-72`:

```cpp
// This function must NOT contain any objects with destructors.
static bool SafeProcessCall(IAudioProcessor* processor, ProcessData& data) {
#ifdef _WIN32
    __try {
        processor->process(data);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ...
    }
#endif
```

**SEH (`__try`/`__except`) is Windows-only.** On Linux, a plugin throwing a C++ exception from `processor->process(data)` will unwind through the audio callback. Per AGENTS.md §10, exceptions on the RT thread are forbidden — but Aestra has no enforcement, no catch barrier.

`plugin->process()` is called from 6+ sites on the RT path with **zero** try/catch wrappers:
- `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/EffectChain.cpp:306, 316, 326` (per-slot track FX)
- `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/EffectChain.cpp:400, 412` (snapshot variant)
- `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:365, 397` (Arsenal route + master)
- `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2903` (Arsenal export path)

A misbehaving third-party plugin on Linux either:
- Throws → unwinds across the RT callback → undefined behavior, possibly hard crash.
- Hits a SIGSEGV → no recovery, immediate crash.

**Remediation:**
1. **Process-isolation host:** the existing `AestraPluginHostMain` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/AestraPluginHostMain.cpp`) is a sandbox harness. If it can be wired through for VST3 + CLAP on Linux, plugin crashes become recoverable. This is the right long-term answer.
2. **Short-term:** wrap every `plugin->process(...)` call in a no-objects-with-destructors lambda guarded by `catch (...)` on Linux (acknowledging that catching async signals like SIGSEGV requires `sigaction` + `siglongjmp`, which is hostile to RT). At minimum, wrap with `catch (...)` to stop C++ exception propagation.
3. **Marker:** add `noexcept` to `IPluginInstance::process` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/PluginHost.h:151`). This converts an exception escape into `std::terminate` — same outcome as crashing, but **deterministic** and **catchable by crash reporter**.

**Effort:** Short-term `catch (...)` + `noexcept` marker: 2 days. Process-isolation host: separate effort tracked in Plugin Hosting audit (Tier-1 audit #4).

---

### P0.3 — Plugin NaN/Inf outputs poison the mix

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:930-942` sanitizes NaN/Inf in the **final master sum**:

```cpp
if (std::isnan(L) || std::isinf(L)) { L = 0.0; nanCount++; ... }
if (std::isnan(R) || std::isinf(R)) { R = 0.0; nanCount++; ... }
```

But this is the **master output stage**. By the time we sanitize, NaN has already:
- Propagated through every downstream plugin in the same chain.
- Been multiplied through automation, pan, gain.
- Been summed into bus mixes (poisoning all parallel feeders into that bus).
- Been pushed through the safety limiter's state (compressor envelopes go NaN; ratio coefficients corrupt; bypass-toggle takes seconds to recover).

**The arch audit also flagged this from a DSP-grade angle.** From an RT-safety angle, it's a stability hole.

**Remediation:**
1. **Per-plugin output validation:** after each `plugin->process(...)`, scan the output buffer for NaN/Inf and replace with zero. Cost: ~one pass of cheap SIMD per plugin per block. Acceptable.
2. **Counter:** track which plugin produced the NaN so we can identify hostile plugins.
3. **Bypass policy:** after N NaN events in M blocks, auto-bypass the plugin and notify UI. Avoids permanent corruption from a single broken plugin.

**Effort:** 2–3 days. Highest leverage of the P0 set — single broken plugin shouldn't kill the session.

---

### P0.4 — `windows.h` in public AudioEngine.h (Linux build hazard)

Already documented in `ARCHITECTURE_AUDIT_2026Q2.md` §2.1 but re-flagged here because it's *also* an RT-safety concern:

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h` (header section near the top) includes `windows.h` unconditionally. On Linux this is a no-op (the header is empty/missing in the include path), but it pollutes the macro namespace on Windows (the `min`/`max`/`SendMessage` macro contamination). Headers that use `std::max` or `std::min` inside RT inline functions will produce unexpected expansions.

**Remediation:** wrap in `#ifdef _WIN32` or remove if no longer needed. Standard arch hygiene.

**Effort:** 15 minutes.

---

## 3. P1 — fix before v1 Beta but not blockers

### P1.1 — `MidiBuffer mOut` per-block stack construction (3 sites)

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:364, 396` and `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2901`:

```cpp
MidiBuffer mOut;
u.plugin->process(ins, outs, 2, 2, ctx.numFrames, mIn, &mOut);
// mOut is discarded
```

**Re-verified vs arch audit:** `MidiBuffer` is **stack-allocated** with an inline `m_events[MAX_EVENTS]` array (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/PluginHost.h:330-340`), so this is **not** a heap allocation as the arch audit implied. The cost is the inline array's zero-init + atomic init per construction, multiplied by every Arsenal unit per block.

**Still a problem because:**
1. Cost scales linearly with unit count.
2. The output is **discarded** — plugins that wanted to emit MIDI from these paths (Arsenal route/master) get their MIDI silently dropped.
3. The "should we even be giving them a write target" is an API contract question we never answered.

**Remediation:**
- If MIDI from Arsenal units must be discarded: pass `nullptr` for `midiOutput` (the interface already supports it — `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/PluginHost.h:152`).
- If it must be captured: route into an actual destination buffer that downstream consumers read.
- Either way: don't construct-and-drop.

**Effort:** 1 hour. Arch audit fix is wrong on the heap-alloc claim — correct it in a follow-up edit.

---

### P1.2 — `processBlock` and `renderBlock` are not `noexcept`

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:510` declares `int AudioEngine::processBlock(...)` without `noexcept`. Same for `AudioRenderer::renderBlock` at `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:48`.

This means:
1. The compiler emits unwind tables and landing pads for these functions.
2. Any exception thrown from a call site (plugin, STL, our own code that we forgot was `throw`-capable) can propagate up.
3. We forfeit the optimizer's freedom to inline aggressively across `noexcept` boundaries.

Pairs naturally with P0.2 (plugin exceptions). Adding `noexcept` here forces deterministic `std::terminate` on any escape — which is what we want on the audio thread.

**Remediation:**
1. Mark `AudioEngine::processBlock` and `AudioRenderer::renderBlock` `noexcept`.
2. Mark every helper they call `noexcept`.
3. Build with `-Wnoexcept` to catch silent escape paths.
4. Land alongside P0.2.

**Effort:** 1–2 days (cascading `noexcept` propagation; touches ~30 functions).

---

### P1.3 — Two parallel RT guard systems exist; one is dead

Already covered in arch audit §2.5. Calling it out here for the RT lens:

- **Live:** `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/RealtimeThreadGuard.h` — `ScopedRealtimeAudioThread` + `reportRealtimeMisuse`.
- **Dead:** `@/home/currentsuspect/Dev/Aestra/Source/AudioThreadConstraints.h` — has its own counters/violations API that nothing in the audio path ever invokes.

**Remediation:** delete `AudioThreadConstraints.h` (or fold any unique API surface into `RealtimeThreadGuard.h`). One source of truth.

**Effort:** 30 minutes. Trivial.

---

### P1.4 — `MidiBuffer midiOut; // Unused` in AudioEngine.cpp Arsenal export path

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:2901-2903`:

```cpp
MidiBuffer midiOut; // Unused
unit.plugin->process(inputs, outputs, 2, 2, numFrames, midiIn, &midiOut);
```

Same family as P1.1, also in the Arsenal export path. The `// Unused` comment proves the author knew it was waste. Fix together.

**Effort:** rolled into P1.1.

---

### P1.5 — `processAudio(std::map<UnitID, MidiBuffer*>&)` overload exists alongside `noexcept` variant

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Playback/PatternPlaybackEngine.cpp:271`:
```cpp
void PatternPlaybackEngine::processAudio(uint64_t currentFrame, int bufferSize,
                                         std::map<UnitID, MidiBuffer*>& unitMidiBuffers) {
```
vs the RT-safe variant at line 305:
```cpp
void PatternPlaybackEngine::processAudio(uint64_t currentFrame, int bufferSize, const UnitMidiRoute* routes,
                                         size_t routeCount) noexcept {
```

The map-taking overload is not `noexcept`, takes a non-trivial container, and could throw on construction/lookup. If anything on the RT path calls it, that's a violation. The cleaner overload (with `UnitMidiRoute*` array) is noexcept and clearly designed for RT.

**Remediation:**
1. Verify no RT path calls the map-taking overload (grep callers; if any, fix the caller).
2. Either delete the map-taking overload, or mark it `// Off-RT only` and ideally rename to make the distinction obvious (`processAudioForTest`?).

**Effort:** 2 hours.

---

### P1.6 — `Filter::OversampledBuffer::resize` allocates

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/DSP/Filter.cpp:809-814`:
```cpp
void Filter::OversampledBuffer::resize(uint32_t newSize) {
    if (newSize != size) {
        buffer = std::make_unique<float[]>(newSize);
        AESTRA_MEMORY_ALLOC(newSize * sizeof(float));
        size = newSize;
    }
}
```

Must verify the caller chain never reaches this from `processBlock`. The function name "resize" is honest, but if called transitively from `Filter::process()` via a sample-rate change or block-size change path, that's a violation.

**Remediation:**
1. Walk every caller of `OversampledBuffer::resize` and confirm RT-disjoint.
2. Add an assertion: `assert(!isRealtimeAudioThread())` at entry.
3. If any caller path is RT-reachable, restructure: preallocate at `prepare()` time with the max expected size.

**Effort:** 1 hour to verify; up to 1 day if a violation is found.

---

## 4. P2 — quality-of-life, not blockers

### P2.1 — `EffectChain::setSlotBypassed` pattern is misleading

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/EffectChain.cpp:187-200`:

```cpp
void EffectChain::setSlotBypassed(size_t slotIndex, bool bypassed) {
    if (slotIndex < MAX_SLOTS) {
        m_slots[slotIndex].bypassed.store(bypassed, std::memory_order_release);
        if (!reportRealtimeMisuse("EffectChain::setSlotBypassed")) {
            publishSnapshot();
            if (m_onLatencyChanged) {
                m_onLatencyChanged();
            }
        }
    }
}
```

The atomic store happens **before** the RT check. On RT, the side effect (bypass state change) does take effect, but the snapshot republish and latency callback are skipped. That's actually intentional — the atomic flip is the cheap part — but the API contract is fuzzy and easy to misread.

**Remediation:** comment the rationale at the call site, or split into `setSlotBypassedRT(bool)` and `setSlotBypassedAndPublish(bool)`. Same applies to `setSlotDryWetMix` (line 210).

**Effort:** 30 minutes.

---

### P2.2 — `ScopedRealtimeAudioThread` uses thread-local int, not RAII assertion

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/RealtimeThreadGuard.h:39-46` is an opt-in marker — the caller must remember to construct it. A worker thread used by `RealTimeThreadPool` (for parallel mixer rendering) needs to install one too, otherwise auxiliary RT threads can call non-RT APIs and the guard won't notice.

**Remediation:**
1. Audit every thread that runs RT-priority code (audio callback, RT thread pool workers, low-latency MIDI receive thread if any).
2. Ensure every such thread's entry point constructs `ScopedRealtimeAudioThread`.
3. Add a debug assertion that audio APIs run with `g_realtimeAudioThreadDepth > 0` only when expected.

**Effort:** 4 hours.

---

### P2.3 — `m_inputCallback.load() + cb()` at top of `processBlock` is fully unbounded

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:516-519`:
```cpp
auto cb = m_inputCallback.load(std::memory_order_relaxed);
if (cb) {
    cb(inputBuffer, numFrames, m_inputCallbackData.load(std::memory_order_relaxed));
}
```

The input callback is a function pointer the application installs. There's no guarantee it's RT-safe. If recording-while-monitoring sets this to a callback that writes to disk synchronously, the audio thread will block.

**Remediation:** document the contract that input callbacks **must** be RT-safe; provide a default callback that pushes to an SPSC queue and a non-RT consumer that drains to disk. Already mostly correct in `recording/` code but worth making the contract explicit at the registration site.

**Effort:** 2 hours including doc.

---

## 5. What's already excellent

To balance the criticism — these are doing the right thing and shouldn't change:

| Item | Where |
|------|-------|
| FTZ/DAZ enabled at block entry | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:527` (`DISABLE_DENORMALS` macro) |
| Lock-free transport / fade state | `m_transportPlaying`, `m_fadeState`, etc. all `std::atomic` |
| Lock-free command queue | `applyPendingCommands` at line 557 |
| Buffer config preallocated off-RT | `setBufferConfig` at lines 1208-1300 — every `resize`/`reserve` is off-RT, capped, and documented |
| `EffectChain` snapshot publishing | `std::shared_ptr<const EffectChainSnapshot>` flipped atomically — correct lock-free pattern |
| Master output NaN/Inf scrub | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:933-942` (even if it's only at master — see P0.3) |
| `AudioRenderer.cpp` itself | No locks, no logging, no allocations, no exceptions. Should serve as the model for any new RT code. |
| Test-side RT-violation harness | `@/home/currentsuspect/Dev/Aestra/Tests/AestraAudio/GarbageCollectorTest.cpp:185` and friends — the *tests* prove the guard works; the *engine* just needs to wire it up (P0.1). |

---

## 6. Remediation roadmap

If we're aiming for v1 Beta in December 2026:

| Sprint | Item | Effort | Risk reduction |
|--------|------|--------|----------------|
| **This week** | P0.1 install production RT-violation handler | 1d | Catch RT bugs in the field — single highest-leverage item in this audit |
| **This week** | P0.4 `windows.h` cleanup | 15 min | Free win, do it now |
| **This week** | P1.3 delete `AudioThreadConstraints.h` | 30 min | Free win, removes ambiguity |
| **This week** | P1.1 + P1.4 kill the discarded `MidiBuffer mOut` | 1h | Free win, also slightly faster RT |
| Week 2 | P0.3 per-plugin NaN/Inf scrub + counter | 2-3d | One broken plugin no longer kills the session |
| Week 2 | P1.2 `noexcept` propagation on RT entry points | 1-2d | Locks down compiler behavior; pairs with P0.2 |
| Week 3 | P0.2 plugin exception barrier (`catch (...)` + `noexcept` on `IPluginInstance::process`) | 2d | Linux survival; full sandbox is separate effort |
| Week 3 | P1.5 PatternPlaybackEngine RT-unsafe overload | 2h | Remove a noexcept-trap |
| Week 3 | P1.6 verify `Filter::OversampledBuffer::resize` is non-RT | 1h | Bound the audit, possibly find a hidden hole |
| Week 4 | P2.1, P2.2, P2.3 polish | 1d total | Make the discipline self-documenting |

**Total estimated effort: ~2 weeks of focused work,** parallelizable with PDC v2.

---

## 7. Tests to add

These five tests collectively prove the RT-safety claims and form `RTSafetyRegressionSuite`:

1. **`RtMisuseHandlerInstalledTest`** — boot `AudioEngine`; assert that calling a non-RT API from inside `ScopedRealtimeAudioThread` increments the production misuse counter.
2. **`PluginExceptionDoesNotEscapeTest`** — install a fake plugin whose `process()` throws; render one block; assert the engine survives, the plugin is auto-bypassed, and the breadcrumb buffer recorded it.
3. **`PluginNaNOutputDoesNotPoisonTest`** — install a fake plugin whose `process()` emits NaN; render 64 blocks; assert master output never goes NaN and the plugin is bypassed within N blocks.
4. **`NoExceptOnRtEntryTest`** — `static_assert(noexcept(engine.processBlock(...)))` for both `AudioEngine::processBlock` and `AudioRenderer::renderBlock`.
5. **`RtAllocationCounterTest`** — render 1000 blocks; assert `AESTRA_MEMORY_ALLOC` counter delta is zero across the loop.

Pairs with the "lock the A+" suite in [`Path-to-All-A.md`](../audio/Path-to-All-A.md) §8.

---

## 8. Cross-references

- Arch audit found the dead `AudioThreadConstraints.h` and the discarded `MidiBuffer mOut` — this audit confirms scope (3 sites, not 1) and corrects the heap-alloc claim (it's stack-only).
- PDC v2 design (P3+) introduces `SolvedLatencyTopology` as a new double-buffered RT-published structure — apply the same `noexcept`-on-RT-entry discipline when it lands.
- Plugin Hosting audit (Tier-1 #4, queued) will deep-dive the process-isolation host as the long-term fix for P0.2 and P0.3.
