# DSP Filter Lab — Program Constitution

## Scope

This lab targets the biquad filter subsystem in `AestraAudio`. It covers:

- `AestraAudio/include/DSP/Filter.h`
- `AestraAudio/src/DSP/Filter.cpp`
- `AestraAudio/include/DSP/FilterSIMD.h`
- `AestraAudio/include/DSP/MixerSIMD.h`
- `AestraAudio/include/DSP/WaveformSIMD.h`
- `Tests/AestraAudio/FilterTest.cpp`
- `Tests/AestraAudio/PerformanceStressTest.cpp` (broad advisory benchmark only)
- `Tests/CMakeLists.txt` (target wiring only, if needed)

No other engine, UI, plugin-host, or platform code is in scope.

## Allowed Files

An autonomous agent may modify:

1. **Filter implementation**: `Filter.h`, `Filter.cpp`, `FilterSIMD.h`
2. **Test harnesses**: `FilterTest.cpp`, `PerformanceStressTest.cpp`
3. **Lab infrastructure**: `labs/dsp-filter/`
4. **Build files**: Only if required for lab targets (`Tests/CMakeLists.txt`)

## Forbidden Behavior

- **DO NOT** modify unrelated DSP, audio, UI, or platform code.
- **DO NOT** weaken or remove test assertions.
- **DO NOT** change filter type semantics (low-pass must still low-pass, etc.).
- **DO NOT** remove oversampling or saturation features to improve performance.
- **DO NOT** invent baseline files or fake golden outputs.
- **DO NOT** claim a performance win from `AestraAudioPerformanceTest`; it is a
  broad engine benchmark, not a trusted filter-only lane.

## Invariants

1. **BIBO stability**: Bounded input produces bounded output (no NaN/Inf).
2. **Low-pass**: Attenuates frequencies above cutoff.
3. **High-pass**: Attenuates frequencies below cutoff.
4. **Band-pass**: Passes frequencies in the specified band.
5. **Resonance**: Boosts response at cutoff frequency when Q > 0.
6. **Reset**: `reset()` clears all internal state to zero.
7. **Oversampling**: 2x/4x oversampling produces cleaner output at high frequencies.
8. **Parameter smoothing**: No clicks during parameter changes.

## Acceptance Logic

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `AestraFilterTest` | All tests pass (exit code 0) |
| Build | Compiles cleanly (no new warnings in filter code) |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| Dirty worktree | Logged as maintenance context, never treated as optimization acceptance |
| Broad performance surface | `AestraAudioPerformanceTest` may be captured as context only, not as a keep/revert gate |

### Decision Status

- **`accept`**: All hard gates pass.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but tests could not run.

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, eval lanes
3. `LAB_BOOK.md` — session summaries
4. `findings/invariants.md` — things that must never break

## Session Budget

- One maintenance or optimization hypothesis at a time.
- Stop after the first coherent repair or after one accepted filter change.
- Do not widen from filter code into unrelated engine work.

## Session-End Reporting

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update durable findings only when an observation is real and reusable
3. Update `LAB_BOOK.md`
4. End with a clean working tree

## Anti-Gaming Rules

- Do not treat an answered prompt or scripted stdin as a product improvement.
- Do not accept a round on broad engine timing alone.
- If a result depends on manual audio listening, mark that part human-in-the-loop
  and keep it outside autonomous acceptance.

## Git Rules

- Start from a clean working tree.
- Dirty-tree maintenance is allowed only for lab repair and must be recorded.
- For rejected rounds: `git checkout -- .`
- For accepted rounds: commit with `dsp-filter-lab: accept round NN <hypothesis>`
- End with a clean working tree.

## Keep/Revert Discipline

- `accept`: commit stands, update baselines.
- `reject`: `git checkout -- .` to discard, log failure.
- Never accumulate rejected changes.
