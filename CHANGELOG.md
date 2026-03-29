# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

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
