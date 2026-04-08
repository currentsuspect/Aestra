# Realtime Lab — Program Constitution

## Scope

This lab targets Linux audio thread real-time scheduling in `AestraAudio`.
The goal is to ensure the audio callback thread runs with proper real-time
priority (SCHED_FIFO or SCHED_RR), not default CFS (SCHED_OTHER).

### The Bug

Discovered during memory-lab session 005:

1. **`RtAudioDriver::openStream()`** sets `RTAUDIO_MINIMIZE_LATENCY` but **not**
   `RTAUDIO_SCHEDULE_REALTIME`. Priority = 0.
2. **`RtAudioDriver::startStream()`** calls `pthread_setschedparam(pthread_self(), SCHED_FIFO, ...)`.
   **This sets priority on the calling (UI) thread, not the audio thread.**
   RtAudio creates its own callback thread internally; by the time
   `startStream()` returns, that thread is already running at SCHED_OTHER.
3. **`mlockall(MCL_CURRENT | MCL_FUTURE)`** works correctly (process-wide).
4. **RtAudio internals**: `RTAUDIO_SCHEDULE_REALTIME` triggers `SCHED_RR` at
   thread creation for PulseAudio/JACK backends. **ALSA does not honor this flag.**

### In-Scope Files

**Linux driver:**
- `AestraAudio/src/Linux/RtAudioDriver.cpp` — openStream, startStream, callback
- `AestraAudio/include/Linux/RtAudioDriver.h` — class definition

**Audio engine:**
- `AestraAudio/src/Core/AudioEngine.cpp` — driver initialization (if needed)

**Tests and diagnostics:**
- `Tests/AestraAudio/RealtimeSchedulingTest.cpp` (new)
- `Tests/CMakeLists.txt` (realtime test targets only)

**Lab infrastructure:**
- `labs/realtime/program.md` — this file
- `labs/realtime/EVALS.md`
- `labs/realtime/LAB_BOOK.md`
- `labs/realtime/run_eval.sh`
- `labs/realtime/result_schema.json`
- `labs/realtime/findings/*`
- `labs/realtime/results/*`
- `labs/realtime/sessions/*`

## Allowed Files

An autonomous agent working in this lab may modify:

1. **Linux driver implementation**:
   - `AestraAudio/src/Linux/RtAudioDriver.cpp`
   - `AestraAudio/include/Linux/RtAudioDriver.h`

2. **Tests and diagnostics** (new files):
   - `Tests/AestraAudio/RealtimeSchedulingTest.cpp`
   - `Tests/AestraAudio/RealtimeLatencyTest.cpp` (new, if needed)

3. **Build files** (only realtime-related targets):
   - `Tests/CMakeLists.txt`

4. **Lab infrastructure**:
   - All files under `labs/realtime/`

## Forbidden Behavior

- **DO NOT** modify unrelated engine code, UI, plugin host, or platform layers.
- **DO NOT** change the public API of `AudioDriver` or `RtAudioDriver`.
- **DO NOT** modify `AestraAudio/External/` submodules (RtAudio source is external).
- **DO NOT** break Windows or macOS builds — changes must be `#ifdef __linux__` guarded.
- **DO NOT** remove or weaken existing test assertions.
- **DO NOT** add clever metaprogramming or template-heavy code.
- **DO NOT** change audio callback semantics — only scheduling priority.

## Invariants

The following invariants MUST be preserved across all changes:

1. **Cross-platform compatibility**: Linux changes must be `#ifdef __linux__` guarded.
   Windows and macOS behavior must remain unchanged.
2. **Audio callback correctness**: Callback must produce identical audio output.
3. **No allocations in callback**: Must remain RT-safe.
4. **Graceful degradation**: If `SCHED_FIFO` fails (no `CAP_SYS_NICE`), fall back to
   `SCHED_OTHER` without crashing.
5. **mlockall preserved**: Process-wide memory locking must continue to work.

## Acceptance Logic

A change is **accepted** only if ALL of the following are true:

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `MathTests` | Exit code 0 |
| `AestraSampleRateConverterTest` | Exit code 0 |
| `OfflineRenderRegressionTest` | Exit code 0 |
| Audio output parity | Bit-identical output |
| Build | Compiles cleanly (no new warnings) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| Scheduling policy | Audio thread runs SCHED_FIFO or SCHED_RR |
| Scheduling priority | Priority > 0 (not default) |
| `mlockall` | Process memory is locked |
| RealtimeLatencyTest | p99 latency < buffer duration |

### Decision Status

Each eval run produces one of:

- **`accept`**: All hard gates pass, advisory gates within tolerance.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but some tests could not run.

## Reporting Format

All eval runs must produce a JSON file at `labs/realtime/results/run_<timestamp>.json`
conforming to `labs/realtime/result_schema.json`.

The eval runner (`labs/realtime/run_eval.sh`) emits a summary at
`labs/realtime/results/summary.json`.

## Default Read Set

When starting a new session, read **only** these files:

1. `program.md` — this file (rules, scope, gates)
2. `EVALS.md` — build commands, eval lanes, thresholds
3. `LAB_BOOK.md` — session summaries, finding pointers
4. `findings/invariants.md` — things that must never break

## Session-End Reporting

At the end of every session:

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update findings files with durable knowledge
3. Update `LAB_BOOK.md` session summary table
4. Commit all lab-book changes as a single commit:
   `realtime-lab: update lab-book after session NNN`
5. Verify clean working tree.

## Git Rules

- Start from a clean working tree.
- Before each round, confirm there are no leftover changes.
- For rejected rounds: `git checkout -- .` to discard.
- For accepted rounds: commit immediately.
- Commit message format: `realtime-lab: accept round NN <short hypothesis>`
- Do not amend unrelated commits.
- End the session with a clean working tree.

## Research Order

1. **Diagnose**: Verify current state — what scheduling policy does the audio thread
   actually run? Write a diagnostic tool that reads `/proc/self/sched` or uses
   `sched_getscheduler()` on the callback thread.
2. **Fix ALSA path**: RtAudio doesn't honor `RTAUDIO_SCHEDULE_REALTIME` for ALSA.
   Either patch the driver layer or implement a post-thread-creation priority setter.
3. **Validate**: Confirm audio thread runs SCHED_FIFO/SCHED_RR with proper priority.
4. **Measure**: Run latency benchmarks to verify the fix improves worst-case latency.
5. **Iterate**: Each change runs through eval harness. Accept or revert.
