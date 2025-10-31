# Nomad DAW — Bug Triage (2025-10-31)

**Build:** Release/Debug (Windows)
**Audio:** WASAPI Exclusive/Shared enabled
**Display:** Windowed + Maximized
**System goalposts:** UI ≥30 FPS, Audio ≤10 ms RTL

---

## 🟥 Critical


### 1) Exclusive mode while another app holds Exclusive → loud, distorted output (no graceful fallback)

**Severity:** Critical
**Area:** Audio engine / WASAPI init & format negotiation

**Repro**

1. Open **FL Studio** using **WASAPI Exclusive** on the same output device.
2. Launch **Nomad** with the device set to **WASAPI Exclusive**.
3. Hit Play in Nomad (without switching to Shared).

**Expected**

* If the device is already locked by another Exclusive client, Nomad:

  * Detects it and **fails gracefully** (clear toast/banner: *“Device in use (Exclusive). Switch to Shared or free the device in FL.”*), **or**
  * **Auto-falls back to Shared** cleanly (sample rate/format renegotiated), and
  * Applies a **soft-start ramp** so there are **no blasts** or harsh artifacts.

**Actual**

* Playback starts but the audio is **very loud and heavily distorted**.

**Likely causes**

* We’re attempting to run despite **AUDCLNT_E_DEVICE_IN_USE / _NOT_ALLOWED** and end up in an undefined/mismatched stream.
* **Format mismatch** (e.g., writing Float32 to Int16 PCM, or 44.1 ↔ 48 kHz) after our “auto-detect” path changed the device format without updating the engine/resampler.
* Output buffers not zeroed on underflow/reinit; no ramp/limiter on (re)start.

**Acceptance Criteria**

* With FL holding Exclusive, starting playback in Nomad **never** emits harsh/loud audio.
* Nomad either:

  * Presents a **clear prompt** and **stays silent** until user switches to Shared or frees the device, **or**
  * **Automatically switches to Shared** and plays clean audio within ≤200 ms.
* On any device (re)init, output uses a **50–150 ms fade-in** ramp; buffers are zeroed; peak limiter prevents >0 dBFS bursts.
* Logs show: device ID, requested/actual share mode, requested/actual format, HRESULTs from `IsFormatSupported`/`Initialize`, whether fallback occurred.

**Developer Tasks**

* Pre-flight probe for Exclusive:

  * Call `IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, …)`; handle `AUDCLNT_E_DEVICE_IN_USE`/`_NOT_ALLOWED` explicitly.
  * If busy, **disable Exclusive toggle** (UI) and surface a **“Device busy”** message.
* Robust fallback:

  * On Exclusive failure, **re-init Shared** with the device’s **mix format**; apply resampler if engine SR ≠ device SR.
  * Ensure **sample-type conversion** (Float32 ↔ Int16/24) is correct and clipping-safe.
* Safety net:

  * **Zero** output buffers on (re)init/underrun.
  * Add **soft-start ramp** and a lightweight **peak limiter** for first ~200 ms after graph start/restart.
* Telemetry:

  * Log format tuples `(sr, channels, bits, sampleType)` before/after init, HRESULTs, and fallback path taken.
* QA checklist:

  * FL in Exclusive → Nomad Exclusive: toast + silence **or** clean auto-Shared.
  * Release device in FL; retry Exclusive in Nomad succeeds cleanly (no pop).
  * Change engine SR while Shared; playback remains clean.

---

## 🟧 High

### 2) File Browser: mouse interaction broken; keyboard nav should be Up/Down only

**Area:** UI input (File Browser)
**Steps**

1. Open File Browser.
2. Attempt to double-click to open a folder, with the mouse.

**Expected:** Mouse double-click works normally; keyboard navigation uses Up/Down only.
**Actual:** Mouse interaction fails or act inconsistently; Left/Right keys act on items.

**Acceptance:** Mouse click selects; double-click opens; wheel scrolls; only Up/Down move selection; Enter activates.

---

### 3) Track scrollbars (H & V) “jump” to top/left when dragging the thumb

**Area:** Scrollbars (Timeline/Tracks)
**Steps**

1. Click and drag the scrollbar thumb on either axis.

**Expected:** Thumb follows pointer delta smoothly.
**Actual:** Thumb snaps to top/left (“jumps”), making precision impossible.

**Hypothesis:** Using absolute vs relative drag origin; missing clamp; incorrect normalization on first mouseDown.
**Acceptance:** Dragging preserves initial grab offset; no sudden snaps; inertial/step scrolling remains correct.

---

### 4) Playhead: drag does not work (click-to-seek only)

**Area:** Transport/Timeline interaction
**Steps**

1. Click and hold on the playhead.
2. Drag horizontally.

**Expected:** Continuous scrub/seek while dragging.
**Actual:** Only point-and-click seek; drag is ignored.

**Acceptance:** Press-drag updates playhead each frame; release finalizes position; cursor-follow is smooth at current FPS target.

---

### 5) Dropdown click-through: selecting a dropdown option triggers button underneath

**Area:** UI z-order / hit testing
**Steps**

1. Open any dropdown in Settings/Toolbar.
2. Click to select an option.

**Expected:** Only the dropdown handles the click; nothing beneath toggles.
**Actual:** A button behind the dropdown toggles (“ghost click”).

**Hypothesis:** Missing modal layer or event consumption on overlay; z-index/hit-test order.
**Acceptance:** While a dropdown is open, underlying controls never receive pointer events.

---

### 6) Multiple tracks can be Solo’d simultaneously (should be exclusive)

**Area:** Mixer/Tracks state machine
**Steps**

1. Click Solo on Track 1, then Solo on Track 2.

**Expected:** Solo is exclusive (last-clicked wins), unless a “Solo Safe/Group” feature exists.
**Actual:** Multiple tracks remain Solo at once.

**Acceptance:** With default behavior, only one Solo is active (or implement explicit multi-solo groups with UI affordance).

---

## 🟨 Medium

### 7) Playhead L/R extents aren’t clipped; bleed into controls/scrollbars

**Area:** Rendering/clipping (scissor) + hit-test
**Steps**

1. Scroll outside playhead while playback is on.
2. Wait for it to come to view and out of view.

**Expected:** Playhead visuals and hit-area stay inside the timeline viewport.
**Actual:** Playhead lines bleed into track controls and scrollbars.

**Hypothesis:** Missing scissor rect or clip path for the timeline layer.
**Acceptance:** Playhead visuals and interactions are fully constrained to the timeline region.

---

### 8) Profiler view doesn’t maximize correctly

**Area:** Window/layout system (Profiler)
**Steps**

1. Maximize the main view/window.

**Expected:** Layout scales like the FPS monitor (panels and graphs adapt).
**Actual:** Contents do not scale/reflow; bg fills the screen but text adapts; graph maximizes.

**Acceptance:** On maximize/restore, panels reflow; text remains readable; no overlayed content(unless we add it that way in future which we might).

---

### 9) Settings panel needs a third column (current UI bleeds outside)

**Area:** Settings layout
**Steps**

1. Open Settings.

**Expected:** Content stays within container; columns adapt (tabs later when Info/Profile arrives).
**Actual:** Content bleeds outside the container.

**Acceptance:** Three-column grid (or responsive wrap) contains all controls without overflow at supported widths.

---

## 🟩 Low / Enhancements

### 10) Quality Presets need redefinition after adding new features

**Area:** Settings → Quality
**Change:**

* **Master** = absolute best quality
* **High Fidelity (default)** = balanced “best of both worlds”
* **The rest** = For improved performance or custom and low end PCs.
* Ensure new quality toggles map cleanly to each preset.

**Acceptance:** Selecting a preset sets all relevant toggles; preset descriptions match resultant settings.

---

### 11) Playhead line length should extend (infinite feel)

**Area:** Timeline rendering polish
**Acceptance:** Playhead extends full viewport height (and/or across clip region) without impacting performance.

---

🟧 High 2.0 - Forgotten Soldiers 😅

### 12) GitHub Workflows & dev scripts don’t run

**Severity:** High
**Area:** CI/CD (GitHub Actions) & local helper scripts

**Repro**

1. Push to `develop` (or open a PR to `main`).
2. Observe Actions: workflows fail or don’t trigger.
3. Run local scripts (`/scripts/*.ps1`, `.sh`) from project root.

**Expected**

* CI triggers on push/PR, builds/tests artifacts, and uploads release artifacts when tagged.
* Local scripts run cross-platform (Windows PowerShell / Bash) and succeed with clear output.

**Actual**

* Workflows fail to start or error out (e.g., missing permissions/secrets, invalid path, non-executable scripts).
* Local scripts error (e.g., wrong interpreter, CRLF/exec bit, wrong working dir).

**Likely causes**

* Wrong `on:` filters/branches; missing `permissions:` for `GITHUB_TOKEN`.
* Actions pinned to deprecated versions; missing `secrets` (e.g., `ACTIONS_DEPLOY_KEY`).
* Script path issues (`./scripts` vs `scripts`), CRLF line endings, missing execute bit on `*.sh`, PowerShell execution policy.
* CMake/Cache not set; artifact paths wrong.

**Acceptance**

* On `push` to `develop` and `pull_request` to `main`, CI:

  * checks out, configures CMake, builds in Release + runs unit tests, caches dependencies;
  * uploads build artifacts on success.
* Tag `v*`: release workflow publishes signed artifacts.
* Local scripts run on Windows & Linux with one command (`pwsh scripts/dev.ps1` or `bash scripts/dev.sh`) and finish without errors.

**Developer Tasks**

* Update workflows:

  * Add explicit `permissions:` (e.g., `contents: read`, `actions: read`, `id-token: write` if needed).
  * Pin maintained actions (`actions/checkout@v4`, `actions/cache@v4`, `ilammy/msvc-dev-cmd@v1`, `actions/upload-artifact@v4`).
  * Fix `on:` to include `push: [develop]`, `pull_request: [main]`, and `workflow_dispatch`.
  * Ensure `shell:` and paths are correct; set working-directory.
* Scripts:

  * Provide twin scripts `dev.ps1` and `dev.sh`.
  * Normalize line endings (LF) and set exec bit for `*.sh`.
  * Print usage/help; exit with non-zero on failure.
* CMake:

  * Standardize build dir (`build/`), generator, and presets (CMakePresets.json).
* QA:

  * Push a dummy commit to `develop` → CI green; open PR to `main` → CI green; create tag → release artifacts appear.

**Labels:** `ci/cd`, `area:build`, `severity:high`

---

### 13) Piano Roll compilation errors (target wiring / includes / linkage)

**Severity:** High
**Area:** Build system (CMake) & Piano Roll module

**Repro**

1. Build the project (local or CI).
2. Compilation fails when including/using the Piano Roll (errors vary: missing symbols, circular includes, ODR, undefined refs).

**Expected**

* Piano Roll compiles cleanly across targets; links into Nomad UI without undefined references; headers are self-contained.

**Actual**

* Build fails on Piano Roll sources/headers or at link step.

**Likely causes**

* CMake target not declared as a proper library or not linked to its deps (e.g., NomadCore, NomadUI, math, containers).
* Circular includes; missing forward declarations; headers rely on transitive includes.
* Mismatched compile definitions or RTTI/exception settings per target.
* Source files added to the wrong target; missing `target_include_directories`.
* ODR violations from duplicate single-definition utilities.

**Acceptance**

* Full project builds (Debug/Release) on Windows locally and in CI with zero Piano Roll errors.
* Piano Roll unit tests (selection, note editing, scrolling, snapping) compile/run and pass.
* No warnings above agreed baseline (e.g., /W4 or -Wall with suppressions logged).

**Developer Tasks**

* CMake hygiene (example):

  ```cmake
  add_library(NomadPianoRoll STATIC
    src/PianoRoll.cpp
    src/NoteGrid.cpp
    src/Selection.cpp
    include/nomad/pianoroll/PianoRoll.hpp
    include/nomad/pianoroll/NoteGrid.hpp
    include/nomad/pianoroll/Selection.hpp
  )
  target_include_directories(NomadPianoRoll PUBLIC include)
  target_link_libraries(NomadPianoRoll
    PUBLIC NomadCore NomadUI
    PRIVATE NomadMath NomadContainers
  )
  target_compile_features(NomadPianoRoll PUBLIC cxx_std_20)
  ```
* Header discipline:

  * Each header includes what it uses; remove hidden transitive deps.
  * Replace circular includes with forward declarations; move inline-only into headers; keep definitions in `.cpp`.
* Linkage:

  * Ensure NomadUI links **against** `NomadPianoRoll`; do not duplicate sources across targets.
  * Consolidate utility singletons to one TU to avoid ODR.
* Config:

  * Align compile options/defines across targets (exceptions/RTTI, `_UNICODE`, `NOMAD_PLATFORM_*`).
  * Add minimal unit tests under `tests/pianoroll/`.
* QA:

  * Clean build (`git clean -xfd && cmake --preset release && cmake --build --preset release`) succeeds locally and in CI.
  * Run tests in CI; artifacts include `NomadPianoRoll.lib/.a` and executable with Piano Roll integrated.

**Labels:** `area:pianoroll`, `area:build`, `severity:high`


## ✅ Quick Regression Tests (post-fix)

* Switch Exclusive ↔ Shared during playback 10x; no dropouts/harshness; transport continues within 200 ms.
* File Browser: mouse select, double-click, wheel; Up/Down navigate; Left/Right do nothing.
* Drag both scrollbars for 10 seconds; no snaps; content position matches drag.
* Press-drag the playhead across 1–2 minutes of timeline; smooth scrub at stable FPS.
* Open a dropdown above a toggle; click to select; underlying control never toggles.
* Solo: enabling on a second track disables the first (unless “Solo Safe” is explicitly enabled).
* Window maximized: no clipped text/graphs; resize back and forth—layout remains stable.
* Settings: three columns fit; no overflows at 1280×720 and above.
* Presets: switching presets updates all quality toggles deterministically.
* Playhead renders within clip; no bleed into controls/scrollbars.

---

## Labels & Ownership (suggested)

* `area:audio` → #1
* `area:ui-input` → #2, #5
* `area:ui-scroll` → #3
* `area:transport` → #4, #7, #11
* `area:layout` → #8, #9
* `area:mixer` → #6
* `area:settings` → #10
* `severity:critical` → #1
* `severity:high` → #2, #3, #4, #5, #6
* `severity:medium` → #7, #8, #9
* `type:enhancement` → #10, #11

---

### Notes for Implementers

* **Audio mode switch (#1):** Ensure full teardown → device re-enumeration → format negotiation → stream init → ramp in; guard against stale `IAudioClient` references and mismatched sample rates/channel masks.
* **UI bleed (#5, #7, #9):** Add a modal overlay that captures events; enforce scissor rectangles for timeline; verify hit-testing order matches z-order.
* **Scrollbars (#3):** Record mouseDown offset within the thumb and add it to deltas; clamp to track bounds before mapping to content range.
* **Solo logic (#6):** Centralize solo/mute state in TrackManager; when a solo is set, clear others (unless “Solo Safe/Group” flags).