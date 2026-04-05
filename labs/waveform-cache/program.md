# Waveform Cache Lab — Program Constitution

## Scope

This lab targets the waveform cache subsystem in `AestraAudio`. It covers:

- `AestraAudio/include/IO/WaveformCache.h`
- `AestraAudio/src/IO/WaveformCache.cpp`
- `AestraAudio/include/DSP/WaveformSIMD.h`
- `Tests/AestraAudio/WaveformCacheTest.cpp`
- `Tests/AestraAudio/WaveformLockTest.cpp`
- `Tests/CMakeLists.txt`

No engine transport, exporter, playlist, plugin-host, or UI widget code is in
scope.

## Allowed Files

An autonomous agent may modify:

1. **Waveform cache implementation**: `WaveformCache.h`, `WaveformCache.cpp`
2. **Test harnesses**: `WaveformCacheTest.cpp`, `WaveformLockTest.cpp`
3. **Lab infrastructure**: `labs/waveform-cache/`
4. **Build files**: Only if required for lab targets (`Tests/CMakeLists.txt`)

## Forbidden Behavior

- **DO NOT** modify unrelated audio-engine, renderer, exporter, or UI code.
- **DO NOT** weaken query correctness or returned peak fidelity to improve
  latency.
- **DO NOT** remove thread-safety or narrow lock coverage without proof.
- **DO NOT** treat a single noisy latency win as acceptance.
- **DO NOT** invent baseline files or fake golden outputs.

## Invariants

1. **Ready semantics**: `isReady()` is false before build completion and true
   after a successful build.
2. **Mip layout**: samples-per-peak grows by the documented multiplier across
   levels.
3. **Peak correctness**: min/max peaks bound the true source samples for the
   queried range.
4. **Query safety**: invalid channels and empty ranges return safe zero peaks,
   not crashes.
5. **Clear semantics**: `clear()` removes levels and resets readiness state.
6. **Concurrency safety**: readers must remain safe while writers rebuild the
   cache.
7. **Level selection discipline**: coarser levels may be chosen for scale, but
   returned peaks must still bound the visible source range.

## Acceptance Logic

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `AestraWaveformCacheTest` | Deterministic cache correctness passes |
| Build | Compiles cleanly for waveform-cache targets |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| `AestraWaveformLockTest` | Reader latency stays below current threshold under concurrent rebuilds |
| Dirty worktree | Logged as maintenance context, never treated as optimization acceptance |

### Decision Status

- **`accept`**: All hard gates pass.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Hard gates pass but advisory latency is noisy or cannot run.

## Default Read Set

1. `program.md`
2. `EVALS.md`
3. `LAB_BOOK.md`
4. `findings/invariants.md`

## Session Budget

- One maintenance or optimization hypothesis at a time.
- Stop after one coherent accepted change or one meaningful rejection.
- Do not widen into UI drawing, clip loading, or exporter work.

## Session-End Reporting

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update findings files with durable lessons
3. Update `LAB_BOOK.md`
4. End with a clean working tree

## Anti-Gaming Rules

- Do not reduce query work or peak fidelity to look faster.
- Do not accept a latency win unless correctness remains identical.
- Do not treat advisory latency data as a keep/revert gate until variance is
  understood.
- If the concurrency lane flakes, mark the run `inconclusive` instead of
  forcing acceptance.

## Git Rules

- Start from a clean working tree.
- Dirty-tree maintenance is allowed only for lab repair and must be recorded.
- For rejected rounds: `git checkout -- .`
- For accepted rounds: commit with `waveform-cache-lab: accept round NN <hypothesis>`
- End with a clean working tree.

## Keep/Revert Discipline

- `accept`: commit stands, update lab memory.
- `reject`: discard uncommitted changes, log the failure.
- `inconclusive`: keep only lab-maintenance changes, not optimization claims.
