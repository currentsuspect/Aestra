# Aestra Automation Policy

This file documents the public automation expectations for this repository.

## Scope

- Automation should keep changes small, reviewable, and aligned with the current public roadmap.
- Public contributors and bots should prefer matching docs to the real repository state instead of inventing new process or release claims.
- Feature work should land through normal review, not through hidden automation-only flows.

## Pull Request Hygiene

- Do not merge bot-generated pull requests blindly.
- Review automation output the same way you would review a human contribution.
- Reject misleading suppressions, generated artifacts that should not be tracked, and claims that are not supported by the code or tests.

## Repo Expectations

- Keep build, test, and contributor docs aligned with the real CMake options and GitHub Actions workflows.
- Update public docs when contributor workflow, CI posture, or release-status language changes.
- Prefer surgical commits grouped by concern.
- Recording pipeline now supports armed capture, take commit, monitoring modes, project-relative recordings, and input diagnostics.
- `Loop -> Project` has empty-project fallback (16 bars). Loop extent is set on selection, not auto-updated on clip changes — reselect "Project" to recalculate. This is acceptable for v1 Beta.
- Piano Roll ↔ Arsenal sync is complete: double-click unit opens Piano Roll with unit's pattern, edits save back and refresh Arsenal minimap. Minimap shows pitch-based note bars with grid lines, octave labels, and scroll-wheel pitch viewport.
- Arsenal audio leakage is prevented: `processArsenalUnits()` returns early in Timeline mode (`m_patternPlaybackMode` guard at AudioEngine.cpp:2459).
- Offline export now renders through the live engine path and temporarily suspends the realtime stream during export.
  - Last validated: 2026-04-13 (Piano Roll ↔ Arsenal sync, minimap, audio leakage verified)

## Freshness

Last reviewed: 2026-04-13 by Hermes.

---

# AI Agent Handbook

This section is written for AI agents (Codex, Cursor, Copilot, CodeRabbit, etc.) working in this repository. Read it before making any changes.

## Project Architecture

Aestra is a C++17 DAW (Digital Audio Workstation) with a modular architecture:

```
Aestra (executable)
├── AestraCore        — Foundation: math, threading, logging, config, profiler
├── AestraPlat        — Platform abstraction: windowing, OpenGL, input (Win32, Linux/X11+SDL2)
├── AestraAudio       — Real-time audio engine: RtAudio, DSP, mixer, plugin hosting (VST3/CLAP)
├── AestraUI          — Custom GPU-accelerated UI framework (OpenGL 3.3+)
├── AestraPlugins     — Premium plugins (AestraRumble 808 synth)
├── AestraSDK         — Plugin/extension API (planned for v3.0, currently empty)
├── Source            — Main DAW application
├── Tests             — Centralized test suite
├── aestra-core       — Public core-mode variant (stub/thin wrapper for builds without premium modules)
├── docs/             — MkDocs Material documentation portal
├── AestraDocs/       — Internal design docs, architecture notes, vision docs
└── scripts/          — Build, install, audit, and utility scripts (~26 files)
```

### Dependency Graph

```
Aestra ─┬─ AestraCore
        ├─ AestraPlat ──> AestraCore
        ├─ AestraUI_Core
        ├─ AestraUI_Platform ──> AestraUI_Core, AestraPlat
        ├─ AestraUI_OpenGL ──> AestraUI_Core, glad, FreeType
        ├─ AestraAudio (interface) ──> platform-specific backend
        └─ AestraAudioCore ──> AestraCore, AestraPlat
```

### External Dependencies (git submodules)

| Dependency | Path | Purpose |
|---|---|---|
| VST3 SDK | `AestraAudio/External/vst3sdk` | Plugin hosting |
| FreeType | `AestraUI/External/freetype_local` | Text rendering |
| SDL2 | `external/SDL2` (branch release-2.30.x) | Linux windowing, input |
| RtAudio | `AestraAudio/External/rtaudio` | Cross-platform audio I/O |
| miniaudio | `AestraAudio/External/miniaudio` | Audio decoding (MP3, FLAC) |
| CLAP SDK | `AestraAudio/External/clap` | Plugin hosting (header-only) |

**Critical:** Submodules are NOT checked out in CI (`submodules: false`). VST3-dependent code must be conditional on SDK presence.

## Build System

### CMake Configuration

- Minimum version: 3.22
- Language: C++17 (required, not optional)
- Runtime output: `build/bin` (or `build/bin/<Config>` for multi-config generators)

### Key CMake Options

| Option | Default | Purpose |
|---|---|---|
| `Aestra_CORE_MODE` | ON | Build without premium modules, use mock assets |
| `AESTRA_HEADLESS_ONLY` | OFF | Skip UI targets, container/CI-friendly |
| `AESTRA_ENABLE_UI` | ON | Build the desktop UI application |
| `AESTRA_ENABLE_TESTS` | ON | Build test executables |
| `AESTRA_LOW_MEMORY_BUILD` | OFF | Memory-saving compilation for 4GB RAM machines |
| `AESTRA_ENABLE_RUNTIME_TESTS` | OFF | Enable device-dependent integration tests |
| `AESTRA_ENABLE_EXPERIMENTAL_TESTS` | OFF | Enable unstable tests |

### CMake Presets (`CMakePresets.json`)

| Preset | Use Case |
|---|---|
| `headless` | No UI, container/CI builds |
| `full` | Full app with UI + tests |
| `full-fast` | Full app, tests OFF |
| `lowmem` | 4GB RAM machines (disables LTO, vectorization, uses -O2) |

### Build Commands

**Standard build (use this by default):**
```bash
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

**Headless build (CI-safe, no UI dependencies):**
```bash
cmake -S . -B build-headless -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless --parallel
```

**Using presets:**
```bash
cmake --preset headless && cmake --build --preset headless-release
```

**Run tests:**
```bash
ctest --test-dir build --output-on-failure
```

### Build Gotchas

- **`AESTRA_LOW_MEMORY_BUILD`** must be set BEFORE `project()` in root CMakeLists.txt
- **AVX-512**: `SincAVX512.cpp` is compiled with `-mavx512f -mavx512dq` on x86_64 only, skipped on ARM
- **AVX-2**: NOT enabled globally — uses per-TU `#pragma` in `SincAVX2.h` for safe runtime dispatch. **Never add global `/arch:AVX2` or `-mavx2`** — it will crash on non-AVX2 CPUs
- **Circular dependency on Linux**: `AestraAudio` and `AestraAudioCore` use `-Wl,--start-group ... -Wl,--end-group` for GNU ld
- **RtAudio sources**: Only compiled into `AestraAudioWin` or `AestraAudioLinux`, NOT into `AestraAudioCore`
- **FreeType deprecation warnings**: Temporarily suppressed during `add_subdirectory(External/freetype_local)` — do not "fix" these suppressions

## Code Conventions

### Formatting (`.clang-format`)

- Base style: LLVM
- Indent: 4 spaces, no tabs
- Column limit: 120
- Braces: Attach style
- Pointer alignment: Left (`int* ptr`)
- Includes: Sorted and regrouped
- Max empty lines: 1

### Static Analysis (`.clang-tidy`)

- Enabled: `bugprone-*`, `cppcoreguidelines-*`, `modernize-*`, `performance-*`, `portability-*`, `readability-*`
- Header filter: `Aestra(Audio|Core|Plat)/.*` (excludes External)
- Standard: C++17

### Naming Patterns

| Element | Convention | Example |
|---|---|---|
| Classes | `PascalCase` | `AudioEngine`, `MixerBus` |
| Methods | `camelCase` | `openStream`, `createWindow` |
| Members | `m_camelCase` | `m_creatingThreadId` |
| Constants/macros | `UPPER_SNAKE_CASE` | `AESTRA_HEADLESS_ONLY` |
| Files | `PascalCase.h` / `PascalCase.cpp` | `AudioEngine.h` |
| Namespace | `Aestra` (single top-level) | `Aestra::AudioEngine` |

### Include Structure

Headers organized by domain in `AestraAudio/include/`: `Core/`, `DSP/`, `Models/`, `Playback/`, `IO/`, `Drivers/`, `Plugin/`, `Commands/`, `Headless/`. CMakeLists.txt adds all subdirectories to include paths.

## Audio Engine Rules

**These are non-negotiable for real-time audio code:**

- **No blocking calls in the audio thread** — ever
- **Lock-free communication only** — use SPSC ring buffers for UI-to-audio commands
- **No memory allocation in the audio callback**
- **Target: <10ms latency**
- Audio callback chain: `RtAudioCallback -> AestraAudioEngine -> MixerBus -> OutputBuffer`

## Test Suite

### Test Tiers

| Tier | Flag | Tests |
|---|---|---|
| Always registered | (none) | `CommandHistoryTest`, `MoveClipCommandTest`, `MacroCommandTest`, `AestraOscillatorTest`, `AestraMixerBusTest`, `AestraAtomicSaveTest` |
| Runtime-gated | `AESTRA_ENABLE_RUNTIME_TESTS=ON` | `AestraAudioTest`, `AestraAudioSoakTest`, `AestraAudioDriverSoakTest`, `ProjectRoundTripTest`, `AutosaveRoundTripTest`, `RumblePluginFactoryTest`, `RumbleUsagePathTest`, `RumbleDiscoveryTest`, `RumbleStateTest`, `InternalPluginProjectRoundTripTest`, `ArsenalInstrumentAttachmentTest`, `ArsenalParameterStressTest`, `AestraDeviceManagerTest` (Win32) |
| Experimental-gated | `AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON` | `AestraWaveformLockTest`, `AestraWaveformCacheTest`, `AestraAudioCallbackTest`, `AestraFilterTest`, `AestraSampleRateConverterTest` |

**CI runs only the always-registered tier.** If you add a test that requires hardware or runtime state, gate it behind `AESTRA_ENABLE_RUNTIME_TESTS`.

## Known Issues and Dead Code

- **`SelectionModel.cpp`** is commented out in CMakeLists.txt — TODO: rewrite for new PlaylistModel API. Do not try to use or fix it without understanding the new API.
- **`SelectionModel.h`** is commented out — references non-existent `PlaylistClip.h`.
- **`AestraSerializationCompatibilityTest`** is fully commented out in `Tests/CMakeLists.txt`.

## Runtime State Files

These files represent runtime state and are committed to the repo. **Do not modify them** unless explicitly asked:

- `audio_settings.conf` — audio driver/device/samplerate config
- `browser_settings.json` — file browser state (contains Windows paths)
- `aestra_profile.json` — Chrome trace format profiler output
- `autosave.aes` — autosave project file
- `crash_flag` — runtime crash flag
- `runtime_log.txt` — runtime log
- `build_log.txt` — binary build log

## CI/CD Workflows

| Workflow | Triggers | Platforms | Blocking? |
|---|---|---|---|
| `ci.yml` | push/PR to main, develop | Linux, Windows, macOS (advisory) | Yes (Linux+Windows) |
| `public-ci.yml` | push to main only | Windows | Yes |
| `api-docs.yml` | *.h/*.cpp/Doxyfile changes | Linux | No (advisory) |
| `docs-check.yml` | PR touching docs/*.md | Linux | No |
| `deploy-docs.yml` | push to main/develop touching docs/ | Linux | No |
| `gitleaks.yml` | push to main, develop | Linux | Yes |
| `gitleaks-pr.yml` | PR opened/synchronize/reopened | Linux | Yes |
| `private-release.yml` | workflow_dispatch (manual) | Windows 2022 | N/A |

All CI builds use `-DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_UI=OFF`.

## Documentation

- **Public docs**: `docs/` — MkDocs Material site, deployed to GitHub Pages
- **Internal docs**: `AestraDocs/` — design docs, architecture notes, vision docs
- **API docs**: Generated by Doxygen (`Doxyfile`), uploaded as CI artifacts
- Markdown supports: admonitions, tabs, task lists, details, Mermaid diagrams, emoji, highlights
- Custom CSS: glassmorphism nav, "Studio" code theme with cyan cursor glow

## Security

- **License**: ASSAL v1.1 (source-available, NOT open-source). Code can be studied and contributed to, but cannot be used/copied/modified/redistributed without permission.
- **Secret scanning**: Gitleaks configured (`.gitleaks.toml`). Never commit keys, tokens, or credentials.
- **Vulnerability reports**: Email `security@aestra.studio` with subject `SECURITY: [summary]`. 72-hour acknowledgment SLA.
- Aestra runs locally; no internet access except optional update checks.
- Data-loss bugs, crash loops, and performance regressions are high-priority.

## GitHub Contribution Conventions

### Issue Templates

- **Bug reports**: Title prefix `[BUG] `, labels `bug`, requires environment info, severity, affected layer
- **Feature requests**: Title prefix `[FEATURE] `, labels `enhancement`, requires alignment with Aestra philosophy (Intentional Minimalism, Performance as Aesthetic, Clarity Over Decoration, No Borrowed Parts)

### Pull Requests

- Template sections: Summary, Why, Testing Performed, Docs Updated?, Risk/Rollback Notes
- Keep changes small, reviewable, and aligned with the public roadmap
- Prefer surgical commits grouped by concern

### CodeRabbit

- Profile: ASSERTIVE (detailed reviews)
- Auto-review enabled, auto-incremental, max 500 files
- Ignores: build dirs, minified files, `External/**`, `subprojects/**`
- Will request changes for issues — do not override its findings without justification

## DO and DON'T

### DO

- Use `Aestra_CORE_MODE=ON` for all public-facing builds
- Use `AESTRA_HEADLESS_ONLY=ON` when UI dependencies are not needed
- Gate hardware-dependent tests behind `AESTRA_ENABLE_RUNTIME_TESTS`
- Match docs to the real repository state
- Keep commits surgical and grouped by concern
- Review automation output the same way you would review a human contribution
- Use clang-format and clang-tidy before committing
- Check existing components and patterns before introducing new ones
- Preserve the `main` and `develop` branches — they must never be deleted

### DON'T

- Blindly merge bot-generated pull requests
- Invent new process or release claims not supported by the code
- Add global AVX flags (`/arch:AVX2`, `-mavx2`) — use per-TU pragmas only
- Commit runtime state files (audio_settings.conf, browser_settings.json, etc.)
- Try to use or fix `SelectionModel` without understanding the new PlaylistModel API
- Make blocking calls in the audio thread
- Add memory allocation in audio callbacks
- Override CodeRabbit findings without justification
- Use hidden automation-only flows for feature work
- Modify FreeType warning suppressions
- Delete the `main` or `develop` branches under any circumstances
