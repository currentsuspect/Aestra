# Changelog — 2026 Q2 (March–May)

All notable changes for Aestra in Q2 2026 are documented here.

## v0.6.0-alpha — Security & RT Hardening (2026-05-29)

### Security

- Take snapshot paths confined within project `.takes` directory — traversal regression coverage
- Scanned/cache plugins can no longer shadow registered internal plugin IDs
- Hard-coded dev account API fallback removed — stale/future/premium lease rejection in default builds
- Account refresh `issued_at` handling hardened; private release workflow token exposure fixed (#338)
- Production key required for premium leases
- Arsenal project plugin restore restricted to registered IDs only
- CLAP scanner helper writes guarded from SIGPIPE
- Nightly workflow token permissions limited to minimum required

### RT Safety

- Stale waveform source callbacks eliminated
- Async preview decodes bounded
- Audition decode worker lifetime owned explicitly
- Render main output slot lookup cached per graph compile
- Mixer lane state clamped before cast
- OpenGL kerning cache bounded
- MSVC ARM64 denormal register access made safe
- AestraVerb active state rebuilds guarded
- Graph dirty UI signal guarded
- Master safety limiter reshaped to transparent cubic Hermite knee

### Plugin Hosting

- VST3 host crash handling hardened — real VST3 processing in plugin helper
- Plugin proxy watchdog state made atomic
- Non-finite plugin output quarantined (NaN/Inf detection)
- Effect-chain fault state ownership fixed

### Callback Safety

- Triple-buffer `EngineState` with `GraphReadHandle` for RT graph access
- `RecordingCaptureRoute` double-buffered snapshot for input capture
- PDC edge state ownership fixed
- TSan CI added for callback-safety regression detection

### Serialization / Project Persistence

- Pattern restore and timeline persistence fixed
- BPM changes synced through playlist/timeline/pattern playback
- Arsenal sampler audio restored from `audioClipPath` for legacy states
- Migration framework proven with v1 fixture roundtrip

### Recovery

- Autosave crash session binding
- Session recovery test fixed on Windows

### CI / Build

- LeakSanitizer (LSan) advisory CI job
- API docs quality workflow Graphviz dependency fix
- GitHub Pages deploy gate changed from opt-in to opt-out (`DISABLE_GITHUB_PAGES != 'true'`)

### Sprint 2 Resolved

- `std::atomic_load` on `shared_ptr` deprecated (#331)
- Plugin crash protection verified Linux-only (#334)
- Plugin NaN/Inf validation moved to master output (#333)
- Migration framework proven (#332)
- fsync absence documented, deferred to post-beta (#335)

### Known Issues (as of v0.6.0-alpha)

- OOP plugin parameters are no-ops (#238) — P0
- Autosave serializer data race (#239) — P0
- Routing gain smoothing, cycle detection, RT allocation (#240, #241, #243) — P0
- CLAP MIDI input unimplemented (#244) — P0
- Full list: https://github.com/users/currentsuspect/projects/3

---

## v0.5.0-alpha — Feature & CI Milestone (2026-05-23)

**Takes system** — Multi-take recording with manifest, snapshots, transactional switching, path traversal guards, and UI integration.

**CLAP parameter support** — ClapParamInfo, ClapPluginParams structs, CLAP core constant definitions, null paramsExt fix.

**Audio quality** — K-weight race fix, ARM64 denormals, send gain smoother coefficient fix, audition queue deadlock fix, autosave atomic rename.

**CI hardening** — Removed jwlawson/actions-setup-cmake@v2 from all jobs, system cmake, DelayLine off-by-one fix (Capacity+1 buffer), Windows path separator fix, 13 tests registered, platform guards.

**PRs merged** — #291-#305 (11 PRs), develop→main merge (#304, 51 commits).

---

## v0.4.0-alpha — Hardening Milestone (2026-05-20)

### Security

- Full security audit completed — 11 findings identified, 8 fixed, 3 deferred with written justifications
- Project load path hardened against malicious files (structure, numeric, path, compression bomb validation)
- Crash recovery (`crash_flag`) hardened with opaque flag file, stale backup skip, max-load-size guard
- Archive extraction path traversal protection (`../` and absolute path rejection)
- `MixerSettingsSerializationTest` disabled (heap exploitation vector)

### Audio Quality

- BS.1770 K-weighting filter for true-peak metering
- Proper TPDF dither for 16-bit and 24-bit export (replacing naive quantization)
- Export quantization uses `std::lrint` instead of truncation
- Denormal protection via `AESTRA_FTZ_MANUAL` macro
- Removed Boost dependency (only user was sinc interpolator, replaced with manual `std::tgamma`)

### RT Safety

- `GarbageCollector::flush()` made lock-free
- GC retirement from audio thread via SPSC ring buffer
- `Mixer::m_effectChainSnapshot` race fixed (double-buffered, lock-free publish)
- `fadeOutActive` moved to `std::atomic<bool>` (was torn under concurrent access)
- `EngineSupervisor` added for audio thread liveness monitoring
- `RTGuard` added for RT-safety violation detection
- `AsyncCleanupManager` added for deferred resource cleanup

### Build / CI

- Nightly build workflow (`nightly.yml`) — scheduled headless build with ASan/UBSan, auto-issue on failure
- Versioning and tagging policy (AGENTS.md Section 27)
- Nightly build policy (AGENTS.md Section 28)
- RELEASES.md tag inventory and milestone history
- macOS CI test exclusions aligned with sanitizer config (#229, #230)
- gitleaks PR trigger fix (workflow existed, issue closed)
- CodeRabbit review findings fixed (PR #236, #237)
- Deprecated `std::atomic_load` on `shared_ptr` identified (14+ sites, tracked as #251)
- `-Wno-maybe-uninitialized` global suppression identified (tracked as #260)
- Global warning suppressions documented

### Plugin Hosting

- OOP plugin parameters identified as no-ops (tracked as #238, P0 beta-blocker)
- CLAP MIDI input identified as unimplemented (tracked as #244, P0 beta-blocker)
- CLAP host callbacks identified as no-ops (tracked as #270)
- Plugin crash protection identified as Linux-only (tracked as #247)
- Per-plugin NaN/Inf validation identified as missing (tracked as #248)

### Serialization / Project Persistence

- Autosave serializer data race identified (tracked as #239, P0 beta-blocker)
- No fsync in any write path identified (tracked as #245)
- Autosave non-atomic remove+rename identified (tracked as #246)
- Migration framework identified as unproven (tracked as #249)
- Content integrity check identified as missing (tracked as #263)
- Asset references identified as paths not hashes (tracked as #264)
- ProjectSerializer layering violation identified (tracked as #266)
- 13 unregistered test files identified (tracked as #250)

### UI

- Full UI token pass — theme tokens applied to Arsenal, Timeline, Audition, FileBrowser, Transport, MenuBar, TitleBar
- Piano roll features completed (9 features, 7 bug fixes)
- Mixer color palette, track color index, right-click swatch picker
- Plugin browser filter redesign
- File preview panel overhaul

### Routing

- Send gains not smoothed — identified as P0 (tracked as #240)
- No cycle detection — identified as P0 (tracked as #241)
- RT allocation in render loop — identified as P0 (tracked as #243)
- Send pan law inconsistency — identified (tracked as #261)
- Per-sample send iteration — identified (tracked as #262)

### Platform

- macOS platform identified as unimplemented (tracked as #267)
- Linux RT scheduling identified as missing (tracked as #255)
- Audio settings page device enumeration freeze identified (tracked as #256)

### Testing

- ArsenalExportLiveParityTest fixed (2026-05-15, re-fixed 2026-05-19)
- NUITextInputLayoutTest identified as pre-existing failure
- MixerChannelScratchBuffer and OfflineRenderRegression verified active
- 13 unregistered test files documented

### Documentation

- AGENTS.md Sections 27 (Versioning/Tagging) and 28 (Nightly Builds) added
- RELEASES.md created with tag inventory and milestone history
- 69 GitHub issues opened with full priority/type/component taxonomy
- GitHub Projects board created with sprint structure through December

### Known Issues (as of v0.4.0-alpha)

- OOP plugin parameters are no-ops (#238) — P0
- Autosave serializer data race (#239) — P0
- Routing gain smoothing, cycle detection, RT allocation (#240, #241, #243) — P0
- CLAP MIDI input unimplemented (#244) — P0
- 5 empty model stub files (#252) — P1
- No fsync in write paths (#245) — P1
- macOS platform unimplemented (#267) — P1
- 13 unregistered test files (#250) — P1
- Full list: https://github.com/users/currentsuspect/projects/3

---

## v0.3.0-alpha — Plugin Expansion (March–April 2026)

### Audio Plugins

- **AestraComp**: RMS-based detection, hardware compressor behavior
- **AestraVerb**: SIMD optimizations (SSE/AVX), reverb quality lab
- **AestraDelay**: Full delay plugin implementation
- **Rumble**: 808-style bass synth with Arsenal integration
- **AestraEQ**: Parametric equalizer

### Arsenal

- UnitManager runtime/persistence layer
- Internal plugin metadata (`BuiltInPlugins`)
- Arsenal unit enable/attach/load flows
- Plugin state survives project round-trips

### Piano Roll

- Double-click pattern clips to open in Piano Roll
- Unit routing (notes carry `unitId`)
- Piano Roll ↔ Sequencer sync
- Auto-save on note changes

### Playlist / Clip Editing

- Cut/Copy/Paste/Delete wired to edit menu and shortcuts
- Split tool via blade tool click and `S` key
- Clip split bug fix (patternId, name, colorRGBA preserved)

### Export / Offline Render

- AudioExporter rewrite — uses `AudioRenderer::renderBlock()`
- Duration computed from actual playlist
- Position advances correctly (sample-accurate)
- Master output stage applied during export
- Three render scopes: FullSong, LoopRegion, Selection
- Bit depths: 16-bit, 24-bit, 32-bit float

### Device Resilience

- Driver-level underrun/xrun telemetry
- Health monitor thread (500ms polling, 2s stall detection)
- Hot-plug detection (WASAPI `IMMNotificationClient`)
- RT safety: removed stdout/stderr from audio threads
- PerformanceHUD enhancements

### Build / CI

- Low-memory build preset (`lowmem`) for 4GB RAM machines
- CI pipeline fixes (macOS Security framework, Windows C++17 compat)
- gitleaks expansion to PR trigger

---

## v0.2.0-alpha — First Plugin Generation (January–February 2026)

### Audio Engine

- ASIO driver support (dual-tier: ASIO + WASAPI/DirectSound)
- Pan law optimization (per-block gain smoothing)
- Resampling pre-calculated window tables
- Master silence bug fix (buffer reallocation invalidating routing pointers)

### UI

- Audio preview scrubbing (real-time click/drag on waveforms)
- File Preview Panel overhaul
- Waveform generation and redraw fixes

### Build

- FreeType deprecation warning suppression
- CMake build system improvements

---

## v0.1.0-foundation — Engine Foundation (October 2025)

- Initial engine architecture
- Core audio pipeline
- Basic plugin hosting
- Platform abstraction (Windows, Linux)
