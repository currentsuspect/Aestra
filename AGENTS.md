# AGENTS.md — Aestra Automation Policy

This file defines how AI agents, bots, and automation tools should work inside the Aestra repository.

## Philosophy

Before working on Aestra, read [`philosophy.md`](philosophy.md). It defines the product vision, the people we build for, and the engineering values that underpin every decision here. When this policy and philosophy conflict on direction, philosophy wins. When this policy and philosophy conflict on safety, this policy wins.

---

## 1. Scope

This policy applies to any bot-generated or AI-assisted contribution.

Agents must keep changes small, reviewable, factual, and aligned with the real repository state.

---

## 2. Non-Negotiable Rules

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
```

### Branching

- Branch from `develop`.
- Never commit directly to `main` or `develop`.
- Merge flow: `feature/*` → `develop` → `main`.

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

Correct for audio, DSP and plugin work, and it is what most changes need. It does
**not** compile `Source/Core/` or `AestraUI/` — see the `AESTRA_CI` note in §8.

### UI/App Build — required for `Source/` and `AestraUI/` changes

```bash
# No AESTRA_CI and no AESTRA_HEADLESS_ONLY: both force-disable the UI.
cmake -S . -B build-ui \
  -DAESTRA_ENABLE_TESTS=ON \
  -DAESTRA_ALLOW_LICENSE_GATE_OFF=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-ui --parallel
```

```bash
# The single target that proves the app links. Cheaper than a full UI build and
# catches the whole Source/ tree.
cmake --build build-ui --target Aestra --parallel
```

> [!gotcha] Two builds, for two kinds of change
> An agent that verifies UI work with the headless recipe has verified **nothing**,
> because the file was never compiled. This is not a theoretical hazard: PR #1025
> reached review with twelve compile errors in `Source/Core/AestraContent.cpp` and
> every local build reported it clean. Run the headless build for the test suite,
> and additionally run this one whenever the diff touches `Source/` or
> `AestraUI/`.

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
* **`AESTRA_CI=ON` force-disables the UI.** It sets `AESTRA_HEADLESS_ONLY=ON` with
  `CACHE ... FORCE`, which sets `AESTRA_ENABLE_UI=OFF`. So
  `-DAESTRA_CI=ON -DAESTRA_HEADLESS_ONLY=ON` is redundant *and* self-defeating if
  you meant to build the app: the second flag looks like the only one that matters,
  and the first silently forces it. Drop `AESTRA_CI` for UI work (§7).
* **`AESTRAUI_ENABLE_PREMIUM_EDITORS` changes array sizes at compile time.** A
  `#ifdef` inside a `std::array` initializer means the declared size must be
  derived, not written — a hardcoded size compiles the default build and breaks the
  premium one. Verify premium with `-DAESTRAUI_ENABLE_PREMIUM_EDITORS=ON` when a
  private plugin is present.
* Agent scratch written into the source tree is gitignored: `msg*.txt` and
  `commitmsg.txt`. `git add -A` sweeps the file you just wrote your commit message
  into, so use `git commit -F` and delete the file, or it lands in the repository.

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
* `labs/` for experiments, generated evidence, quality reports, and benchmark artifacts.

**Internal notes do not live in this repository.** Architecture reports, design
notes, implementation plans, audits, status documents, product strategy, and
roadmaps belong in the private **Aestra-Internals** vault, not here. This repo is
public: anything committed to it is world-readable forever, including in history.

If a change needs internal write-up, record it in Aestra-Internals and reference
it by title from the PR description. Do not recreate `AestraDocs/`.

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

### Adding a built-in effect

A built-in plugin is now **DSP + a ParamSpec table + one registration line**.
`AestraAudio/include/Plugin/InternalPluginBase.h` owns the parameter array, the
clamp and non-finite guards, `getParameters()`, the state blob, and the
editor and watchdog stubs. `AestraSat.h` is the reference implementation.

```cpp
class AestraFoo : public InternalPluginBase {
    inline static constexpr ParamSpec kSpecs[] = {
        {kDrive, "Drive", "DRV", "dB", 0.25f, 0.0f, 1.0f, true},
        {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
    };
    const ParamSpec* paramSpecs() const override { return kSpecs; }
    uint32_t paramSpecCount() const override { return kParamCount; }
    uint32_t stateMagic() const override { return kStateMagic; }
    // ... initialize() calling seedDefaultsOnce(), and process()
};
```

Register it in `AestraAudio/src/Plugin/BuiltInPlugins.cpp`. The plugin ID is
permanent; choose it once.

Rules for the rest:

* Call `seedDefaultsOnce()` from `initialize()`. The base gates it to the first
  initialize, so a re-prepare preserves the user's parameters and loaded state.
  A plugin that skips it starts with every parameter at `0.0f`.
* Read parameters with `paramValue(id)` and `isBypassed()`; do not reach for the
  storage directly.
* Override `getParameterDisplay()` only when labels are in musician units.
  Override `onParameterChanged()` only to invalidate derived state.
* **The inherited state blob is `{magic, version=1, params[count]}` sized by
  `paramSpecCount()` — byte-identical to what the older plugins shipped.** That
  is what makes inheriting it safe. If your blob needs fields beyond the
  parameters, keep your own `saveState()`/`loadState()` and do not inherit.
  `AestraEQ` is the worked example, and it carries eight blob versions; a
  ParamSpec table does not remove that debt.
* **Bypass must be a pure delay of `getLatencySamples()`.** `EffectChain::getTotalLatency()`
  counts plugin latency and cannot see the plugin's own Bypass knob, so a
  straight copy in bypass places the plugin early against everything downstream
  in a delay-compensated chain. `AestraSat` shows the pattern.
* Report the latency the path **actually** delays, not the one you intended.
  An impulse probe is how you check.

Generic contracts need no tests of their own: `PluginConformanceSweepTest` loops
`InternalPluginRegistry`, so registering a plugin gets it 11 contracts —
prepare matrix, silence in/silence out, hostile input, parameter table,
non-finite rejection, state round-trip, garbage state, latency stability,
bypass alignment, zero steady-state allocations, reset idempotence. **Write
tests only for what is specific to the plugin** (its material lab, its quality
measurements), and append the new target to the end of
`Tests/cmake/PluginTests.cmake`, which is append-only so parallel branches do not
collide on it.

`.claude/` is gitignored in this repository, which is why this section lives
here rather than in a skill file: an agent that checks out Aestra gets AGENTS.md
and nothing else.

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

## 21. Pull Requests

PRs should include:

* Summary
* Why
* Testing performed
* Producer note
* Docs updated?
* Risk/rollback notes

### Producer note — required, defaults to `None`

This field is consumed by a **different repository**. The public
changelog on aestra.studio is built from it: `Aestra-website` runs
`npm run changelog:draft`, which harvests every `## Producer note`
block from PRs merged since the last release and prints a draft entry.

Write one sentence a producer would understand, or `None`.

* Test: finish **"now you can ..."** or **"X no longer ..."**.
  If neither sentence works, it is internal — answer `None`.
* `None` is the correct answer for refactors, test wiring, CI, dead
  code removal and internal renames. Most PRs are `None`.
* Rough wording is fine. It gets edited at release time.

```text
"Hitting pause no longer rewinds you."        <- ships
"RMS detection with parameter smoothing"      <- fails the test, use None
```

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

## 23. Versioning and Tagging Policy

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

### Release History

See `RELEASES.md` for the current tag inventory, milestone history, and release status.
Do not duplicate that table in this file.

---

## 24. Nightly Builds

### Schedule

`nightly.yml` runs daily at 2:00 AM EAT (23:00 UTC) on `develop`.

### Scope

* Headless build only (`AESTRA_HEADLESS_ONLY=ON`, `Aestra_CORE_MODE=ON`)
* Checks out `develop` explicitly (scheduled workflows run from the default branch by default)
* Test suite via `ctest`, excluding `SecOutOfProcessPluginHost|SecPluginScanIsolation|SecAccountApiClient` (same as ci.yml sanitizer job)
* ASan + UBSan enabled (`RelWithDebInfo` build type) with linker flags and runtime options aligned to ci.yml's sanitizer job
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
* Do not remove the `ref: develop` from the checkout step — scheduled workflows default to the repository's default branch, not develop.
* Do not remove the ctest `-E` exclusion filter without confirming those tests pass under ASan/UBSan.
