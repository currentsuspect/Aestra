# Changelog — 2026 Q1

All notable changes for Aestra in Q1 2026 are documented here.

## [Unreleased]

### Maintenance & Infrastructure (March 23, 2026)

- **CI/CD Improvements:**
  - Consolidated build workflows into unified CI pipeline
  - Added build timeouts (30m Linux, 45m Windows/macOS) to prevent hanging builds
  - Added Discord webhook gating (skips when secret unavailable)
  - Fixed all CI build failures

- **Code Quality:**
  - Added `.clang-tidy` configuration for static analysis
  - Added pre-commit hook for platform abstraction leak detection
  - Removed redundant CMake C++ standard settings
  - Removed accidentally committed build artifacts (saved 4MB)
  - Strengthened `.gitignore` patterns

- **Bug Fixes (Qodo Review):**
  - Fixed CommandHistory deadlock (execute outside lock)
  - Fixed AutosaveManager self-deadlock (check state before locking)
  - Fixed CommandTransaction::redo() to call redo() on children
  - Fixed DuplicateClipCommand to preserve ID across redo
  - Fixed TrimClipCommand negative duration validation

- **Headless Infrastructure:**
  - Added ProjectValidator for safe project loading
  - Added AutosaveManager for crash recovery
  - Added CommandTransaction for grouped undo
  - Added AudioExporter for offline rendering
  - Added HeadlessMusicGenerator for programmatic music

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

## Notes

- `OfflineRenderRegressionTest` is built and available, but remains fixture-driven rather than self-contained.
- It still needs canonical `.aes` + reference WAV fixtures before it can act as a dependable nightly/CI regression gate.
