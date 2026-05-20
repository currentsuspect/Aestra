# AGENTS.md — Aestra Automation Policy

This file defines how AI agents, bots, and automation tools should work inside the Aestra repository.

It is intentionally strict. Aestra is a native C++ DAW with real-time audio constraints, project persistence, CI requirements, and source-available licensing. Small mistakes can cause crashes, data loss, broken exports, leaked private assets, or misleading public claims.

Read this file before making changes.

---

## 1. Scope

This policy applies to:

- Codex
- Cursor
- Copilot
- CodeRabbit
- ChatGPT-generated patches
- Local automation scripts
- Any bot-generated or AI-assisted contribution

Agents must keep changes small, reviewable, factual, and aligned with the real repository state.

Do not invent roadmap claims, release status, architecture facts, benchmark results, or test results.

---

## 2. Non-Negotiable Rules

These rules override convenience.

- Do not delete, rename, reset, force-push, or rewrite `main` or `develop`.
- Do not commit or push unless explicitly asked.
- Do not merge pull requests automatically.
- Do not blindly accept bot-generated code.
- Do not fabricate test results, SHAs, benchmark numbers, or validation claims.
- Do not add global AVX, AVX2, or AVX512 compiler flags.
- Do not introduce locks, heap allocations, blocking calls, logging, file I/O, or sleeps into real-time audio paths.
- Do not modify tracked runtime state files unless explicitly asked.
- Do not commit secrets, tokens, keys, credentials, private assets, premium binaries, model weights, or `.env` files.
- Do not change licensing, release posture, pricing, or public roadmap language unless explicitly asked.
- Do not hide failures behind broad suppressions, fake fallbacks, or misleading “success” paths.
- Do not silence CodeRabbit, clang-tidy, compiler, sanitizer, or CI findings without a clear technical reason.
- Do not change serialization/project format casually. Treat persistence changes as high-risk.
- Do not “fix” dead/commented code unless the task explicitly targets it.

When unsure, prefer reporting uncertainty over guessing. When the user's intent is ambiguous or unclear, ask for clarification rather than assuming.

---

## 3. Agent Workflow

Before editing:

1. Inspect the current branch and working tree.
2. Identify the smallest safe change.
3. Read nearby code and existing patterns before creating new abstractions.
4. Check whether the change touches real-time audio, serialization, project loading, CI, licensing, security, or public docs.
5. Make one scoped change at a time.

After editing:

1. Build the affected target.
2. Run the narrowest relevant test first.
3. Run broader tests when the change touches shared systems.
4. Report exactly what was changed, tested, skipped, and why.

Preferred workflow:

```bash
git status --short
git branch --show-current
git rev-parse --short HEAD
````

Do not proceed with destructive Git operations unless explicitly requested.

---

## 4. Required Final Report Format

Every completed agent session must report:

```md
## Final Report

### Git State
- Starting branch/SHA:
- Final branch/SHA:
- Working tree status:

### Files Changed
- `path/to/file`: summary

### Change Type
- DSP changed: yes/no
- UI changed: yes/no
- Serialization/project format changed: yes/no
- Build/CI config changed: yes/no
- Public docs changed: yes/no

### Validation
- Commands run:
- Tests passed:
- Tests failed:
- Checks skipped and why:

### Risk Notes
- Remaining risks:
- Follow-up work:
```

Never claim “all green” unless the relevant command was actually run and passed.

---

## 5. Project Architecture

Aestra is a C++17 native DAW with a modular architecture.

```text
Aestra
├── AestraCore        — Foundation: math, threading, logging, config, profiler
├── AestraPlat        — Platform abstraction: windowing, OpenGL, input
├── AestraAudio       — Real-time audio engine, DSP, mixer, plugin hosting
├── AestraUI          — Custom GPU-accelerated UI framework
├── AestraPlugins     — Premium/internal plugins where available
├── AestraSDK         — Plugin/extension API, currently limited/planned
├── Source            — Main DAW application
├── Tests             — Centralized test suite
├── aestra-core       — Public core-mode variant/stub layer
├── docs/             — Public documentation
├── AestraDocs/       — Internal architecture/design notes
└── scripts/          — Build, audit, utility, and maintenance scripts
```

### Dependency Shape

```text
Aestra
├── AestraCore
├── AestraPlat -> AestraCore
├── AestraUI_Core
├── AestraUI_Platform -> AestraUI_Core, AestraPlat
├── AestraUI_OpenGL -> AestraUI_Core, glad, FreeType
├── AestraAudio interface -> platform backend
└── AestraAudioCore -> AestraCore, AestraPlat
```

Prefer existing module boundaries. Do not create cross-layer dependencies casually.

---

## 6. External Dependencies

External dependencies may be present as submodules or local vendor trees.

| Dependency | Typical Path                       | Purpose               |
| ---------- | ---------------------------------- | --------------------- |
| VST3 SDK   | `AestraAudio/External/vst3sdk`     | VST3 plugin hosting   |
| CLAP SDK   | `AestraAudio/External/clap`        | CLAP plugin hosting   |
| RtAudio    | `AestraAudio/External/rtaudio`     | Audio I/O             |
| miniaudio  | `AestraAudio/External/miniaudio`   | Audio decoding        |
| FreeType   | `AestraUI/External/freetype_local` | Text rendering        |
| SDL2       | `external/SDL2`                    | Linux windowing/input |

Important:

* CI may not check out all submodules.
* VST3-dependent code must be conditional on SDK presence.
* Do not make public/core builds depend on premium/private modules.
* Do not vendor new dependencies without explicit approval.

---

## 7. Build System

Aestra uses CMake.

* Minimum CMake: 3.22
* Language: C++17
* Default public mode: `Aestra_CORE_MODE=ON`
* CI-safe mode: headless/core/testable

### Important CMake Options

| Option                             | Default | Meaning                                 |
| ---------------------------------- | ------: | --------------------------------------- |
| `Aestra_CORE_MODE`                 |      ON | Build without premium/private modules   |
| `AESTRA_HEADLESS_ONLY`             |     OFF | Skip UI targets for CI/container builds |
| `AESTRA_ENABLE_UI`                 |      ON | Build desktop UI                        |
| `AESTRA_ENABLE_TESTS`              |      ON | Build test executables                  |
| `AESTRA_LOW_MEMORY_BUILD`          |     OFF | Memory-saving build mode                |
| `AESTRA_ENABLE_RUNTIME_TESTS`      |     OFF | Enable hardware/device/runtime tests    |
| `AESTRA_ENABLE_EXPERIMENTAL_TESTS` |     OFF | Enable unstable/experimental tests      |

### Standard Build

```bash
cmake -S . -B build \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel
```

### Headless CI-Safe Build

```bash
cmake -S . -B build-headless \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-headless --parallel
```

### Preset Build

```bash
cmake --preset headless
cmake --build --preset headless-release
```

### Test Command

```bash
ctest --test-dir build --output-on-failure
```

---

## 8. Build Gotchas

* `AESTRA_LOW_MEMORY_BUILD` must be configured before `project()` if used in root CMake.
* Do not add global `/arch:AVX2`, `-mavx2`, `/arch:AVX512`, or `-mavx512*`.
* SIMD must use runtime dispatch or isolated per-translation-unit targeting.
* AVX512 code must remain guarded and platform-aware.
* RtAudio sources should only compile into platform backend targets, not duplicated into core libraries.
* GNU ld circular dependency fixes using linker groups must not be removed without validating Linux builds.
* FreeType warning suppressions are intentional. Do not “clean them up” unless explicitly asked.
* UI dependencies should not leak into headless builds.

---

## 9. Code Conventions

Follow `.clang-format` and `.clang-tidy`.

### Formatting

* Base style: LLVM
* Indent: 4 spaces
* No tabs
* Column limit: 120
* Attached braces
* Pointer alignment: left, e.g. `int* ptr`
* Includes sorted and regrouped

### Naming

| Element          | Convention          | Example                |
| ---------------- | ------------------- | ---------------------- |
| Classes          | `PascalCase`        | `AudioEngine`          |
| Methods          | `camelCase`         | `openStream`           |
| Members          | `m_camelCase`       | `m_sampleRate`         |
| Constants/macros | `UPPER_SNAKE_CASE`  | `AESTRA_HEADLESS_ONLY` |
| Files            | `PascalCase.h/.cpp` | `AudioEngine.cpp`      |
| Namespace        | `Aestra`            | `Aestra::AudioEngine`  |

### Include Layout

Headers are organized by domain under `AestraAudio/include/`, including:

* `Core/`
* `DSP/`
* `Models/`
* `Playback/`
* `IO/`
* `Drivers/`
* `Plugin/`
* `Commands/`
* `Headless/`

Prefer existing locations and naming patterns.

---

## 10. Real-Time Audio Rules

Real-time audio code is safety-critical.

Inside audio callbacks or real-time processing paths:

### Forbidden

* Heap allocation
* Mutexes or blocking locks
* File I/O
* Console logging
* Sleeping
* Waiting on futures/promises
* Unbounded loops
* System calls
* UI calls
* Throwing exceptions
* Dynamic plugin discovery
* JSON parsing
* Project save/load
* Any operation with unpredictable latency

### Required

* Lock-free communication where needed
* Preallocated buffers
* Bounded work
* Clear ownership
* Deterministic behavior
* NaN/Inf protection where applicable
* Safe bypass behavior
* Stable behavior across sample rates and buffer sizes

Common safe communication pattern:

```text
UI/threaded command path -> SPSC queue/ring buffer -> audio thread consumes bounded commands
```

Do not “just add a mutex” to fix audio-thread races.

---

## 11. DSP Rules

When changing DSP:

* Preserve bypass parity unless the task explicitly changes bypass behavior.
* Avoid loudness jumps, clicks, denormals, NaN, and Inf.
* Validate silence input.
* Validate impulse/transient behavior when relevant.
* Validate sample-rate-dependent behavior.
* Keep parameter smoothing where needed.
* Do not change plugin IDs or stable parameter IDs casually.
* Do not alter saved state compatibility without migration logic.
* Do not fake quality improvements with labels or UI-only claims.

DSP changes must report:

* Whether audio output changed
* Whether latency changed
* Whether state format changed
* Relevant tests/labs run
* Any subjective listening assumptions

---

## 12. Serialization and Project Files

Project persistence is high-risk.

When touching save/load, project roundtrip, migration, clips, tracks, routes, plugins, Arsenal state, automation, or export state:

* Preserve backward compatibility where possible.
* Add migration/default handling for new fields.
* Do not silently drop unknown or future fields unless existing policy requires it.
* Fail loudly and diagnostically on corrupt/unresolvable critical state.
* Add or update roundtrip/regression tests.
* Validate both UI and headless/backend paths where applicable.

Never treat “the UI still opens” as sufficient validation for project persistence.

---

## 13. Testing Policy

Use the smallest relevant test first, then broaden.

### Test Categories

| Category              | Rule                                                    |
| --------------------- | ------------------------------------------------------- |
| Pure logic/unit tests | Always safe to run                                      |
| Headless audio tests  | Preferred for CI and agent work                         |
| Runtime/device tests  | Must be gated behind `AESTRA_ENABLE_RUNTIME_TESTS`      |
| Experimental tests    | Must be gated behind `AESTRA_ENABLE_EXPERIMENTAL_TESTS` |
| UI/manual tests       | Report as manual, not automated proof                   |

### Test Requirements

Add or update tests when changing:

* Audio engine behavior
* DSP
* Export/bounce
* Project save/load
* Plugin state
* Routing
* Automation
* Threading/lifetime
* CI/build scripts
* Security-sensitive code

Do not add hardware-dependent tests to the always-on CI tier.

---

## 14. CI/CD Expectations

CI is part of the product contract.

Typical workflows may include:

| Workflow                           | Purpose                          |
| ---------------------------------- | -------------------------------- |
| `ci.yml`                           | Main build/test gate (push/PR)   |
| `nightly.yml`                      | Scheduled nightly build + tests  |
| `public-ci.yml`                    | Public branch guard (Windows)    |
| `docs-check.yml`                   | Documentation validation         |
| `deploy-docs.yml`                  | Public docs deployment           |
| `gitleaks.yml` / `gitleaks-pr.yml` | Secret scanning                  |
| `api-docs.yml`                     | Doxygen/API docs                 |
| `aestra-reverb-simd-lab.yml`       | Reverb SIMD lab (develop/audio)  |
| `dsp-benchmark.yml`                | DSP benchmark (develop/DSP)      |
| `private-release.yml`              | Manual/private release flow      |

General CI rules:

* Keep public/core builds independent from private assets.
* Do not weaken secret scanning.
* Do not make macOS/Windows/Linux assumptions without guards.
* Do not remove failing tests to make CI pass.
* Do not convert real failures into advisory checks without explicit approval.

All public CI builds should remain compatible with core/headless mode unless explicitly changed.

---

## 15. Runtime State Files

Some runtime state files may be tracked for historical or development reasons.

Do not modify, regenerate, normalize, delete, or include changes to these files unless explicitly asked:

* `audio_settings.conf`
* `browser_settings.json`
* `aestra_profile.json`
* `autosave.aes`
* `crash_flag`
* `runtime_log.txt`
* `build_log.txt`

If they change during local testing, revert them before finalizing unless the task explicitly requires updating them.

Recommended check:

```bash
git status --short
```

---

## 16. Known Dead or Sensitive Areas

Do not revive, rewrite, or “clean up” these areas unless explicitly tasked:

* `SelectionModel.cpp` if commented out or detached from current PlaylistModel APIs
* `SelectionModel.h` if it references removed/nonexistent clip APIs
* Fully commented-out serialization compatibility tests
* Deprecated compatibility paths
* Private/premium module stubs
* License-gated code
* Runtime state files
* Generated artifacts

Dead code can still encode historical design decisions. Do not delete it casually.

---

## 17. Documentation Rules

Public docs must match real behavior.

Do not claim:

* A feature exists unless implemented and validated.
* A release is available unless published.
* A test passed unless run.
* A benchmark improved unless measured.
* A plugin/module is public if it depends on private code/assets.
* A workflow is supported unless CI/build scripts support it.

Use:

* `docs/` for public documentation.
* `AestraDocs/` for internal design notes, architecture reports, implementation plans, and status documents.
* `labs/` for experiments, generated evidence, quality reports, and benchmark artifacts.

Do not put temporary project status into `AGENTS.md`.

---

## 18. Security and Licensing

Aestra is source-available, not open-source.

* License: ASSAL v1.1 unless changed by the repository owner.
* Do not alter license headers or license terms casually.
* Do not copy Aestra code into incompatible licenses.
* Do not import incompatible third-party code.
* Do not commit secrets, credentials, private keys, signing material, tokens, paid assets, private models, or proprietary SDK blobs.

Security-sensitive areas include:

* License verification
* Update checks
* Plugin loading
* File parsing
* Project loading
* Path handling
* Archive/extract logic
* Network access
* Crash recovery
* Export/write paths

Vulnerability reports should go to:

```text
security@aestra.studio
```

Use subject format:

```text
SECURITY: [summary]
```

---

## 19. Plugin and Host Rules

When touching plugin hosting or internal plugins:

* Preserve stable plugin IDs.
* Preserve stable parameter IDs.
* Preserve saved state compatibility.
* Keep plugin processing real-time safe.
* Validate bypass, reset, prepare, and sample-rate changes.
* Treat third-party plugin input as untrusted.
* Do not let plugin crashes corrupt project state.
* Do not make premium/internal plugins required for public core builds.

Known internal plugin IDs may include:

```text
com.Aestrastudios.sampler
com.Aestrastudios.eq
com.Aestrastudios.comp
com.Aestrastudios.verb
com.Aestrastudios.delay
```

Do not rename IDs without explicit migration work.

---

## 20. Export/Bounce Rules

Offline export must stay behaviorally aligned with live engine rendering unless a task explicitly changes policy.

When touching export:

* Validate render path parity.
* Validate track routing.
* Validate plugin state use.
* Validate silence and empty-project behavior.
* Validate file write errors are reported clearly.
* Avoid using UI-only state as the source of truth.
* Do not leave realtime streams active during unsafe offline rendering if current policy suspends them.

Always report whether live/export parity was affected.

---

## 21. Git and PR Hygiene

### Commits

* Keep commits surgical.
* Group by concern.
* Do not mix formatting-only changes with functional changes unless requested.
* Do not include unrelated cleanup.
* Do not commit generated files unless they are expected artifacts.
* Do not commit local machine paths, caches, or runtime noise.

### Pull Requests

PRs should include:

* Summary
* Why
* Testing performed
* Docs updated?
* Risk/rollback notes

Do not merge bot-generated PRs blindly.

### Branch Protection

Never delete or rewrite:

* `main`
* `develop`

Treat both as protected even if local Git allows the operation.

---

## 22. CodeRabbit and Review Tools

CodeRabbit is expected to be assertive.

Do not override or dismiss its findings without technical justification.

Acceptable responses to review findings:

* Fix the issue.
* Explain why it is a false positive.
* Add a targeted suppression with a comment.
* Open a follow-up issue if the fix is real but out of scope.

Unacceptable responses:

* Broadly suppressing an entire category.
* Removing the reviewed code path without understanding it.
* Claiming the review is wrong without evidence.
* Hiding the issue behind fallback logic.

---

## 23. Agent Decision Rules

When choosing between options:

Prefer:

* Small patches over rewrites
* Existing patterns over new frameworks
* Explicit errors over silent failure
* Deterministic behavior over cleverness
* Headless validation over UI-only validation
* Compatibility over convenience
* Clear diagnostics over vague success
* Real test evidence over assumptions

Avoid:

* Large speculative refactors
* “While I’m here” cleanup
* Premature abstraction
* UI-only fixes for backend bugs
* Backend-only fixes that leave UI state misleading
* Serialization changes without migration
* Performance claims without measurements

---

## 24. Public Roadmap and Status Claims

Do not add or update public claims about:

* Release dates
* Beta status
* Pricing
* Premium features
* Cloud features
* Benchmarks
* Supported platforms
* Plugin compatibility
* Security guarantees
* “Production-ready” status

unless explicitly requested and supported by repository evidence.

Status belongs in dedicated docs or reports, not in this file.

---

## 25. Known Framework Debt

### NUIPlatformBridge `NUIMouseEvent::type` — FIXED

`NUIMouseEvent::type` is now populated correctly by `AestraUI/Platform/NUIPlatformBridge.cpp` and `Source/Core/AestraWindowManager.cpp` as of 2026-05-11. Use freely.

Historical context: the field was previously left as `NUIMouseEventType::None` at every construction site, causing components that checked `event.type` to silently fail. The following components were affected and have been verified:

* `MembershipSettingsPage` — now uses `event.pressed` directly (works regardless).
* `AestraHistoryPanel` — `event.type` checks for Move/Scroll now fire correctly.
* `NUIApp::handleMouseEvent` — adaptive FPS type checks now match correctly.
* `UnitRow` — step-editing type checks are now functional (redundant with `pressed`/`released` but no longer dead).
* `PluginBrowserPanel` — drag type check remains dead (`Drag` is not emitted by the bridge), but the `button == Left` path covers actual drag logic.

Both `event.type` and `event.pressed`/`released` patterns are valid going forward.

---

## 26. DO

* Use `Aestra_CORE_MODE=ON` for public-facing builds.
* Use `AESTRA_HEADLESS_ONLY=ON` when UI is not needed.
* Gate hardware/device tests behind `AESTRA_ENABLE_RUNTIME_TESTS`.
* Match docs to actual code and CI behavior.
* Keep changes surgical.
* Review automation output like human code.
* Use clang-format and clang-tidy where practical.
* Check existing components before adding new ones.
* Preserve protected branches.
* Report exact tests and commands.
* Be honest about skipped validation.
* Fail loudly on dangerous states.
* Protect realtime audio constraints.

---

## 26. DON'T

* Do not blindly merge bot PRs.
* Do not invent unsupported process, release, benchmark, or roadmap claims.
* Do not add global AVX/AVX2/AVX512 flags.
* Do not commit runtime state changes unless explicitly requested.
* Do not use or revive dead code casually.
* Do not make blocking calls in the audio thread.
* Do not allocate memory in audio callbacks.
* Do not override review findings without justification.
* Do not use hidden automation-only feature flows.
* Do not weaken secret scanning.
* Do not modify FreeType warning suppressions without explicit reason.
* Do not delete `main` or `develop`.
* Do not claim success without evidence.

---

## 27. Versioning and Tagging Policy

Aestra uses semantic versioning with phase suffixes.

### Version Scheme

```text
v0.MINOR.PATCH-alpha    — active development (current phase)
v0.MINOR.PATCH-beta     — public beta (target: Dec 2026)
v1.0.0                  — initial public release
```

MINOR increments on meaningful milestones: new plugin tiers, major engine work,
major architecture changes, or hardening milestones.
PATCH increments on hotfixes or minor maintenance between milestones.

### Tag Rules

* All release tags must be **annotated** (`git tag -a`), never lightweight.
* Annotated tags must include a real message summarizing the milestone.
* Tags live on `main` only — cut after a milestone branch has been merged.
* Do not tag mid-feature, mid-fix, or on merge-conflict-resolution commits.
* Do not create premature major-version tags (e.g. `v1.0.0` before beta).

### Creating a Tag

```bash
git tag -a v0.4.0-alpha -m "Hardening milestone: security audit, audio quality session, repo hygiene"
git push origin v0.4.0-alpha
```

### Existing Tags

| Tag                  | Status   | Notes                                     |
| -------------------- | -------- | ----------------------------------------- |
| `v0.1.0-foundation`  | Keep     | Historical engine foundation, Oct 2025    |
| `v0.1.0-alpha`       | Deleted  | Redundant with foundation tag             |
| `v1.0.0`             | Deleted  | Premature — do not recreate until release |
| `v0.4.0-alpha`       | Current  | Hardening milestone, May 2026             |

### What Agents Must Not Do

* Do not create or delete tags without explicit instruction.
* Do not push tags.
* Do not use lightweight tags for milestones.
* Do not recreate deleted premature version tags.

---

## 28. Nightly Builds

Nightly builds run on a schedule via `nightly.yml` and are the primary canary
for regressions not caught by push/PR CI.

### Schedule

`nightly.yml` runs daily at 2:00 AM EAT (23:00 UTC) on `develop`.

### Scope

* Headless build only (`AESTRA_HEADLESS_ONLY=ON`, `Aestra_CORE_MODE=ON`)
* Full test suite via `ctest`
* ASan + UBSan enabled (`RelWithDebInfo` build type)
* Build artifacts retained for 7 days

### On Failure

Nightly failure automatically opens a GitHub issue labeled `nightly-failure`
with a link to the failing run. Do not suppress or close these issues without
resolving the underlying cause.

### What Agents Must Not Do

* Do not remove the `nightly-failure` issue label or auto-open logic.
* Do not add a schedule trigger to `ci.yml` — nightly scope belongs in `nightly.yml`.
* Do not reduce artifact retention below 7 days without explicit approval.
* Do not disable ASan/UBSan on nightly without explicit approval and a written reason.

### What Agents May Do

* Add additional nightly steps (e.g. macOS matrix, DSP benchmarks) when explicitly tasked.
* Adjust cron timing when explicitly asked.
