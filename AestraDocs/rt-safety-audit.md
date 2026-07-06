# Real-Time-Safety Audit — Findings & Baseline

**Scope:** all code reachable from the audio callback. **Method:** empirical
allocation trap (`RTAllocationTrapTest`) + static sweep
(`scripts/rt_safety_audit.sh`) + manual classification of every hot hit.
**Baseline commit:** develop @ 80fbbf87 (+ integrity series).

## Callback entry points (verified, with sources)

```
RtAudioDriver::rtAudioCallback            AestraAudio/src/Linux/RtAudioDriver.cpp:248
  → AestraAudioController::audioCallback  Source/Core/AestraAudioController.cpp:407
      → AudioEngine::processBlock         AestraAudio/src/Core/AudioEngine.cpp:593
          (ScopedRealtimeAudioThread marks the thread at :594)
          → renderGraph → per-track mix, EffectChain::process, plugins
          → AuditionEngine::processBlock  (AudioEngine.cpp:722)
      → TrackManager::mixInputMonitoring  (AestraAudioController.cpp:431)
      → PreviewEngine::processRealtime    (AestraAudioController.cpp:436)
Offline export drives the same processBlock from the exporter thread
(AudioExporter.cpp render loop), also under the guard.
```

## Machine-checkable gates (added by this series)

- **`RTAllocationTrapTest`** — overrides global operator new/delete; renders a
  3-track session with fader/pan engaged and an active insert; asserts ZERO
  steady-state heap activity while `isRealtimeAudioThread()`. Includes a
  self-check proving the trap fires (it caught GCC's allocation elision faking
  a pass during development). Scope: C++ new/delete; NOT raw malloc, mutex
  waits, or syscalls.
- **`scripts/rt_safety_audit.sh`** — static tripwire over 12 RT-reachable
  sources; compare its output against the classified baseline below.
- **`reportRealtimeMisuse()`** (pre-existing) — ~20 mutating APIs self-report
  if called from the audio thread.

## Findings

| # | Finding | Evidence | Classification |
|---|---|---|---|
| 1 | Render path performs **zero steady-state heap activity under tested conditions** (3 tracks, fader/pan params, active insert, 186 blocks) — with **one detected first-block 31-byte allocation when an active insert is present** (finding #3; zero absolutely without an insert). The claim is NOT unconditional until #3 is resolved. | `RTAllocationTrapTest` | **verified safe at steady state** |
| 2 | Preview ducking attenuated offline exports by the full configured depth | `RealtimeExportParityTest` | **confirmed violation — FIXED** (AudioExporter save/disable/restore + duck-smoother snap) |
| 3 | **1 allocation (31 bytes) in the first block after an active insert is added** — lazy init somewhere in the effect-chain path; xrun risk on the first callback after inserting a plugin during playback | `RTAllocationTrapTest` first-block counter (0 allocs without insert, 1 with) | **suspicious but unproven origin** — reproducible; origin hunt is follow-up (gdb conditional breakpoint on the trap) |
| 4 | `AuditionEngine::processBlock` (RT, AudioEngine.cpp:722) invokes `m_onPositionChanged` — an arbitrary app-bound `std::function` — on the audio thread every 10th block (AuditionEngine.cpp:543-544) | grep + code read; **currently unbound in Source/** (no binder found) | **latent hazard by design** — safe today, one `setOnPositionChanged(ui_lambda)` away from UI work on the RT thread. Follow-up: route through an SPSC snapshot like meters |
| 5 | `AuditionEngine::processBlock` body (439-546): no direct locks/logs/allocs; its 26 mutex sites live in queue/load helpers (`loadCurrentTrack` etc.) | windowed grep | **acceptable by design** (helpers are non-RT) |
| 6 | `PatternPlaybackEngine::processAudio` drains its RT queue lock-free; the file's 3 locks are in non-RT mutators, and `refillWindow` is guarded by `reportRealtimeMisuse` and called from `performNonRealtimeMaintenance` | code read :279+, :96/:124/:151 | **acceptable by design** |
| 7 | `PreviewEngine::processRealtime` (:240) takes no locks; the 4 mutex sites are worker setup + `handleDeferredCompletion` (non-RT, called from maintenance) | code read | **acceptable by design** |
| 8 | `MixerChannel` has 8 `m_sendMutex` guards — all on send *mutation* APIs; the RT path reads sends from the compiled graph snapshot, not MixerChannel | code read :168-213 | **acceptable by design** |
| 9 | `AudioEngine` sleeps: :1769 is the LUFS worker thread loop (background, ~10 Hz); :3173 is the non-RT isolated-bounce path ("allow RT to spin down") | code read | **acceptable by design (non-RT)** |
| 10 | 62 logging calls across the swept files — spot-checked hot ones are in non-RT methods (bounce, setup, error paths); `performNonRealtimeMaintenance` logs RT-misuse reports from the maintenance thread, not the RT thread | grep + spot checks | **acceptable by design**, not exhaustively proven per-site — the grep script keeps the count visible |
| 11 | 97 container-growth hits — concentrated in `initialize()`/`setBufferConfig()` pre-allocation (`AudioEngine.cpp:1370-1390` reserves everything up front) and non-RT paths; the allocation trap proves none fire at render steady state | grep + trap test | **acceptable by design** (trap is the enforcement) |
| 12 | Fresh-engine `ContinuousParamBuffer` values take one processBlock to reach the mix (~85 ms at export block size) | RMS trajectory, integrity doc §6 | **acceptable by design / documented sharp edge** |

## What is NOT covered (honesty section)

- Raw `malloc`/`free` (C paths) — not trapped; none observed in sweeps of RT
  sources, but unproven.
- Mutex *waits* — `reportRealtimeMisuse` covers instrumented APIs only; an
  uninstrumented lock on a cold path would not be caught until it appears in
  the grep sweep.
- Third-party plugin process() internals (VST3/CLAP) — untrusted by policy
  (AGENTS §19); EngineSupervisor timeout/quarantine is the mitigation.
- Windows/macOS driver callback paths — compile-checked only on this machine.

## How to use this

1. After touching RT code, run `scripts/rt_safety_audit.sh --counts` and
   `ctest -R RTAllocationTrapTest`.
2. New grep hits → classify them here in the same PR.
3. Trap failures → the test prints the first violating block; hunt with a
   conditional breakpoint on the trap's operator new.
