# Changelog

All notable changes to this project will be documented in this file.

The public repository is currently on a `0.x` pre-beta line. The release target tracked in the roadmap is **v1 Beta in December 2026**.

## v0.4.0-alpha — Hardening Milestone (2026-05-20)

### Security
- Full security audit — 11 findings, 8 fixed, 3 deferred with justification
- Project load path hardened (structure, numeric, path, compression bomb validation)
- Crash recovery hardened with opaque flag file and stale backup skip
- Archive extraction path traversal protection
- `MixerSettingsSerializationTest` disabled (heap exploitation vector)

### Audio Quality
- BS.1770 K-weighting for true-peak metering
- Proper TPDF dither for 16-bit/24-bit export
- Export quantization uses `std::lrint` (was truncation)
- Denormal protection via `AESTRA_FTZ_MANUAL`
- Removed Boost dependency

### RT Safety
- `GarbageCollector::flush()` lock-free
- GC retirement from audio thread via SPSC ring buffer
- `Mixer::m_effectChainSnapshot` race fixed
- `fadeOutActive` atomic (was torn)
- `EngineSupervisor` for audio thread liveness
- `RTGuard` for RT-safety violation detection
- `AsyncCleanupManager` for deferred resource cleanup

### Build / CI
- Nightly build workflow (`nightly.yml`) with ASan/UBSan, auto-issue on failure
- Versioning/tagging policy (AGENTS.md §27) and nightly policy (AGENTS.md §28)
- macOS CI test exclusions aligned (#229, #230)
- CodeRabbit review fixes (#236, #237)

### Known Issues (P0 beta-blockers)
- OOP plugin parameters are no-ops (#238)
- Autosave serializer data race (#239)
- Routing: gain smoothing, cycle detection, RT allocation (#240, #241, #243)
- CLAP MIDI input unimplemented (#244)
- Full list: 69 issues at https://github.com/users/currentsuspect/projects/3

---

## v0.5.0-alpha — Feature & CI Milestone (2026-05-23)

11 PRs merged (#291–#305). Multi-take recording system, CLAP parameter support, audio quality fixes, CI hardening.

### Takes System

- Multi-take recording with manifest, snapshots, transactional switching
- Path traversal guards on take snapshot paths
- UI integration for take management

### CLAP Hosting

- `ClapParamInfo` and `ClapPluginParams` structs for CLAP parameter enumeration
- CLAP core constant definitions
- Null `paramsExt` fix — prevents crash when plugin lacks parameter extension

### Audio Quality

- K-weight race fix — metering filter state no longer torn under concurrent access
- ARM64 denormal handling — `fpcr` register access guarded for MSVC ARM64
- Send gain smoother coefficient fix — correct smoothing time constant
- Audition queue deadlock fix — lock ordering corrected
- Autosave atomic rename — crash-safe save via POSIX rename

### CI / Build

- Removed `jwlawson/actions-setup-cmake@v2` from all CI jobs — uses system CMake
- DelayLine off-by-one fix — `Capacity+1` buffer allocation
- Windows path separator fix in test fixtures
- 13 previously unregistered test files added to CMakeLists
- Platform guards for Windows/Linux/macOS-specific code paths

### Documentation

- v0.5.0-alpha added to RELEASES.md

---

## v0.6.0-alpha — Security & RT Hardening (2026-05-29)

26 PRs merged. Security hardening across the plugin host, license gate, and project loader. RT-safety fixes for waveform callbacks, preview decodes, mixer state, and the master limiter. Plugin host crash resilience and non-finite output quarantine. Callback-safety architecture (triple-buffer graph, routing snapshot, PDC edge ownership, TSan CI). Sprint 2 tracking issues resolved.

### Security

- Take snapshot paths confined within project `.takes` directory — traversal regression coverage (#338)
- Scanned/cache plugins can no longer shadow registered internal plugin IDs (#338)
- Hard-coded dev account API fallback removed — stale/future/premium lease rejection in default builds (#338)
- Account refresh `issued_at` handling hardened; private release workflow token exposure fixed (#338)
- Production key required for premium leases (#339)
- Arsenal project plugin restore restricted to registered IDs only (#340)
- CLAP scanner helper writes guarded from SIGPIPE (#365)
- Nightly workflow token permissions limited to minimum required (#347)

### RT Safety

- Stale waveform source callbacks eliminated — preview engine no longer holds dangling lambdas (#341)
- Async preview decodes bounded — prevents unbounded work accumulation on the audio thread (#342)
- Audition decode worker lifetime owned explicitly — no more race on teardown (#343)
- Render main output slot lookup cached per graph compile — removes per-block map lookup (#346)
- Mixer lane state clamped before cast — prevents undefined values from reaching the audio path (#345)
- OpenGL kerning cache bounded — prevents unbounded growth in glyph atlas (#350)
- MSVC ARM64 denormal register access made safe — no more direct `fpcr` manipulation (#344)
- AestraVerb active state rebuilds guarded — prevents spurious re-initialization during bypass transitions (#360)
- Graph dirty UI signal guarded — prevents UI-triggered graph rebuilds from racing the audio thread (#358)
- Master safety limiter reshaped to transparent cubic Hermite knee (0.98 → 0.9997 ceiling clamp) — never boosts input, preserves recovery compatibility (#367)

### Plugin Hosting

- VST3 host crash handling hardened — plugin helper runs real VST3 processing instead of stub (#334)
- Plugin proxy watchdog state made atomic — prevents torn reads under concurrent access (#334)
- Non-finite plugin output quarantined — NaN/Inf detected and reported instead of propagating (#333)
- Effect-chain fault state ownership fixed — fault markers scoped correctly to prevent cross-channel bleed (#333)

### Callback Safety

- Triple-buffer `EngineState` with `GraphReadHandle` protecting RT graph access (#310)
- `RecordingCaptureRoute` double-buffered snapshot — input capture reads immutable snapshot instead of live state (#310)
- PDC edge state ownership fixed — latency compensation no longer races graph recompile (#310)
- TSan CI added for callback-safety regression detection (#310)

### Serialization / Project Persistence

- Pattern restore and timeline persistence fixed — patterns survive project round-trips correctly (#368)
- BPM changes synced through playlist/timeline/pattern playback — audio clip duration preserved in seconds (#368)
- Arsenal sampler audio restored from `audioClipPath` when legacy plugin state lacks `samplePath` (#368)
- Audio clip placement hardened — imports fail loudly instead of leaving orphan patterns (#368)
- Migrated round-trip payload values asserted — migration framework validated against v1 fixtures (#332)
- v1 project migration roundtrip proven — `ProjectRoundTripIntegrityTest` passing (#332)

### Recovery

- Autosave crash session binding — recovery autosaves tagged to crash session markers
- Session recovery test fixed on Windows — path separator handling corrected

### CI / Build

- LeakSanitizer (LSan) advisory CI job added (#312)
- API docs quality workflow Graphviz dependency fixed (#337)
- GitHub Pages deploy gate changed from opt-in to opt-out — deploy fires on every main push unless `DISABLE_GITHUB_PAGES` is set to `'true'` (#369)

### Sprint 2 Tracking Issues Resolved

- `std::atomic_load` on `shared_ptr` deprecated — replaced with `std::atomic<std::shared_ptr>` or direct member access (#331)
- Plugin crash protection verified Linux-only — Windows/macOS behavior documented (#334)
- Plugin NaN/Inf validation moved to master output — per-plugin validation planned for post-beta (#333)
- Migration framework proven with v1 fixture roundtrip (#332)
- fsync absence in write paths documented — deferred to post-beta (#335)

### Documentation

- Issue audit system Phase 1a/1b/1c — taxonomy review and priority alignment (#308)
- Doxygen coverage for callback-safety public API (#311)

### Known Issues (as of v0.6.0-alpha)

- OOP plugin parameters are no-ops (#238) — P0
- Autosave serializer data race (#239) — P0
- Routing gain smoothing, cycle detection, RT allocation (#240, #241, #243) — P0
- CLAP MIDI input unimplemented (#244) — P0
- 5 empty model stub files (#252) — P1
- macOS platform unimplemented (#267) — P1
- Full list: https://github.com/users/currentsuspect/projects/3

---

## v0.7.0-alpha — Routing, Automation & Hosting Milestone (2026-08-16)

331 PRs merged (#624–#771). The story of this milestone is coherence: routing and gain staging are correct end to end, automation targets the instance it was drawn for, the Master channel is a real plugin host whose latency sits in the compensation graph, and the piano roll grew from an editor into a workflow.

> **Versions, clarified.** `v0.7.0-alpha` is the *application release version*. It is unrelated to `ProjectSerializer::PROJECT_VERSION_CURRENT` (the project/file format version, currently 3): the format version changes only when the `.aes` format itself changes, never on a release bump.

### Routing & Mixing Correctness

- Unity gain law end to end: channel strips use the stereo-balance law, so routing a signal through any mixer channel no longer attenuates it — and the same law governs live playback, offline export, isolated bounce, audition preview, and input monitoring (parity pinned by tests).
- Fader and pan applied once: one UI gesture no longer writes two stores that the engine multiplied (−6 dB on the fader used to play at −12 dB).
- Routing command seam with mutation-time cycle rejection, stable send IDs, and master-legality enforcement; render-path semantics match the routing contract (V1/V2/V3) with live/offline parity.
- Master channel is a valid plugin host: insert chains process on the master bus before the fader and safety limiter, persist in the project file, and their latency feeds the plugin-delay-compensation graph (reported project latency is now the real end-to-end delay).
- Master-routed clips obey the live solo gate; isolated-track bounce renders only the selected track's stage and excludes the master stage by design — both halves of the isolation contract are pinned by tests.

### Automation

- Automation targets the instance it was drawn for, never the slot it occupied — chain reordering keeps curves bound to their plugin.
- Empty lanes are automatable: the first click creates a neutral Volume curve bound to the lane's channel; edits rebuild the audio graph and mark the project dirty.
- Leftover demo automation removed from the default project and from previously saved projects.

### Piano Roll & Sequencing

- Selection subdivision by snap, proportional selection stretching, chord-aware stroke input, harmony context persistence, contextual editor workspace, playhead alignment in timeline mode, and bidirectional sampler ping-pong loops.

### UI & Editing

- Design constitution pass across mixer, browser, timeline, and arsenal.
- Audio Clip editor: musical pitching, trim persistence, live waveform updates, resize-safe layout.
- Timeline geometry has a single authority for grid and x→beat conversion.

### Reliability & Hardening

- Security, durability, and realtime contract lanes are CI-authoritative; test contract coverage and founder decision citations are enforced gates.
- Plugin hosting compiles in CI; Windows plugins run sandboxed out-of-process; CLAP SDK vendored.
- The routing/automation/master/UI triage branch went through four review rounds (human + CodeRabbit) with 20 findings addressed before merge.
- License-signing worker dependencies cleaned (undici CVEs).

## [Unreleased]

### Fixed
- Piano Roll: dropdown menu (Scale, Snap, Root Key) now responds to clicks — was using local bounds for positioning but global bounds for hit-testing

### Audio Plugins
- **AestraComp**: Replaced peak detection with RMS — the detector now responds to sustained loudness rather than transients, matching hardware compressor behavior. Uses power-domain IIR smoothing (10ms window).
- **Rumble**: Smooth GlideCurve parameter during active glide — prevents audible discontinuities when curve is adjusted mid-glide.

### Device Resilience (EPIC K)

- **Telemetry wiring**: Driver-level underrun/xrun counters now flow to engine telemetry across WASAPI Shared, WASAPI Exclusive, and RtAudioDriver
- **Health monitor thread**: Automatic driver health polling (500ms interval, 2s stall detection → safety driver fallback)
- **Hot-plug detection**: `IMMNotificationClient` implementation for both WASAPI drivers — device removal/default device change events
- **RT safety**: Removed all `std::cout`/`std::cerr` from WASAPI audio thread loops; added Linux SCHED_FIFO scheduling + mlockall in RtAudioDriver
- **Fallback reasons**: `AudioDeviceManager` now populates descriptive fallback reason strings during driver fallback loop
- **PerformanceHUD**: Enhanced with underrun detail, overruns, consecutive underruns, recovery mode, thread priority status, and last callback timing (% of budget)
- **Driver soak test**: New `AestraAudioDriverSoakTest` exercises real audio driver path with live stream, monitors telemetry, reports every 5s
- **AudioTelemetry fix**: `lastCallbackNs`, `maxCallbackNs`, `lastBufferFrames`, `lastSampleRate` now updated every callback frame (previously always zero)

### Export / Offline Render

- **Rewrote `AudioExporter` from scratch** — the previous implementation was fundamentally broken (never started transport, never advanced position between blocks, hardcoded 60s duration → produced silence).
- Now uses `AudioRenderer::renderBlock()` — the same proven offline rendering path as `bounceRangeToWav()` — ensuring export matches real-time playback.
- **Duration computed from actual playlist** via `PlaylistModel::getTotalDurationBeats()` instead of hardcoded values.
- **Position advances correctly** between render blocks (sample-accurate).
- **Master output stage** applied during export: DC blocking, soft clipping, TPDF dither for PCM — matching the playback signal path.
- **Three render scopes supported**: FullSong, LoopRegion, Selection.
- **Bit depths**: 16-bit PCM, 24-bit PCM (stored in 32-bit containers), 32-bit IEEE float.
- **Progress callbacks** and **cancel support** for UI integration.
- **Wired `File > Export Audio...`** menu item — exports to WAV using project sample rate, 24-bit PCM, with 2s tail for reverb/decay.
- Added `friend class AudioExporter` to `AudioEngine` for safe access to renderer internals.

### Playlist / Clip Editing

- **Fixed clip split bug**: `PlaylistModel::splitClip()` now copies `patternId`, `name`, and `colorRGBA` to the newly created clip half. Previously the second half received a default empty `patternId` (value 0), producing one valid clip + one empty pattern.
- **Wired Cut/Copy/Paste/Delete** to the edit menu and keyboard shortcuts (`Ctrl+X`, `Ctrl+C`, `Ctrl+V`, `Delete`):
  - `cutSelectedClip()` — copies to clipboard and removes via `RemoveClipCommand`
  - `copySelectedClip()` — copies to clipboard
  - `pasteClipboardAtCursor()` — pastes at playhead position
  - `deleteSelectedClip()` — removes without clipboard
- **Split tool** now works via both blade tool click and keyboard shortcut (`S` key), all routed through `SplitClipCommand` for full undo/redo support.

### Piano Roll / Pattern workflow

- **Double-click pattern clips** on the timeline to open them in the Piano Roll panel.
- **Piano Roll unit routing**: notes now carry `unitId` for Arsenal unit routing. New notes inherit the currently selected Arsenal unit.
- **Piano Roll ↔ Sequencer sync**: editing a pattern in Piano Roll refreshes the Sequencer panel. Selecting a unit in Arsenal updates the Piano Roll's editing context.
- `PianoRollPanel` now auto-saves on note changes and displays the active pattern name + unit label.

### Arsenal Panel

- **Unit selection state**: Arsenal now tracks a selected unit, highlights it visually, and broadcasts selection changes to Piano Roll and Sequencer.
- **Remove units**: Delete/Backspace removes the selected unit (minimum one unit enforced). Notes belonging to the removed unit are cleaned from all patterns.
- **Unit row redesign**: rows now show group label (Synth/Drums/Audio), source summary (Plugin/Sample/Empty), and a source tag badge. Selected rows get accent-colored border and shadow.
- **Progress header** now displays the selected unit name and active pattern info.

### Build / CI

- **Low-memory build preset** (`lowmem`): configured for 4GB RAM laptops — 2 parallel jobs, no LTO, no tests, Release mode. Use `cmake --preset lowmem && cmake --build --preset lowmem-release`.

### Internal Arsenal / Rumble milestone

- Added a verified internal instrument validation stack around **Aestra Rumble**.
- Added and validated:
  - `RumbleStateTest`
  - `RumblePluginFactoryTest`
  - `RumbleUsagePathTest`
  - `RumbleDiscoveryTest`
  - `RumbleRenderTest`
  - `RumbleArsenalAudibleTest`
- Strengthened Rumble plugin behavior:
  - aligned parameter defaults with implementation
  - added versioned plugin state blob with magic/version header
  - clarified MVP mono/decay-led note behavior
  - strengthened render regression checks for tail decay and preset differentiation

### Internal plugin architecture

- Added canonical built-in plugin metadata via `BuiltInPlugins` so internal plugin identity no longer drifts across scanner/plugin/runtime code.
- Registered built-in plugins in the normal discovery flow so internal instruments can be found via standard manager lookup.
- Made `PluginManager` headless-safe for internal plugin workflows by treating platform-utils/cache access as optional in headless mode.

### Unit / project / Arsenal integration

- Reworked `UnitManager` from a placeholder-oriented stub into a minimally real runtime/persistence layer.
- Units can now persist:
  - plugin ID
  - plugin state
  - ordering
  - route/group data
  - key UI-facing state
- Units now expose a real `AudioArsenalSnapshot` for audio-engine consumption.
- Internal plugin units now survive project round-trips and restore plugin state on load.
- `AestraContent::loadInstrumentToArsenal(...)` now creates and attaches a real plugin instance instead of creating a placeholder-only unit.
- Arsenal unit enable/attach/load flows now activate plugins consistently instead of relying on test-only lifecycle setup.

### Project reliability

- Fixed a real project serialization bug where playlist clips could be serialized with an invalid `patternId: 0` even when a valid source pattern existed.
- Restored reliable project clip round-tripping by serializing/restoring the effective pattern linkage correctly.
- `ProjectRoundTripTest` is passing again after the serializer fix.
- Added `InternalPluginProjectRoundTripTest` to prove internal instrument units survive save/load.

### Audible proof

- Added `ArsenalInstrumentAttachmentTest` to verify units expose attached plugins to the audio path.
- Added `RumbleArsenalAudibleTest` to prove headless Arsenal pattern playback can route MIDI to Rumble and produce real audible output.

### Documentation

- Updated README and core technical docs to reflect the current verified March 2026 state instead of older aspirational roadmap language.
- Added a documented high-value confidence suite in `docs/technical/testing_ci.md`.
- Updated roadmap/task-list docs with the new internal Arsenal / Rumble validation status.

### Notes

- `OfflineRenderRegressionTest` is built and available, but remains fixture-driven rather than self-contained.
- It still needs canonical `.aes` + reference WAV fixtures before it can act as a dependable nightly/CI regression gate.

## v0.1.1 - 2025-12-28

### Added

- **ASIO Driver Support** (Professional Audio):
    - **Dual-Tier Driver System**: Seamless startup with automatic failover between ASIO (via `ASIODriver`) and WASAPI/DirectSound (via `RtAudioBackend`).
    - **Native COM Integration**: implemented a clean-room `ASIODriver` class handling safe COM loading (`QueryInterface`), binary compatibility (`__stdcall`, 4-byte packing), and STA threading enforcement.
    - **Low-Latency Streaming**: Verified sample-accurate callback loop (`bufferSwitch`) with zero allocations and lock-free processing.
    - **Robust Diagnosis**: Added detailed error reporting for COM (`HRESULT`) and ASIO initialization failures.

### Optimized

- **Audio Engine Performance**:
    - **Pan Law**: Replaced expensive per-sample trigonometry (`sin`/`cos`) with per-block gain smoothing (`gainL`/`gainR`), significantly reducing CPU overhead in the mixing loop.
    - **Resampling**: Implemented pre-calculated window tables for all Sinc Interpolators (8, 16, 32, 64-point), removing iterative Bessel function calculations from the audio callback.

### Fixed

- **Audio Engine Stability**:
    - Fixed "Master Silence" bug where buffer reallocation invalidated routing pointers (added `compileGraph` to `setBufferConfig`).
    - Restored missing audio summing loop in `renderGraph`.
    - Added Safety Fallback for unmapped tracks to ensure consistent gain behavior if UI parameters are missing.

## v0.1.0 - 2025-12-23

### Added

- **Audio Preview Scrubbing**:
    - Real-time scrubbing (click/drag) on waveforms in the File Preview Panel.
    - Dual-mode duration: 8-second initial limit, unlocked to 300 seconds upon scrubbing.
    - Auto-restart logic: Scrubbing a finished sample now automatically restarts playback from the seek point.
    - Real-time playhead visualization on the waveform.
- **File Preview Panel UX**:
    - Professional "Empty State" with a large file icon when no selection is active.
    - Improved Metadata display: Duration, Sample Rate, and Channel configuration.
    - Compact Folder info layout with side-by-side icon and text.

### Fixed

- **Audio Engine**:
    - Fixed critical crash/silence issue when scrubbing short samples or performing rapid seek requests.
    - Removed hardcoded 5-second duration limit in `AestraContent`.
    - Fixed duplicate `seek` method implementation in `PreviewEngine`.
- **UI**:
    - Fixed Folder name clipping/ellipsis behavior in the Preview Panel.
    - Disabled playback interactions for folders to prevent invalid engine states.
- **Build System**:
    - Suppressed CMake deprecation warnings from the FreeType dependency for cleaner build output.

### Changed

- **Tests**:
    - Updated `WavLoaderTest.cpp` to use `PlaylistTrack.h` instead of the legacy `Track.h`.
- **Documentation**:
    - Created `ALL_WALKTHROUGHS.txt` as a central index for project history.
