# Plugin Hosting Audit — 2026Q2

**Status:** Internal — audit findings, no code changed
**Scope:** VST3 + CLAP plugin loading, scanning, lifecycle, RT processing, crash isolation, parameter handling, security model, IPC protocol, watchdog discipline.
**Date:** 2026-05-14
**Auditor:** Cascade
**Companion:** [`AUDIT_RT_Safety_2026Q2.md`](AUDIT_RT_Safety_2026Q2.md), [`AUDIT_Threading_Concurrency_2026Q2.md`](AUDIT_Threading_Concurrency_2026Q2.md), [`AUDIT_Serialization_2026Q2.md`](AUDIT_Serialization_2026Q2.md), [`ARCHITECTURE_AUDIT_2026Q2.md`](ARCHITECTURE_AUDIT_2026Q2.md)

> Plugin hosting is the audit that most surprised me — there is a real out-of-process sandbox already wired up by default. The crash-recovery story is far ahead of what the other audits implied was needed. **But the OOP path has a hard feature gap** (parameter changes don't reach the plugin) that turns crash-resilience into a feature-incomplete experience.

---

## 1. Executive summary

**The architecture is excellent. The implementation has two unshippable gaps and a watchdog inconsistency.**

What's already in place — and it's substantial:

- **Hybrid plugin factory** is the default. Internal plugins run in-process; third-party VST3/CLAP go through the out-of-process helper. `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginManager.cpp:67-69` literally documents the policy: *"Built-ins stay in-process. Third-party native plugin formats are routed through the helper process so a plugin crash does not take down the DAW."*
- **OOP host process** (`AestraPluginHostMain.cpp`) is a real isolated process with an IPC protocol, crash detection, and even a test backdoor (`__aestra_test_crash__` literally calls `std::abort()`).
- **Audio thread is protected.** On crash, `OutOfProcessPluginInstance::process` falls through to passthrough audio (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:387-389`). No stall, no exception, no UB.
- **Plugin scanner security model** has system-trusted paths + user-configured trusted paths, first-load warning for untrusted, out-of-process metadata scan for CLAP. Past tickets (SEC-RTM-005 Parts A and B) prove the team takes this seriously.
- **VST3 in-process path has a real watchdog** with budget violations, auto-bypass at `WATCHDOG_VIOLATION_LIMIT`, and a recovery decay.

What's broken or missing:

- **OOP plugin parameters do not work.** `OutOfProcessPluginInstance::setParameter` is a no-op. Load a third-party VST3, the audio flows, the UI shows knobs — turn them, nothing happens.
- **CLAP MIDI is unimplemented.** `process.in_events = nullptr; process.out_events = nullptr` at `CLAPHost.cpp:358-359` with explicit comment "events are silently dropped". CLAP synths cannot receive notes.
- **`PluginManager::createInstance` blocks the calling thread on `future.wait()`** — code comment admits this hangs the UI for OOP plugins.
- **Watchdog coverage is asymmetric.** VST3 (in-process) has a real one. OOP has crash detection but no time-budget watchdog. CLAP has stubs that always return false. Internal plugins have stubs (correct — they're trusted).

**Findings:** 3 P0, 5 P1, 4 P2. Net judgment: of all four Tier-1 audits, this is the one with the *strongest architecture* and the *most immediately user-visible gaps* in the implementation.

---

## 2. P0 — must fix before v1 Beta

### P0.1 — Out-of-process plugin parameters are a no-op

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:437-440`:

```cpp
void OutOfProcessPluginInstance::setParameter(uint32_t id, float value) {
    (void)id;
    (void)value;
}
```

**This is the entire implementation.** It silently discards every parameter change. Because `HybridPluginFactory` routes all third-party VST3 and CLAP through OOP by default (`PluginManager.cpp:69`), this means:

1. User loads a third-party Serum, Phase Plant, FabFilter, etc.
2. Plugin loads. Audio passes through correctly.
3. User turns any knob, automates any parameter, loads any preset.
4. **Nothing reaches the plugin.** The audio output is fixed at whatever the plugin's default state was at load.
5. The plugin GUI may show the knob moving (if the plugin's GUI reads its own internal state, which it doesn't via setParameter), but the *audible* output is frozen.

For automation, preset loading, MIDI CC mapping, and basic knob-twiddling — none of it works.

**Equally broken** are `getParameter` (returns 0.0f via `(void)id;` at line 434), and the parameter-list query (need to verify but likely similarly stubbed). So even reading current values from the host's UI is broken.

This is **unshippable for v1 Beta** — the DAW would functionally be incompatible with every third-party plugin's most basic feature.

**Remediation:**
1. Define an IPC command set: `SETPARAM <id> <hex-encoded-float>`, `GETPARAM <id> -> OK <hex-encoded-float>`, `LISTPARAMS -> OK <count> <id> <flags> <name-hex>...`.
2. Implement in `OutOfProcessPluginInstance::setParameter`: hex-encode, `sendCommand`, mark crash on failure.
3. Implement on the helper side in `AestraPluginHostMain.cpp`: route to VST3 `IEditController::setParamNormalized` or CLAP `clap_plugin_params->flush`.
4. Parameter changes from RT path: cannot block on IPC. Use the existing worker-thread async pattern — RT writes to an SPSC queue of `{id, value}`, the worker drains and ships them to the helper.
5. Test against a known third-party plugin (Surge XT, Vital, anything free + popular) — load, change a parameter, render, verify audible output matches.

**Effort:** 3-5 days. The IPC plumbing exists; this is parameter-handling logic on both sides. Highest single-task ROI in this audit because it converts the OOP path from *broken-but-stable* to *production-ready*.

---

### P0.2 — CLAP MIDI input is not implemented

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/CLAPHost.cpp:333-359`:

```cpp
(void)midiInput;
(void)midiOutput;
// ...
process.in_events = nullptr; // CLAP MIDI not implemented — events are silently dropped.
process.out_events = nullptr;
```

And explicitly warned at load (`CLAPHost.cpp:249`):
> `Log::warning("CLAP support is experimental. MIDI input, plugin state save/load, and host callbacks are not fully implemented for this plugin.");`

But the scanner happily exposes CLAP plugins to users (`PluginScanner::scanCLAPPlugin`), so they'll appear in the plugin browser. A user loading a CLAP synth gets a silent plugin and a one-line log warning they probably won't see.

For v1 Beta — even one whose Mission is "produce music" — **CLAP instrument support being silent-on-load is a beta-blocker**.

**Remediation options** (ranked by my preference):

A. **Implement CLAP MIDI** properly. Translate `MidiBuffer` events to `clap_event_t` + `clap_input_events`. This is mechanical: CLAP has a clean event API.

B. **Hide CLAP from the user-facing plugin browser** until A is done. Mark `CLAP support coming v1.0` in UI. Continues to load CLAP for internal testing.

C. **Defer CLAP support to v1.0** entirely — VST3 + Internal only at Beta. Cleanest if CLAP is a low-priority format.

If the answer is A or B (i.e., CLAP ships at Beta), this is P0. If the answer is C (delete the scanner code that's exposing it), this is P2 housekeeping. Need a product call.

**Effort:** A = 3-5 days; B = 2 hours; C = 1 day for the cleanup.

---

### P0.3 — `PluginManager::createInstance` blocks the calling thread

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginManager.cpp:161-185`:

```cpp
PluginInstancePtr PluginManager::createInstance(const PluginInfo& info) {
    // ...
    std::promise<PluginInstancePtr> promise;
    auto future = promise.get_future();
    m_factory->createPluginAsync(info, [&](PluginInstancePtr instance) {
        promise.set_value(instance);
    });
    // Block until complete (In-Process factory is synchronous anyway, so this is safe)
    // For true async factories, this would hang the UI, which is why we must migrate consumers.
    // ...
```

For OOP plugins, this involves: spawning the helper process, IPC handshake, helper loading the plugin DLL/bundle, the plugin's own initialization (some VST3 plugins do significant work in `IComponent::initialize`), plus the round-trip back.

In the UI thread, this is a **multi-second hang** for any heavy plugin (Kontakt, Omnisphere, Diva, Pigments). And it's worse with cold caches.

The code comment acknowledges the problem:
> "For true async factories, this would hang the UI, which is why we must migrate consumers."

**Beta-blocker because** users will judge load-time responsiveness in the first 30 seconds of using Aestra. Hanging on every plugin instantiation poisons the impression.

**Remediation:**
1. Audit every call to `PluginManager::createInstance` in `Source/` and `AestraAudio/` and convert to `createInstanceAsync` (already exists at line 195). Most callers are UI controllers that can show a spinner and accept a callback.
2. Replace the blocking `future.wait()` with a UI-thread message-pump-friendly variant. The internal-plugins-only test paths can keep the synchronous version.
3. Add a "Loading: <plugin name>" UI affordance with an X to cancel.

**Effort:** 2-3 days. The async API exists; the work is migrating callers.

---

## 3. P1 — fix before v1 Beta but not blockers

### P1.1 — Watchdog coverage is asymmetric across plugin formats

| Format | Watchdog implementation | Notes |
|--------|-------------------------|-------|
| Internal (AestraComp, AestraVerb, AestraEQ, AestraDelay, Sampler) | Stub — returns false/zero | Correct: trusted code, no watchdog needed |
| VST3 (in-process — currently the test path) | Real (`VST3Host.cpp:489-504`) — budget violation count, auto-bypass at `WATCHDOG_VIOLATION_LIMIT`, decay recovery | Production-quality |
| VST3 (out-of-process — default in `HybridPluginFactory`) | Crash detection only (`m_crashed`, `markCrashed`). No time-budget watchdog | Gap: a CPU-pegging plugin will stall the helper process for up to 500ms (the `sendCommand` deadline at line 478) per block |
| CLAP (in-process) | Stub — always returns false (`CLAPHost.h:92-95`) | Gap: no protection at all |
| OOP CLAP | Inherits crash detection from OOP path | Same gap as VST3 OOP |

**Specific problems:**
1. A CLAP plugin that does 10ms of work in `process()` at 1ms block period silently kills CPU; no detection, no bypass.
2. An OOP plugin that hangs (deadlock, infinite loop) stalls the helper. The audio thread falls through to passthrough after the 500ms IPC timeout, **but during those 500ms the audio thread is blocked.** That's 500ms of dropouts.

**Remediation:**
1. **Lower the IPC timeout** from 500ms to something like 5ms (≈2 buffer periods at 48kHz/256). On timeout: mark crashed, immediately passthrough. Hangs degrade gracefully rather than catastrophically.
2. **Implement the time-budget watchdog for CLAP** — the same pattern as VST3 (`VST3Host.cpp:489-504`).
3. **Surface watchdog state in UI** — a per-plugin badge ("auto-bypassed by watchdog; click to reset") so users know what happened.

**Effort:** 2 days.

---

### P1.2 — IPC protocol uses hex-encoded payloads; performance is uncharacterized

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:547-548`:
```cpp
command << "PROCESS " << channels << " " << frames << " "
        << hexEncodeBytes(input.data(), sampleCount * sizeof(float));
```

Each block:
- Hex-encode input → 2x byte count.
- Write to pipe.
- Helper reads, hex-decodes, runs `plugin->process`, hex-encodes output.
- Parent reads response, hex-decodes, memcpy to output buffer.

At 48kHz / 128 frames / stereo: 1 KB samples → 2 KB hex → roughly 4 KB round-trip data per block at ~2.67ms block period. Times every OOP plugin in the project.

**Concerns:**
1. Hex encoding doubles payload (vs binary). Pipe is local but syscall overhead scales with payload.
2. No measurement exists. May be fine in practice, may not.
3. Newer protocols (shared memory + signaling pipe) would be ~100x faster.

**Remediation (incremental):**
1. **Benchmark first.** Run a stress test: 16 OOP plugin instances, render 5 minutes, measure CPU on the audio thread *and* the helper process. If audio thread is < 30% busy, defer the rewrite.
2. **Switch to binary IPC** (raw bytes, length-prefixed) — eliminates hex-encode CPU + halves IO. Compatible with current pipe transport.
3. **Long-term:** shared memory ring buffer for the audio payload; pipe used only for control. Standard pro-audio host pattern.

**Effort:** Benchmark = 1 day. Binary encoding = 2 days. Shared memory = 1-2 weeks. Defer the last if benchmark shows headroom.

---

### P1.3 — No process-startup timeout in `OutOfProcessPluginInstance::load()`

`OutOfProcessPluginInstance::load()` at line 298 spawns the helper, sends `LOAD`, waits for response — but the only timeout is the per-IPC `readLine(..., 500ms)` (line 478 inside `sendCommand`). If the helper hangs *between* IPC steps (e.g. plugin DLL `dlopen` takes 30s for Kontakt-class plugins), there's no overall load deadline. The UI thread is stuck on `createInstance` (which is P0.3) the whole time.

**Remediation:**
1. Add a wall-clock deadline for the entire load operation (e.g. 30s, configurable via settings for users with slow plugins).
2. On timeout: kill helper, mark plugin instance as failed-to-load, return nullptr.
3. User-facing: "Plugin took too long to load; it may need to be added to a denylist or your system has high I/O load. Try again?"

**Effort:** half a day.

---

### P1.4 — Plugin scan cache invalidation strategy is fragile

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginScanner.cpp:538` saves a scan cache. `loadScanCache` at line 594 restores it. `isCacheModified` at line 705 checks file last-write-time.

This catches the case where the user upgrades a plugin and the DLL is newer. But:
1. Plugin manifest data (channel counts, parameter count, vendor metadata) can change without the DLL being touched if the plugin reads from a sidecar `.json` or registry. Cache won't invalidate.
2. The cache is per-platform-path. If a user adds a custom plugin path, system-wide cache wasn't aware of it; first scan after path-add will re-scan everything.
3. No version stamp on the cache schema itself — if `PluginInfo`'s serialized representation changes between Aestra versions, the old cache is misread silently.

**Remediation:**
1. Add a cache schema version (`kCacheSchemaVersion = 1`) — bump on PluginInfo changes; mismatch = invalidate.
2. Treat the user-trusted-paths file as part of the cache key — when it changes, rescan affected paths.
3. Document the cache-invalidation algorithm in a header comment so future agents don't accidentally break it.

**Effort:** half a day.

---

### P1.5 — VST3 SEH protection is Windows-only; Linux/macOS rely on OOP crash recovery alone

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/VST3Host.cpp:63-72` wraps `processor->process(data)` in `__try`/`__except` on Windows. On Linux/macOS:
- In-process VST3: a SIGSEGV in the plugin kills Aestra. No recovery.
- Out-of-process VST3: SIGSEGV kills the helper; `markCrashed` + passthrough recover. Good.

Since the default path is OOP, this is fine *for the default config*. But:
1. In-process VST3 still exists as a configurable path (or test path).
2. `AestraPluginHostMain` itself could be hardened with a signal handler that catches SIGSEGV in the *helper's* audio loop, logs the offending plugin ID + a minidump-style breadcrumb, then exits cleanly — so the parent can report "Plugin X crashed at offset Y" instead of just "helper died."

**Remediation:**
1. Add a `sigaction(SIGSEGV/SIGBUS/SIGFPE)` handler in `AestraPluginHostMain.cpp` that writes a crash record to stderr (which the parent reads) before re-raising the signal.
2. Document that in-process VST3 is unsupported on Linux/macOS post-Beta — force-route to OOP.

**Effort:** 1 day.

---

## 4. P2 — quality-of-life, not blockers

### P2.1 — CLAP host callbacks `hostRequestProcess` / `hostRequestRestart` are TODO stubs

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/CLAPHost.cpp:28-33`:
```cpp
void hostRequestProcess(const clap_host* /*host*/) {
    // TODO: Handle process request
}
```

CLAP plugins use these to request the host wake them when sleeping. Without it, plugins that depend on host-driven wakeup (rare but legal per the spec) won't function.

**Remediation:** wire to AudioEngine's process scheduling. Defer if CLAP is deferred (see P0.2 option C).

**Effort:** 1 day if implementing; 0 if CLAP is deferred.

---

### P2.2 — CLAP plugin state save/load not implemented

Per the warning at `CLAPHost.cpp:249`. CLAP plugins reload to default state every time the project opens — *all parameter values are lost*.

Combined with P0.1 (OOP setParameter no-op), this means: even if OOP parameters worked, reloading a project with a CLAP plugin would still reset the parameter values to defaults because the state isn't persisted.

**Remediation:** implement `clap_plugin_state->save` / `load` plumbing. Defer with CLAP if deferred.

**Effort:** 1-2 days.

---

### P2.3 — `AestraPluginHostMain.cpp` has a test backdoor in production code

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/AestraPluginHostMain.cpp:560-562`:
```cpp
if (id == "__aestra_test_crash__") {
    std::abort();
}
```

This is a deliberate crash trigger keyed off a magic plugin ID. Useful for testing the crash-recovery path, but it's compiled into every release build. If anyone discovers the magic string, they can crash any user's plugin host helper at will (limited blast radius — only the helper, not Aestra itself, and only if they can get the user to load a plugin with that ID).

**Remediation:**
1. Gate behind `#ifdef AESTRA_ENABLE_TEST_HOOKS` (defined only for test builds).
2. Or accept the (minor) risk and document it — the blast radius is genuinely small.

**Effort:** 15 minutes.

---

### P2.4 — `PluginScanner` has try-catch-with-empty-handlers in several places

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginScanner.cpp:394-396`:
```cpp
} catch (...) {
    success = false;
}
```

Similar at lines 589, 688, 711, 866. These swallow all exceptions without logging the type. When a scan fails on a specific plugin, the user gets "scan finished" with no signal of what was skipped.

**Remediation:** capture `std::current_exception()` and log `e.what()` from the typed `std::exception&` catch where possible; for `catch (...)` add a "[unknown exception during scan of <path>]" log line.

**Effort:** 2 hours.

---

## 5. What's already excellent

This is the bulk of what makes me confident the architecture is correct.

| Item | Where |
|------|-------|
| OOP host as default for third-party plugins | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginManager.cpp:67-69` — explicit policy decision |
| `HybridPluginFactory` keeps Internal in-process for performance | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginFactory.cpp:107-114` |
| RT-safe crash fallthrough to passthrough | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:387-389` — no exceptions, no allocations, no waiting on RT path |
| VST3 SEH wrapper on Windows isolates SEH from C++ destructors (C2712 fix) | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/VST3Host.cpp:62-72` — the comment about C2712 proves the team has wrestled with this and won |
| Real watchdog for in-process VST3 with budget tracking + recovery decay | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/VST3Host.cpp:489-504` |
| Test backdoor (`__aestra_test_crash__`) enables crash-path testing | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/AestraPluginHostMain.cpp:560` — note this is also P2.3, but as a testing tool it's gold |
| Trusted-path security model with system + user-configured paths | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginScanner.cpp:879-918` |
| Out-of-process CLAP metadata scan | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginScanner.cpp:822-824` — even scanning is isolated |
| First-load warning + remembered untrusted plugins | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/PluginScanner.cpp:742-766` |
| Worker-thread async IPC architecture (no IPC on the RT thread) | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:504-529` |
| `m_ipcMutex` serializes pipe access correctly | `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Plugin/OutOfProcessPluginInstance.cpp:470` |
| Red-team PoCs for plugin-related crashes | `tests/redteam/poc_unitmanager_stoul.py` (SEC-RTM-003) |
| Async API exists (`createInstanceAsync` at PluginManager.cpp:195) — consumers just haven't migrated | Same file |

---

## 6. Remediation roadmap

| Sprint | Item | Effort | Risk reduction |
|--------|------|--------|----------------|
| **This week** | P0.1 OOP parameter passthrough (IPC + helper-side wiring) | 3-5d | Unblocks third-party plugin viability — single highest-impact item |
| **This week** | P0.2 CLAP MIDI decision (implement / hide / defer) | 1h decision + 0-5d implementation | Removes silent-instrument trap |
| Week 2 | P0.3 migrate `createInstance` callers to async | 2-3d | UI freezes vanish |
| Week 2 | P1.5 SIGSEGV handler in helper for crash diagnostics | 1d | Crash reports become actionable |
| Week 3 | P1.1 watchdog parity (CLAP + OOP time-budget) | 2d | One bad plugin can't tank session |
| Week 3 | P1.3 process-startup wall-clock timeout | 0.5d | No indefinite hangs |
| Week 4 | P1.2 benchmark + (optionally) binary IPC | 1d benchmark, 2d binary | Confirms OOP CPU cost is acceptable |
| Week 4 | P1.4 cache schema versioning | 0.5d | Survives plugin DB changes |
| Week 4 | P2.x polish (test-hook gating, scan logging) | 0.5d | Reduces operational ambiguity |

**Total: 2-3 weeks of focused work.** Some of this is parallelizable with PDC v2 (the OOP/CLAP work doesn't touch the RT engine code) but P0.3 and P1.1 both touch RT-adjacent code and should be coordinated with PDC v2's RT-path changes.

---

## 7. Tests to add

Forms `PluginHostingRegressionSuite`:

1. **`OopParameterRoundtripTest`** — load a fixture VST3 (or a stub helper plugin), set 10 parameter values, read them back, assert all returned correctly (post-P0.1).
2. **`OopParameterAutomatedRenderTest`** — automate a parameter from 0→1 over 1 second, render to WAV, assert the output amplitude rises monotonically (audible proof that parameter changes reach the plugin).
3. **`ClapMidiNoteInputTest`** — load a CLAP synth (or stub), send a single note-on, render, assert non-silent output (post-P0.2 A or C).
4. **`AsyncCreateInstanceNoBlockTest`** — call `createInstanceAsync` from the UI thread; assert the call returns within 1ms (post-P0.3).
5. **`OopCrashRecoveryTest`** — load the `__aestra_test_crash__` plugin, call process; assert the proxy is marked crashed, audio passes through, no exception escapes (validates existing P2.3 backdoor while we have it).
6. **`OopWatchdogStallRecoveryTest`** — load a stub plugin that sleeps for 10s in process; assert IPC times out within 5ms, passthrough engages, watchdog auto-bypass fires (post-P1.1).
7. **`ScanCacheVersionMismatchTest`** — write a cache file with `kCacheSchemaVersion - 1`, attempt to load, assert the loader rejects it cleanly and triggers a re-scan (post-P1.4).
8. **`UntrustedPluginRequiresWarningTest`** — point scanner at a directory in /tmp, run; assert the first-load warning callback fires.

---

## 8. Open questions

1. **What's the product policy for CLAP at Beta?** This decision is the difference between P0.2-A (implement everything), P0.2-B (hide from UI), or P0.2-C (delete the path). Cannot make it from the codebase alone.
2. **What's the project-load contract for missing plugins?** If a project references a VST3 that's not installed on this machine, what does load do today? (Suspect: passthrough proxy is created; verify and document.)
3. **What's the recovery story for "user installed a new plugin version, project references old version's parameters by ID"?** VST3 parameter IDs are supposed to be stable, but plugins do change them. Worth a doc.
4. **Where does `setBypass` live for OOP plugins?** Saw `setParameter` is a no-op; need to verify bypass works. If it doesn't, users can't even turn off a misbehaving plugin without removing it from the chain — workable but ugly.
5. **Does the helper process get killed on Aestra crash?** If Aestra hard-crashes (SIGSEGV in the main process), do helper processes get orphaned? If yes, they're zombie processes consuming RAM until killed manually. Standard solution: `prctl(PR_SET_PDEATHSIG)` on Linux.

---

## 9. Cross-references

- **RT-safety audit P0.2** ("plugin exceptions unhandled on Linux") is **largely solved** by the OOP architecture. RT-safety audit underestimated this. Should be downgraded to P1 acknowledging that *in-process* plugins still have the exception risk but OOP plugins (the default) don't.
- **Threading audit P0.1** (deprecated `std::atomic_load(&shared_ptr)`) doesn't appear in plugin hosting code — the OOP path uses raw atomics and `m_ipcMutex` correctly.
- **Serialization audit P0.1** (untested migration framework) is adjacent — when plugin parameter IDs change between plugin versions, project-format migration may need to map old IDs to new. Plugin parameter migration is a separate concern from project schema migration.

---

## 10. Bottom line

The plugin hosting *architecture* is the best-designed subsystem I audited. The team made the hard call to default to out-of-process; that decision pays off in crash isolation, RT-safety, and security model. The implementation gaps (P0.1, P0.2, P0.3) are scope problems, not design problems. Fixing them is mechanical work, not architectural redesign.

If I had to make one bet about v1 Beta: **fix P0.1 first.** It's the most user-visible gap and converts the entire third-party plugin experience from "load works, audio works, knobs don't" to "load works, audio works, knobs work." That single change is the difference between Aestra being usable for real production work in Beta or not.
