# Aestra Architectural Audit — 2026 Q2

**Scope:** Cross-module layering, real-time audio path, project serialization, UI/rendering.
**Mode:** Read-only inspection. No code changes. All findings are evidence-cited with file/line refs.
**Auditor:** Cascade (AI pair) per AGENTS.md §1–§3.

> Severity legend: **P0** = correctness/safety risk or strategic blocker. **P1** = significant architectural smell with concrete cost. **P2** = code-health / maintenance.

---

## 0. Executive summary

Aestra's module layout is essentially correct (Core → Plat → Audio → UI → App) and the real-time audio path uses the right primitives (SPSC command queue, atomic snapshots, `RealtimeThreadGuard`, double-buffered shared snapshots retired via `GarbageCollector`). However, three structural issues are eroding the discipline the module shape promises:

1. **Two parallel "audio-thread guard" mechanisms exist** and the one documented in UI-facing code (`Source/AudioThreadConstraints.h`) is **never armed in the actual RT callback**, so its violation counters are dead.
2. **`AestraAudio` publicly links `AestraLicense`**, dragging licensing into the supposed audio-core layer and contradicting `aestra-core/AestraCoreMode` semantics.
3. **`AudioEngine` is a 3.2 kLOC singleton with broad `friend` access** from renderer/exporter, and `AudioEngine::getInstance()` silently constructs a *second* fallback engine when the real one is not yet created — a footgun for split state.

Project persistence is single-file (`Source/Core/ProjectSerializer.cpp`, ~1.5 kLOC) at schema `v1` with the migration framework wired but empty — fine *today*, but the schema is mature enough (Arsenal/automation/patterns/clips) that the first breaking change will be painful unless a roundtrip test set is established now.

UI carries three obvious monolith hotspots (`TrackManagerUI.cpp` 5.4 kLOC, `FileBrowser.cpp` 4.2 kLOC, `AestraContent.cpp` 3.5 kLOC) that mix view + state + dispatch and will resist refactoring further the longer they grow.

---

## 1. Cross-module layering & build posture

### 1.1 [P0] `AestraAudio` publicly links `AestraLicense`

`@/home/currentsuspect/Dev/Aestra/AestraAudio/CMakeLists.txt:480-482`
```cmake
if(TARGET AestraLicense)
    target_link_libraries(AestraAudioCore PUBLIC AestraLicense)
endif()
```

Audio engine is supposed to be the platform-agnostic DSP/transport layer. Linking `AestraLicense` PUBLIC means every downstream consumer (tests, headless, plugins, UI) transitively inherits license dependencies — including the supposedly license-free `AestraHeadless` and lab harnesses. Per `AGENTS.md §2`: *"Do not make public/core builds depend on premium/private modules."*

**Recommendation:** Move the license linkage to `Source/` (the app target) only. If audio code legitimately needs a gating check, expose a `LicenseGate` interface in `AestraCore` and have the app inject an implementation.

---

### 1.2 [P0] Duplicate / dead RT-thread guard infrastructure

Two parallel mechanisms exist for marking the audio thread:

- `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/RealtimeThreadGuard.h:12-46` — `g_realtimeAudioThreadDepth`, `ScopedRealtimeAudioThread`, `reportRealtimeMisuse`. **Actually used** in `AudioEngine::processBlock` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:510`) and gating checks like `setMeterSnapshots`, `setContinuousParams`, `setChannelSlotMap` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:322,337,352`).
- `@/home/currentsuspect/Dev/Aestra/Source/AudioThreadConstraints.h:98-122` — `g_isAudioThread`, `AudioThreadGuard`, `AESTRA_ASSERT_AUDIO_THREAD`, `AESTRA_TRACK_ALLOCATION`, `AudioThreadStats`. **Never armed**: the only references in the workspace are the header itself, one include in `Source/Core/AestraAudioController.cpp`, and docs. No `AudioThreadGuard` constructor is invoked in the RT callback, so `g_isAudioThread` is always `false` and every `AESTRA_TRACK_*` macro is a no-op or always-false branch.

This is worse than redundant: the `AudioThreadStats` counters look authoritative from the outside (HUDs/tests may consult them), but they have nothing populating them.

**Recommendation:** Pick one. Either:
- Delete `Source/AudioThreadConstraints.h` and forward its public macros to `RealtimeThreadGuard.h`, or
- Move `AudioThreadConstraints.h` into `AestraAudio/include/Core/`, arm it inside `processBlock`, and decommission `RealtimeThreadGuard.h`.

The first option is smaller. Either way, do not ship both.

---

### 1.3 [P1] Cross-layer header reach via relative paths

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:4-5`
```cpp
#include "../../AestraCore/include/AestraLog.h"
#include "../../AestraCore/include/AestraMath.h"
```

Also `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:4`. The CMake target already exposes `AestraCore` via `PRIVATE` include directories (`@/home/currentsuspect/Dev/Aestra/AestraAudio/CMakeLists.txt:437`), so these `../../AestraCore/include/...` paths are bypassing the configured include layer. They will silently break the moment the directory structure is reorganized or the file is moved.

**Recommendation:** Replace with `#include "AestraLog.h"` / `#include "AestraMath.h"`; the CMake include dirs already make this work.

---

### 1.4 [P1] Windows header leak into a public audio header

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:23-26`
```cpp
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // ALLOW_PLATFORM_INCLUDE
#endif
```

`AudioEngine.h` is a public header included by `Source/`, tests, and lab harnesses. Pulling `windows.h` here pollutes every TU on Windows with `min`/`max`/`SendMessage`/`GetObject` macros and adds large compile-time cost. The `ALLOW_PLATFORM_INCLUDE` marker suggests this was a conscious exception, but the engine class itself does not need any Win32 symbols at the header level (RT callback is signature-portable).

**Recommendation:** Move the Win32 include into the `.cpp`. If a public type genuinely requires a Win32 handle, wrap it in a forward-declared opaque pointer.

---

### 1.5 [P1] Global warning suppression list hides real issues

`@/home/currentsuspect/Dev/Aestra/CMakeLists.txt:110-123`
```cmake
add_compile_options(
    -Wno-sequence-point
    -Wno-pedantic
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-maybe-uninitialized
    -Wno-switch
    -Wno-format-truncation
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
)
```

`-Wno-maybe-uninitialized` and `-Wno-sequence-point` in particular silence two of the most actionable diagnostics gcc emits. Applied globally (after `add_compile_options(-Wall -Wextra -Wpedantic)`), they cancel the strictness the previous line claims. Per `AGENTS.md §2`: *"Do not silence CodeRabbit, clang-tidy, compiler, sanitizer, or CI findings without a clear technical reason."*

**Recommendation:** Move suppressions per-target (e.g., only on the `RtAudio.cpp`, `vst3sdk`, `miniaudio.h`, `FreeType` TUs that genuinely need them) and re-enable the warnings everywhere else.

---

### 1.6 [P2] Header duplication / dead entry in `AESTRA_AUDIO_CORE_HEADERS`

`@/home/currentsuspect/Dev/Aestra/AestraAudio/CMakeLists.txt:370-372`
```cmake
src/DummyAudioDriver.h

src/DummyAudioDriver.h
```
Listed twice. Harmless but indicates the header list is hand-curated and drifting.

`@/home/currentsuspect/Dev/Aestra/AestraAudio/CMakeLists.txt:180,327-328` also carries commented-out `SelectionModel.cpp` / `SelectionModel.h` with `TODO` markers. These are dead entries — either restore behind a `# TODO` issue link or delete.

---

### 1.7 [P2] Singletons in audio core

Three `getInstance()` singletons in the audio library:

- `AudioEngine::getInstance()` — `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:82-90` (with **fallback static instance** if `g_audioEngineInstance == nullptr`).
- `PluginManager` — `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/PluginManager.h` (refs).
- `SamplePool` — `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/IO/SamplePool.h` (refs).

The `AudioEngine` fallback in particular is dangerous:

```cpp
if (!g_audioEngineInstance) {
    static AudioEngine fallback; // Emergency fallback
    return fallback;
}
```

Any caller racing app startup or running under a test harness gets a second, *parallel* engine. Atomic snapshots, sampler caches, command queues — all split between the real and fallback instances. This will manifest as "commands dropped on the floor" or "meters frozen" bugs that are nearly impossible to reproduce.

**Recommendation:** Replace the fallback with a hard assert in debug + nullable return in release, or push the engine into a `ServiceLocator` (which `Source/App/ServiceLocator.h` already hints at). At minimum, log once with a stack identity tag so duplicate-engine bugs are visible.

---

## 2. Real-time audio path

### 2.1 Positive baseline

- `processBlock` is entered under `ScopedRealtimeAudioThread` and all "setter" APIs that touch shared state guard with `reportRealtimeMisuse(...)` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:322,337,352,416,449`).
- Command intake is bounded ("max 16 commands per block", `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:135-150`), edges are coalesced cleanly, and unrouted commands (`LoadProjectState`, `UpdateClipState`, `StartPreview`, `StopPreview`) are explicitly dropped with a comment citing AGENTS.md (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:231-237`).
- Shared snapshot publication uses atomic shared-ptr swap + `GarbageCollector::release()` for deferred destruction (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:328-379`). This is the right pattern.
- No mutexes are taken in any audio-side `.cpp` on the RT path. Grep confirms mutex usage is limited to `CommandHistory` (UI), `OutOfProcessPluginInstance::sendCommand` (IPC), `SessionLog` (logging), and `InternalPluginRegistry` (registration) — none of which `processBlock` reaches.

### 2.2 [P1] `MidiBuffer mOut` constructed per-unit per-block on the RT thread

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:364`
```cpp
MidiBuffer mIn = ... ;
MidiBuffer mOut;                      // ← stack-constructed every iteration
u.plugin->process(ins, outs, 2, 2, ctx.numFrames, mIn, &mOut);
```

Same pattern in `processArsenalUnits` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:396`). Whether this is RT-safe depends entirely on `MidiBuffer`'s default constructor. If `MidiBuffer` reserves any internal storage (`std::vector<MidiEvent>` is the typical case), this is an unbounded heap allocation on every render iteration. The discarded result `mOut` is also a smell: produced MIDI is silently dropped, so the allocation is pure cost.

**Recommendation:** Verify `MidiBuffer`'s ctor allocates nothing, and if it does, hoist a per-unit reusable `MidiBuffer` into `m_unitMidiBuffers`-style preallocated storage. If `mOut` is never consumed, pass `nullptr` instead.

### 2.3 [P1] `ArsenalProcessingContext` is constructed per-block on the RT thread

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:326, 351, 382` — `ArsenalProcessingContext arsenal(...)` constructed three times per block in the renderer. Each then calls `arsenal.getSnapshot()` which calls `UnitManager::getAudioSnapshot()` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Models/UnitManager.cpp:239-242`):
```cpp
auto snapshot = std::atomic_load(&m_publishedSnapshot);
return std::const_pointer_cast<const AudioArsenalSnapshot>(snapshot);
```

`std::atomic_load` on `std::shared_ptr` is **deprecated since C++20 and removed in C++26** (`AGENTS.md §6` already pins C++17 — OK for now, but a blocker for any toolchain upgrade). More immediately, each call returns a fresh `shared_ptr` copy, which atomically refcounts. Three calls × N tracks/units per block adds avoidable refcount churn on the RT thread.

**Recommendation:**
- Hold a single `shared_ptr` snapshot captured once at the top of `processBlock` and pass it through `Context` to all renderer entry points.
- Migrate `std::atomic_load(&shared_ptr)` to `std::atomic<std::shared_ptr<T>>` (C++20) when the toolchain moves, or to a hand-rolled hazard-pointer / SMR scheme.

### 2.4 [P1] `performNonRealtimeMaintenance` calls `refillWindow` on the audio thread? Check

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:415-446` — `performNonRealtimeMaintenance` is *correctly* guarded by `reportRealtimeMisuse(...)` at entry (line 416) and only runs on the main loop. **Good.** But `AudioRenderer::processArsenalMidi` calls `pe->refillWindow(...)` from inside the RT path (`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/AudioRenderer.cpp:343`). If `refillWindow` allocates or locks (typical for lookahead refill), that's an RT violation. Worth verifying — same code path runs in `performNonRealtimeMaintenance` (line 427) which is RT-forbidden, suggesting at least *some* of its work is not RT-safe.

**Recommendation:** Audit `PatternPlaybackEngine::refillWindow` for allocations/locks; if non-trivial, only the non-RT call site should invoke it and the RT-side `processAudio` should consume from a preallocated ring.

### 2.5 [P2] `AudioEngine` is a god-class

`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h` (1037 lines, 46 KB) and `@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp` (3221 lines, 144 KB). Two `friend class` declarations open private state to `AudioRenderer` and `AudioExporter` (`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioEngine.h:62-63`).

The "hybrid engine transition" comment is honest, but it has been the steady state long enough to be load-bearing. The friend coupling makes the `AudioRenderer` extraction half-done: it pretends to be a separate class while still requiring full read/write to `AudioEngine` internals.

**Recommendation:** Define a `RenderContext` POD that `AudioEngine::processBlock` populates and passes to `AudioRenderer` by value/const-ref. Drop `friend`. This is a multi-PR refactor — track it explicitly.

### 2.6 [P2] Denormal protection is per-block enable/disable

`@/home/currentsuspect/Dev/Aestra/AestraAudio/src/Core/AudioEngine.cpp:31-41` defines `DISABLE_DENORMALS` / `RESTORE_DENORMALS` and applies them around `processBlock`. This is correct but per-block MXCSR reads/writes have measurable cost on some CPUs. Most pro audio engines instead require the OS to deliver the callback with FTZ/DAZ already set (or set it once per thread on the first callback).

**Recommendation:** Set FTZ/DAZ once when entering the RT thread (driver layer) and assert thereafter; remove the per-block save/restore.

---

## 3. Project serialization & persistence

### 3.1 Positive baseline

- `Source/Core/ProjectSerializer.cpp` enforces explicit size caps for every section (`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:37-53`) — clips, lanes, sources, patterns, notes, automation points, hex effect-state. This is one of the strongest pieces of code in the repo for input-side hardening.
- `writeAtomically(path, contents)` exists (`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.h:104`) and `bytesToHex`/`hexToBytes` round-trip is bounded.
- `JSON` parser is hardened against pathological numeric tokens (`hasUnsafeNumericToken`, `@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:103-161`).
- Schema versioning + migration framework is in place (`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectMigrations.h`).

### 3.2 [P0] Migration framework is wired but empty; no `MIN_SUPPORTED < CURRENT` plan

`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:37-38`
```cpp
constexpr int PROJECT_VERSION_CURRENT = 1;
constexpr int PROJECT_VERSION_MIN_SUPPORTED = 1;
```

`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectMigrations.h:39-48` registers zero migrations. Per `AGENTS.md §12`: *"Project persistence is high-risk … add migration/default handling for new fields."*

The first breaking change (e.g. ArsenalUnit route-mode rename, AutomationCurve schema, MultiClipPlaylist refactor) lands without:
- A regression corpus of v1 project files.
- A roundtrip test that loads v1 → migrates → saves → loads again.
- A documented compatibility window.

**Recommendation (do before any v2 work):**
1. Check at least 3–5 representative real `.aestra` project files into `Tests/AestraAudio/fixtures/projects/v1/`.
2. Add a `ProjectRoundtripV1Test.cpp` that asserts identical re-serialization (modulo formatting whitespace) and identical `TrackManager` snapshot.
3. Add `ProjectMigrationDryRunTest.cpp` that runs `runMigrations(v0_synthetic, 1)` and confirms the framework end-to-end (currently no test exercises it).

### 3.3 [P1] `ProjectSerializer` is compiled separately into both `AestraHeadless` and `Source/`

`@/home/currentsuspect/Dev/Aestra/CMakeLists.txt:172-175`
```cmake
add_executable(AestraHeadless
    Source/App/HeadlessMain.cpp
    Source/Core/ProjectSerializer.cpp
)
```

`Source/CMakeLists.txt` also pulls it into the main app. Two compilations of the same TU is harmless but invites divergence (compile defs, include dirs). More importantly, `AestraAudioCore` itself has no project-loading capability — meaning every offline/audio-only test that needs to load a project pulls in a Source-layer TU. This makes serialization untestable from `AestraAudio` tests without rebuilding the dependency.

**Recommendation:** Move `ProjectSerializer.{h,cpp}` into `AestraAudio/src/IO/` (or a new `AestraProject` module) and link both `AestraHeadless` and `Source` against it. Serialization is `AGENTS.md §12`-high-risk and should live alongside the things it serializes (`TrackManager`, `PatternManager`, etc.).

### 3.4 [P1] No content hashing / asset integrity check

`resolveProjectAssetPath` (`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:172-179`) resolves relative asset paths but does not record a checksum. If the user moves a `.wav` next to the project, the project loads with a different-content file silently. For a DAW this is a footgun.

**Recommendation:** Record `sha256` (or `xxhash64`) of every referenced asset in the project file. Surface mismatches in `ProjectLoadReport` as warnings, not errors.

### 3.5 [P2] `std::atomic_load` on `std::shared_ptr` (deprecation hazard)

Also referenced in §2.3. `Source/Core/ProjectSerializer.cpp` itself is not affected; `AestraAudio` and the published-snapshot pattern are. Track across both layers.

---

## 4. UI / rendering architecture

### 4.1 [P0] UI monoliths — 3 files account for ~13 kLOC

| File | LOC | Concern |
| --- | ---: | --- |
| `@/home/currentsuspect/Dev/Aestra/Source/Components/TrackManagerUI.cpp` | 5363 | view + dispatch + selection + drag/drop + context menu |
| `@/home/currentsuspect/Dev/Aestra/Source/Components/FileBrowser.cpp` | 4227 | tree model + filesystem watcher + preview + DnD source |
| `@/home/currentsuspect/Dev/Aestra/Source/Core/AestraContent.cpp` | 3523 | main content shell + cross-panel routing |
| `@/home/currentsuspect/Dev/Aestra/Source/Components/TrackUIComponent.cpp` | 2749 | per-track widget tree |

These are the natural growth point for layout/regression bugs. Per `AGENTS.md §3` the right cadence is *"smallest safe change"* — but these files are large enough that "small" changes routinely touch lines hundreds apart, defeating review.

**Recommendation:** Carve along seams:
- `TrackManagerUI.cpp` → split out `TrackManagerSelection.cpp`, `TrackManagerDnD.cpp`, `TrackManagerContextMenu.cpp`.
- `FileBrowser.cpp` → split out `FileBrowserModel.cpp`, `FileBrowserWatcher.cpp`, `FileBrowserPreview.cpp`.
- `AestraContent.cpp` → split panel routing into `AestraPanelRouter.cpp` and keep the shell minimal.

This is mechanical and low-risk if done one file per PR.

### 4.2 [P1] AestraUI does not declare a public include surface

`@/home/currentsuspect/Dev/Aestra/AestraUI/Base/` and `AestraUI/Core/` ship `.h` and `.cpp` side-by-side (no `include/` separation). `Source/Components/*` and `Source/Panels/*` therefore `#include "NUIButton.h"` etc. without any "public vs private API" partition. Any internal helper is reachable from app code, and any churn inside `NUI*` propagates uncontrollably.

**Recommendation:** Introduce `AestraUI/include/AestraUI/...` for the public surface, and treat the in-tree `Base/Core/` paths as private. Mirror the `AestraAudio` layout (which does this correctly: `@/home/currentsuspect/Dev/Aestra/AestraAudio/CMakeLists.txt:422-447`).

### 4.3 [P1] No clear UI-thread vs main-thread story

`Source/AudioThreadConstraints.h` defines "main thread" semantics but nothing in `AestraUI/Core/` enforces them. The render loop (`NUIApp`), input handling, and audio-driven panel updates (level meters, timeline playhead) all coexist with no documented thread affinity beyond AGENTS.md prose. Combined with §1.2 (dead guard), the runtime cannot detect a UI call accidentally landing on the audio thread or vice versa.

**Recommendation:** Add a parallel `g_isUIThread` set by `NUIApp::run()`, and have any UI mutator API assert `isUIThread() && !isAudioThread()`. Cheap and catches real bugs.

### 4.4 [P2] `AestraUI` config sprawl

`@/home/currentsuspect/Dev/Aestra/AestraUI/Config/` carries `aestra_ui_config.yaml`, `slider_config.txt`, plus per-config loader code in `NUIConfigLoader.cpp` (~13 KB) and `NUISliderConfig.cpp`. Mixed YAML + plain-text + bespoke loaders for similar concerns.

**Recommendation:** Consolidate on one config format (YAML is already chosen for the main file) and route all UI config through `NUIConfigLoader`. Retire `slider_config.txt`.

---

## 5. Tests & CI posture

### 5.1 [P1] Test corpus is heavily DSP-weighted

`@/home/currentsuspect/Dev/Aestra/Tests/AestraAudio/` contains 60+ DSP/audio test TUs. `Tests/AestraUI/` has 3. There is **no** test directory for project serialization roundtrip (see §3.2), no test for `AudioEngine` command-queue behavior under bounded saturation, and no test for the `RealtimeThreadGuard` itself (it would catch §1.2 instantly).

**Recommendation, in priority order:**
1. `Tests/AestraAudio/RealtimeThreadGuardTest.cpp` — assert all `reportRealtimeMisuse` call sites trip when invoked under `ScopedRealtimeAudioThread`.
2. `Tests/AestraAudio/ProjectRoundtripTest.cpp` — see §3.2.
3. `Tests/AestraAudio/AudioCommandQueueSaturationTest.cpp` — verify the 16-cmds-per-block cap drops no command edges (transport restart/stop in particular).

### 5.2 [P2] Headless CLI test is trivial

`@/home/currentsuspect/Dev/Aestra/CMakeLists.txt:244-250` registers exactly one CLI test: `AestraHeadlessExportRejectsBadArgs`. The export path otherwise has no end-to-end test asserting that an exported render is deterministic across runs.

**Recommendation:** Add a fixed-seed offline render test that exports a small project and compares against a checked-in WAV (with a per-sample tolerance) — `AGENTS.md §11` already mandates listening for state-format changes; an automatic version is cheaper.

---

## 6. Prioritized recommendations

### P0 — fix or plan before next significant change
1. **Remove `AestraLicense` PUBLIC link from `AestraAudioCore`** (§1.1). One CMake edit.
2. **Consolidate the two RT-thread guards** into `RealtimeThreadGuard.h` and delete `AudioThreadConstraints.h`'s parallel TLS flag (§1.2).
3. **Land a v1 project roundtrip test + fixture corpus** before any schema-touching PR (§3.2).
4. **Eliminate the `AudioEngine` fallback singleton** (§1.7).

### P1 — schedule within current cycle
5. Replace `windows.h` in `AudioEngine.h` with an opaque handle (§1.4).
6. Hoist `MidiBuffer mOut` allocations out of the RT loop (§2.2) and audit `MidiBuffer` ctor.
7. Capture `AudioArsenalSnapshot` once per `processBlock` instead of three times (§2.3).
8. Move `ProjectSerializer` into `AestraAudio` (§3.3) and add asset-hash integrity (§3.4).
9. Tighten global warning suppressions to per-target (§1.5).
10. Begin the `TrackManagerUI.cpp` / `FileBrowser.cpp` split (§4.1) — one file per PR.
11. Introduce `AestraUI/include/` public-API split (§4.2).
12. Replace relative `../../AestraCore/include/...` includes with proper module includes (§1.3).

### P2 — code-health backlog
13. `AudioEngine` god-class split + remove `friend` (§2.5).
14. Move denormal control to thread-entry (§2.6).
15. Clean up duplicate / commented entries in `AestraAudio/CMakeLists.txt` (§1.6).
16. UI-thread guard for symmetry (§4.3).
17. `AestraUI/Config/` consolidation (§4.4).
18. Determinism test for headless export (§5.2).

---

## 7. What this audit did NOT cover

- **DSP correctness**: AVX-512 dispatch, AestraVerb/AestraComp algorithmic behavior, denormal handling at the math level. `AGENTS.md §11` requires listening tests; out of scope here.
- **Plugin hosting (VST3 / CLAP)**: only the build-system gating was inspected; out-of-process IPC and parameter automation paths are not reviewed.
- **License layer internals** (`AestraLicense/`, `workers/license-signing/`): only the build-time dependency was audited, not crypto or signing flows.
- **Per-platform backends** (`AestraAudio/src/Win32/`, `AestraAudio/src/Linux/`): glanced via CMake only; the linker-group RESCAN logic was not deeply analyzed.
- **Settings/Preferences**, **Panels**, and **HUD** subsystems beyond the file inventory.

These are good candidates for follow-up audits, each scoped narrower than this one.

---

## Final Report

### Git State
- Starting branch/SHA: (unchanged, read-only)
- Final branch/SHA: (unchanged, read-only)
- Working tree status: one new doc added (`AestraDocs/architecture/ARCHITECTURE_AUDIT_2026Q2.md`)

### Files Changed
- `AestraDocs/architecture/ARCHITECTURE_AUDIT_2026Q2.md`: new architectural audit document.

### Change Type
- DSP changed: no
- UI changed: no
- Serialization/project format changed: no
- Build/CI config changed: no
- Public docs changed: no (this is `AestraDocs/`, internal)

### Validation
- Commands run: `wc -l` on a handful of large files (read-only).
- Tests passed: none required (no code change).
- Tests failed: none.
- Checks skipped and why: build/test not run — read-only audit per AGENTS.md §3.

### Risk Notes
- Remaining risks: this audit identifies risks; it does not introduce any. Acting on §1.1 (license link), §1.2 (dead guard), and §2.3 (snapshot capture) should be done with their own per-PR validation.
- Follow-up work: see §6 prioritized list.
