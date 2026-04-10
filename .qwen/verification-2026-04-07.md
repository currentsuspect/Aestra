# Aestra State Verification — April 7, 2026

## Branch: `develop` (HEAD: 2246def0)
- Last commit: `dsp-lab: update session 002 log with stress test results`
- 3 working branches: `develop`, `main`, `epic-k-device-resilience`
- Untracked: `labs/memory/results/` (memory benchmark runs from Apr 6)

---

## High-Impact Wins — April 7, 2026

### ✅ OfflineRenderRegressionTest added to confidence suite + CI
- Added to `scripts/run-confidence-suite.sh` (TARGETS array + run section)
- Test passes: correlation=0.997787 (threshold: 0.995), rmsDiffDb=-44.98 (threshold: -35)
- Export regressions will no longer slip through

### ✅ VST3 state persistence implemented
- `VST3Host.cpp`: `saveState()` now uses `IComponent::getState()` with IBStream memory wrapper
- `loadState()` now uses `IComponent::setState()` with IBStream memory wrapper
- Third-party VST3 plugins now survive project save/load round-trips

### ✅ VST3 MIDI input/output wired
- `VST3Host.cpp::process()`: IEventList implementation for MIDI input events
- Converts Aestra MidiBuffer::Event to VST3 Event (note on/off, channel, pitch, velocity)
- MIDI output events from plugins are converted back to MidiBuffer
- VST3 instruments and MIDI effects now functional

### ✅ CLAP setParameter wired
- `CLAPHost.cpp::setParameter()`: calls `clap_plugin_params::set_value()` and `clap_host_params::request_flush()`
- CLAP plugins now respond to parameter changes

### ✅ Recording Path Tests added
- `Tests/AestraAudio/RecordingPathTest.cpp` — 4 tests:
  1. Track arming/disarming
  2. Input capture (synthetic audio feed)
  3. WAV write + clip commit
  4. No capture when not armed
- Added to CMakeLists.txt with full include paths
- Added to confidence suite script

---

## What's Still Missing After Today

| Gap | Priority | Effort Estimate |
|-----|----------|----------------|
| Arsenal effects (EQ, Compressor, Reverb, Delay) | HIGH | Days-weeks per unit |
| CLAP MIDI + state persistence | MED | Similar to VST3 work done today |
| Plugin Delay Compensation | HIGH | Non-trivial routing change |
| 30-min soak test in CI | LOW | Infra work, not code |
| Float-32 export dithering determinism | LOW | One-line fix |
| Recording latency compensation in monitoring path | MED | Timing math |
| CLAP watchdog crash containment | MED | Similar to VST3 SEH pattern |

---

## End-to-End Workflow Audit

### What a producer needs: Blank project → Finished track

| Step | Status | Detail |
|------|--------|--------|
| **Create project, set tempo** | ✅ Working | |
| **Import samples** | ✅ Working | |
| **Arrange patterns/clips on timeline** | ✅ Working | Pattern/playlist model solid |
| **Basic clip editing** (move/dup/split/trim) | ✅ Working | All wired through CommandHistory |
| **Piano Roll** | ✅ Working | Unit-aware notes, auto-save, sequencer sync |
| **Mix** (vol/pan/mute/solo + limiter) | ✅ Working | Master safety limiter in place |
| **Save/Load/Autosave/Recovery** | ✅ Working | Atomic writes (temp+rename), backup rotation |
| **Undo/Redo** | ✅ Working | Clip add/remove/move/split/trim/dup, track add, mixer edits, Arsenal edits |
| **Record audio** | ⚠️ Exists, UNTES TED | No test coverage at all for recording path |
| **Export to WAV** | ⚠️ Works, no regression gate | Offline render test exists but NOT in CI/confidence suite |
| **VST3/CLAP plugins** | ⚠️ Host works, critical gaps | MIDI/states are TODO stubs |
| **MIDI output to hardware** | ❌ Missing | No MIDI I/O library, no device enumeration |

---

## Critical Gaps (Beta Blockers)

### 1. Recording Path — ZERO TESTS
- Code exists in `TrackManager.h` (~300 lines inline)
- Arming, capture, monitoring, WAV writing, take commit — all implemented
- **No recording tests exist anywhere.** Zero coverage of:
  - Track arming/disarming
  - Input capture path
  - Monitoring mix
  - Take commit (WAV write + clip placement)
  - Session begin/finalize under load
  - Device stress during recording
- Latency compensation exists but is **never applied** to the monitoring path

### 2. VST3/CLAP Plugin Hosting — TODO Stubs
| Gap | Severity | Impact |
|-----|----------|--------|
| **MIDI not wired** (both hosts pass nullptr) | HIGH | Instruments produce silence, MIDI effects broken |
| **State persistence missing** (saveState/loadState are stubs) | HIGH | Third-party plugins lose ALL params on save/load |
| **CLAP setParameter is no-op** | HIGH | Parameter changes never reach CLAP plugins |
| **Plugin Delay Compensation** | HIGH | Measured but never compensated |
| **CLAP watchdog is stub** | MED | No crash containment for runaway plugins |
| **Editor embedding: Windows-only** | MED | No macOS/Linux plugin UI |

### 3. Arsenal Plugin Set — Only 2 Instruments, ZERO Effects
| Unit | Type | Status |
|------|------|--------|
| Aestra Rumble | Instrument (808 sub-bass) | ✅ Complete |
| Aestra Sampler | Instrument (32-voice sample player) | ✅ Complete |
| EQ | Effect | ❌ Missing |
| Compressor | Effect | ❌ Missing |
| Reverb | Effect | ❌ Missing |
| Delay | Effect | ❌ Missing |
| Saturation | Effect | ❌ Missing |
| Limiter | Effect | ❌ Missing (master only) |

**Hip-hop producers need at minimum**: EQ, Compressor, Reverb, Delay, Saturation. None exist internally.

### 4. MIDI Output — Does Not Exist
- No RtMidi dependency
- No MIDI device enumeration
- No MIDI clock/MTC output
- No MIDI learn from external controllers
- **Cannot send MIDI to hardware synths, drum machines, or controllers**

### 5. Export Regression Testing — Not in CI
- `OfflineRenderRegressionTest` exists — validates correlation >= 0.995, RMS diff <= -35 dB
- **NOT in confidence suite, NOT in CI profile**
- Float-32 export applies dithering (non-deterministic, breaks repeatability)
- Export supports WAV only (16-bit PCM, 24-bit PCM, 32-bit Float)

---

## What's Actually Solid

### ✅ Production-Grade
- Core audio engine architecture (driver-independent, RT-safe)
- Audio graph and routing (TrackManager, PatternManager, PlaylistModel)
- CommandHistory / Undo-Redo (fully wired for core workflow)
- Project save/load with atomic writes
- Autosave with backup rotation and recovery
- Arsenal Rumble and Sampler (internal instruments)
- VST3/CLAP scanning and audio processing (sans MIDI/state)
- Custom UI stack (not dependent on third-party frameworks)
- Mixer channel processing (vol/pan/mute/solo)
- Clip split/trim/duplicate (both halves retain patternId, name, color)
- Piano Roll ↔ Arsenal ↔ Sequencer sync
- Main.cpp refactor: 2414 lines → 138 lines

### ⏳ In Progress (Phase 3 — Jul-Sep 2026)
- Recording reliability (arming, input selection, monitoring)
- Session stress tests (long playback, repeated takes, device switch)
- Export hardening (offline render regression gate)

### ✅ Complete (Phase 2 — Apr-Jun 2026)
- Dirty-state semantics + UI indicator
- Autosave tied to dirty state
- Autosave recovery on startup
- Crash-safe writes (atomic temp file + rename)
- Project format v1 spec (versioning, migrations, validation)
- Internal plugin project round-trip (Rumble survives save/load)
- Undo/redo for all core actions
- Cut/Copy/Paste/Delete fully wired
- Edit menu with keyboard shortcuts

---

## Roadmap Reality Check (Dec 2026 Beta)

### Phase 1 — Foundation lock ✅ COMPLETE
### Phase 2 — Project + undo/redo ✅ COMPLETE  
### Phase 3 — Recording + export 🟡 IN PROGRESS
### Phase 4 — Plugin scope decision ⏳ PENDING
### Phase 5 — UX hardening + Beta freeze ⏳ NOT STARTED
### Phase 6 — v1 Beta release ⏳ NOT STARTED

---

## What's Missing for v1 Beta (per roadmap)

- [ ] Recording workflow reliability (tested end-to-end)
- [ ] Session stress tests: long playback, repeated record takes, device switch/disconnect
- [ ] Export regression test in CI
- [ ] Minimum Arsenal effects set (EQ, Compressor, Reverb, Delay)
- [ ] VST3/CLAP MIDI wiring
- [ ] VST3/CLAP state persistence
- [ ] Plugin Delay Compensation
- [ ] 30-minute soak test with zero dropouts
- [ ] Performance budgets enforced (audio callback time, UI frame time)
- [ ] Signed Windows build + basic installer

---

## Discussion Points

1. **Do we need Arsenal effects for Beta?** Hip-hop producers won't ship without at least EQ + compressor + reverb + delay.
2. **Plugin scope decision**: Cut VST3/CLAP for Beta (Option A) or fix MIDI/state/PDC (Option B)? Option B is a lot of work.
3. **MIDI output**: Can this be post-Beta? If the target is pattern-based electronic music with internal instruments, maybe.
4. **Recording tests**: This is the biggest gap. The code exists but nobody trusts untested recording in a DAW.
5. **Export regression in CI**: Easy win — just add the existing test to the confidence suite.
