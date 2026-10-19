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

338 PRs merged (#624–#775). The story of this milestone is coherence: routing and gain staging are correct end to end, automation lanes are actually automatable, the Master channel is a real plugin host whose latency sits in the compensation graph, and the piano roll grew from an editor into a workflow.

> **Versions, clarified.** `v0.7.0-alpha` is the *application release version*. It is unrelated to `ProjectSerializer::PROJECT_VERSION_CURRENT` (the project/file format version, currently 3): the format version changes only when the `.aes` format itself changes, never on a release bump.

### Routing & Mixing Correctness

- Unity gain law end to end: channel strips use the stereo-balance law, so routing a signal through any mixer channel no longer attenuates it — and the same law governs live playback, offline export, isolated bounce, audition preview, and input monitoring (parity pinned by tests).
- Fader and pan applied once: one UI gesture no longer writes two stores that the engine multiplied (−6 dB on the fader used to play at −12 dB).
- Routing command seam with mutation-time cycle rejection, stable send IDs, and master-legality enforcement; render-path semantics match the routing contract (V1/V2/V3) with live/offline parity.
- Master channel is a valid plugin host: insert chains process on the master bus before the fader and safety limiter, persist in the project file, and their latency feeds the plugin-delay-compensation graph (reported project latency is now the real end-to-end delay).
- Master-routed clips obey the live solo gate; isolated-track bounce renders only the selected track's stage and excludes the master stage by design — both halves of the isolation contract are pinned by tests.

### Automation

- Automation targets the plugin instance it was drawn for, never the slot it occupied — chain
  reordering keeps curves bound to their plugin (#766).
- Empty lanes are automatable: the first click creates a neutral Volume curve bound to the lane's
  channel, and edits rebuild the audio graph and mark the project dirty.
- Leftover demo automation removed from the default project and from previously saved projects.
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

## v0.7.1-alpha — Trust Sprint (2026-09-07)

Reliability is the feature. This milestone carried exactly one new capability by
design (FD-13); everything else removes a reason a first session ends badly.

### The seam that was reported nine times

The week before this sprint produced nine founder-reported bugs, all in one
family: the UI told you one thing and the engine did another.

- **Pattern edits now reach playback.** Unmuting a channel, and slicing a
  pattern, both left the scheduler evaluating the old structure until an
  unrelated edit forced a refresh — so the timeline showed one arrangement and
  you heard another. Split, delete, mute, solo, undo and drop all reschedule
  now. (#879)
- **Stop lands at zero.** A single stop left the playhead visibly off the start.
  (#868)
- **Recording lines up under latency.** Takes are placed with device latency
  compensated and the capture stamped from the engine frame, so a track recorded
  through a high-latency plugin chain sits where you played it. (#881)

### Losing work is harder

- **Missing audio is recoverable.** A project whose audio files have moved now
  opens with a relink dialog instead of failing quietly. (#876)
- **Recovery no longer doubles your tracks.** Loading cleared the track table
  first, so a recovered project came back with every track twice.

### The one feature

- **Fit audio clip to N bars.** Varispeed tempo-fit, pitch follows tempo, riding
  the existing ratio pipe — no new DSP and no format change. (#747)

### Producer-facing

- **Aestra Transient**, an internal envelope shaper: attack/decay gain scaling,
  zero latency, no PDC. A drum-workflow tool, not a centrepiece. (#869, #873)
- **Mixer plugin menus** are populated from a metadata catalog rather than a
  hand-maintained list, and now open where you clicked. (#872, #875)

### Memory and imports (for the 4 GB laptop)

- **Audio files that are 16- or 24-bit PCM stay that size in memory** (half or three quarters of before),
  and sound bit-for-bit the same. A 12-track, 4-minute-each session fell from 1.2 GB to 0.7 GB of audio
  memory. MP3, OGG and 32-bit float files are unchanged. (#1070)
- **Loading a long sample from the browser no longer freezes the app** while it decodes. (#1071)
- Higher-sample-rate files keep their anti-alias copy only while something plays them, and it is made
  without a second temporary copy. (#1072)
- `scripts/aestra-4gb.sh` runs Aestra under a 4 GB, 2-core limit for performance work. (#1069)

### Trust infrastructure

- **The audio thread is checked at compile time.** `AESTRA_RT_NONBLOCKING`
  (Clang's function-effects analysis) makes allocation, locks, throws and
  unresolvable calls on an annotated real-time path a build error rather than a
  code-review note. Enforced in CI. Coverage starts at `TruePeakMeter` and grows
  per annotation; extending it to the callback itself is tracked separately.
- **One RT reporting API.** `Source/AudioThreadConstraints.h` and its parallel
  counter machinery are gone; `reportRealtimeMisuse` is the only reporting call.
  A detection surface that some call sites consult and others do not reports
  "clean" for thread state it never observed. (#882)
- **Engine ownership is documented and enforced.** No singleton accessor
  survives; production runs one app-owned engine.

### Fixed

- The startup log said `Aestra v1.0.0` — a hand-written string, wrong since
  0.5 — and the build id carried no version at all. Both now come from the build,
  so a log identifies the build it came from.
- Removed an unreachable `MixerChannel` processing path that allocated scratch
  buffers per channel for a function nothing called.

### Known gaps

- The intermittent resize failure (P0) has no reliable reproduction and remains
  open rather than silently closed.
- Automation *authoring* is still parked for v0.8.0 (FD-13); the backend
  machinery exists, the interaction does not.

## v0.8.0-alpha — Automation, Clips and Export (2026-10-11)

Over 150 commits since v0.7.1-alpha. Automation is now something you author; a clip can be
bounced, fitted and faded from the clip itself; export can write stems; and the UI half of
the app can fail a build.

### Automation

- **Edit any curve, undo any edit.** A lane edits the curve you choose (volume, pan, or
  a plugin parameter), every edit is one undo step, points snap to the timeline grid, and
  Shift extends a selection. (#1056, #1057)
- **Automation clips.** A stretch of a curve is an object: drag its grip to move it, Ctrl-drag to
  copy it, right-click for Duplicate, Split Here and Delete. Cutting a clip never changes how it
  sounds. Projects that use no clips save exactly as before. (#1065)
- **Curve tension is audible**, behind a versioned migration (project format v4): projects from
  before store a tension value that never played, so loading one resets it to linear and the
  project sounds the same. (#1052)
- A volume curve with no points is unity, not silence. (#948)

### Clips

- **Fit to bars suggests the musical lengths.** The editor offers the five best bar counts for
  the clip, best first, and says what each does ("x0.88 speed, pitch follows"). (#1061)
- **Bounce in Place.** Right-click an audio clip: it becomes a recording of how it sounds
  through its track's effects. The effects switch off so nothing is doubled, Undo brings
  everything back, and the track's fader and pan stay live. Effect tails past the clip are cut. (#1062)
- Draggable fades on the timeline; a clip context menu; consistent undo for clip operations. (#950, #1050)
- **Left-trimmed MIDI clips play where they are drawn.** They used to play late by the amount trimmed. (#1067)

### Mixing and plugins

- **Insert slots grow** past ten, up to 64. Projects with ten or fewer inserts save as before. (#1059)
- Plugin-parameter automation has a render-thread contract any plugin format can satisfy; internal
  plugins use it now. (#1058)
- The browser remembers recently loaded plugins. (#1064)
- **Every panel can be Docked** (locked in place) **or Floating**, and **Contextual** (closes when you click
  away) **or Keep Open**: right-click a panel's title bar. View > Gather Panels and Reset Panel
  Positions bring everything back. Panels behave as before until you change them. (#1066)

### Export

- **Stems.** One file per track with sound, plus the master, all the same length so they line up in
  any other program. The export window is rebuilt on the app's own controls. WAV only. (#1063)
- Loudness and true peak are measured once per file (a shared analysis); the engine's loudness
  reading was about 1 dB low off 48 kHz and is fixed. (#1053)

### Trust infrastructure

- UI scenarios, UI architectural guards and a budget gate (bundle size, headless RSS) now fail CI.
  (#921, #1055, #1036)
- A shared, disk-backed store holds how you arrange the app (panel geometry, modes, favorites, recents),
  never inside a project. (#1051)
- Placeholders deleted rather than shipped: glassmorphism, the SDF text renderer, the nine arrangement
  widget stubs. (#1049)

### Known gaps

- **Not seen on a display.** Several features here are covered by tests that drive the real code but
  were not clicked through in the running app: Bounce in Place's menu entry, automation clips, the new
  export window, the panel menus, and the eleventh insert row.
- VST3 and CLAP plugin-parameter automation still needs a host adapter; only internal plugins are driven.
- Bounce in Place does not handle instrument tracks and cuts effect tails.
- Panel menus are reached by right-clicking a title bar; there is no visible button yet.
- Frame cost on the 4 GB target machine: an idle timeline costs about 12 ms per frame whether or not
  the transport is playing (the timeline is redrawn every frame; the render cache is off on Linux), and
  a panel that covers only part of it still leaves it drawn underneath. A retained-geometry replay of the
  timeline is the next step; the piano roll fully covering it is already skipped (#990).
- Sources recorded above the project rate (96 kHz in a 48 kHz project) still need a full float32
  anti-alias copy while they are played, and preparing it keeps one core busy for a couple of minutes.
- Presets and in-app documentation are deferred to v0.8.1.

## v0.8.1-alpha — The revamp: Arsenal, Mixer, Routing, Audition, Versions (2026-10-19)

88 PRs since v0.8.0-alpha. The surfaces you spend the most time in are rebuilt: the Arsenal is a rack you
play, the mixer reads meters first, the routing graph shows only what you changed, Audition is a place to
listen and compare, and Versions replaces the history list.

### Arsenal

- **A rack you play.** Units are grouped rows with output pills, patterns are one-click scenes under the
  header, and a row opens into step lanes. The Arsenal opens full width, in the unit's channel colour.
  (#1094, #1095, #1096, #1105, #1106, #1107)
- **Start with a kit.** An empty Arsenal offers to load a folder of sounds: every file becomes a unit,
  named after it. (#1104)
- **Explore sounds without stopping.** A row's < > arrows step to the next sound in its folder, and the
  sound drawer searches the folder and auditions with the arrow keys. A swap comes in on the next hit,
  with no gap. (#1101, #1103, #1136)
- **Sound sets.** A row can walk a library collection made from many folders. (#1137)
- **Swing, chance and ratchets.** Chance plays a step only some of the time, a ratchet splits it into a
  quick roll, swing shifts the off-beats. (#1098, #1100)
- **Place in timeline** puts the pattern in your song. Press it again and the next copy follows the last
  one. Switching patterns while playing takes over at once. (#1109, #1147, #1151)
- **Hits no longer drop when the screen is busy.** Hits were queued only ~85 ms ahead from the UI thread,
  so a slow frame skipped them with no xrun anywhere. The lookahead is a quarter second; edits are still
  heard at once. `get_audio_health` reports any missed hits. (#1161)

### Mixer and routing

- **Meters first.** Strips are grouped by output under coloured headers, with three densities and a
  navigator for big sessions. A strip names its inserts and its output; one dB scale serves them all.
  (#1085, #1086, #1088, #1089, #1090, #1154)
- **The master reads loudness:** integrated, short-term, true peak and correlation. (#1087)
- **The inspector is one column**, sends one line each; the routing minimap is gone. (#1091, #1092, #1093)
- **The routing graph shows only custom routes.** Tracks that go straight to the master are listed, not
  drawn. Select a channel to trace its chain, drag to patch, and a loop is refused. (#1153)

### Audition

- **A hub for listening.** Open a mix and compare it with a reference that plays alongside it at the
  same position, level-matched. Switching is a short ramp, never a cut. (#1127, #1146)
- **Tonal, Dynamics, Stereo and Sections** tabs on an item: tonal balance, PLR, width and mono
  problems, and a section map. (#1126, #1150)

### Versions

- **Versions replaces the History and Takes lists.** Saved versions sit on a branch timeline, each with
  its steps. Branch from any version, bounce a version and play it, bring tracks and patterns over from
  another version, and find a crash-recovery card when something went wrong.
  (#1120–#1124, #1139–#1145, #1155)

### The v0.8.1 look

- One context-menu look, one panel chrome, flat library, pattern and plugin rows. The sample and audio
  clip editors are rebuilt. (#1113, #1114, #1117, #1119, #1128–#1135)
- **The library browser:** Sounds, Patterns and Plugins tabs, one Places button for all your locations,
  tag chips, and a preview strip with Add to Arsenal. (#1160)
- **One playhead everywhere:** a solid line with a filled head in the timeline, piano roll and Arsenal. (#1162)
- **Settings show real values,** with a real auto-save switch and no Windows path on Linux. (#1157)

### Fixes

- **The startup log names the real version.** It said "Aestra v0.1.0" on every release, so a crash report could not
  say which build it came from. (#1167)
- **Panel buttons bring a covered panel forward** instead of closing it, and Arsenal steps and lanes no longer
  spill over the rack when you scroll. (#1166)
- **Opening Aestra with a project plays at that project's tempo.** It used to play at 120 while showing
  yours, so loops sounded cut off. (#1165)
- **New Project and Open Project ask about unsaved work,** as Quit does. (#1163)
- New Project starts clean; the kit picker no longer freezes the window. (#1149)
- Trims and undo/redo rebuild audio playback (no more trimmed-off tails playing). (#1158)
- Closing a title-bar menu no longer crashes. (#1159)
- Audio devices are found by name, not list position. (#1102)
- Held voices are released on stop, pause and loop wrap. (#1078)
- Bounce in Place works on pattern clips, and a pattern on several tracks becomes one clip on Master.
  (#1084, #1152)

### Known gaps

- **Moved to v0.8.2 (FD-51):** presets, in-app documentation, the piano roll, Settings/Export and
  timeline looks, the second half of Audition (band solo, Mono/Sides, blind tests) and Versions' compare
  with the current mix.
- Bounce a version blocks the window while it renders (about 3x real time).
- Bounce in Place on a multi-track pattern does not follow sends or group buses.
- The browser's In tempo preview, per-row waveforms and key column need the analysis service.
- Checked on the 1366x768 laptop for the Arsenal and the panels; the rest was seen on a virtual display and in tests.

## [Unreleased]

Nothing yet — v0.8.1-alpha was cut on 2026-10-19. v0.8.2 work lands here.

## Unfiled — shipped before v0.7.0, never assigned to a release

> **This section is mislabelled history, not pending work.** It sat under
> `[Unreleased]` below the v0.7.1 heading, which read as "none of this has
> shipped yet". It has: spot-checking three of its claims against the tree finds
> the `lowmem` CMake preset, `Source/Settings/ExportDialog.cpp` and
> `RumbleArsenalAudibleTest` all present. The content is real and released; only
> its placement was wrong.
>
> Assigning each entry to the release that actually carried it needs git
> archaeology per item, which is deliberately not guessed at here — a confident
> wrong attribution in a changelog is worse than an honest unfiled one. Until
> that reconciliation happens this heading states what is true: shipped, before
> v0.7.0, never filed.


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
