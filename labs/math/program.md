# Math Lab — Program Constitution

## Scope

This lab targets the math utilities in `AestraCore`. It covers:

- `AestraCore/include/AestraMath.h` (header-only: Vector2/3/4, Matrix4x4, DSP math)
- `AestraCore/src/MathTests.cpp`
- `AestraCore/src/MathBenchmark.cpp`
- `AestraCore/CMakeLists.txt` (target wiring only, if needed)

No other engine, audio, UI, or platform code is in scope.

## Allowed Files

An autonomous agent may modify:

1. **Math implementation**: `AestraCore/include/AestraMath.h`
2. **Test harnesses**: `AestraCore/src/MathTests.cpp`
3. **Benchmark harnesses**: `AestraCore/src/MathBenchmark.cpp`
4. **Lab infrastructure**: `labs/math/`
5. **Build files**: Only if required for lab targets (`AestraCore/CMakeLists.txt`)

## Forbidden Behavior

- **DO NOT** modify unrelated core, audio, UI, or platform code.
- **DO NOT** weaken or remove test assertions.
- **DO NOT** change the numerical results of correct functions (fixing bugs is allowed, changing behavior is not).
- **DO NOT** break existing consumers (`AudioEngine.cpp`, `AudioRenderer.cpp` using `dbToGain`).
- **DO NOT** invent baseline files or fake golden outputs.
- **DO NOT** claim throughput wins until a deterministic benchmark lane exists.

## Invariants

1. **`dbToGain(0.0f) == 1.0f`**: 0 dB is unity gain.
2. **`gainToDb(1.0f) == 0.0f`**: Unity gain is 0 dB.
3. **`dbToGain(-90.0f) == 0.0f`**: Below -90 dB is silence.
4. **`gainToDb(0.0f)` must not produce `-inf` or `NaN`**: Returns a defined floor value.
5. **`map()` must not produce `NaN` when `inMax == inMin`**: Returns `outMin` or handles gracefully.
6. **`lerp(a, b, 0.0f) == a` and `lerp(a, b, 1.0f) == b`**.
7. **Vector identity**: `v.normalized().length()` is `1.0f` for non-zero `v`.
8. **Matrix identity**: `Matrix4x4::identity() * v == v` for any Vector4.
9. **No silent NaN propagation**: Operations on valid inputs must not produce NaN/Inf.
10. **Cross product**: `X × Y == Z` for unit basis vectors.

## Acceptance Logic

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| `MathTests` | All test cases pass (exit code 0) |
| `MathBenchmark` | XRUN rate < 0.1%, deadline miss rate < 0.1% |
| Build | Compiles cleanly (no new warnings in math code) |
| Consumer compatibility | `AudioEngine.cpp` and `AudioRenderer.cpp` still build |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| Dirty worktree | Logged as maintenance context |
| Median regression | > 20% regression on any benchmark median vs baseline |

### Decision Status

- **`accept`**: All hard gates pass.
- **`reject`**: Any hard gate fails.
- **`inconclusive`**: Build succeeded but tests/benchmarks could not run.

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, eval lanes
3. `LAB_BOOK.md` — session summaries
4. `findings/invariants.md` — things that must never break

## Session Budget

- One maintenance or optimization hypothesis at a time.
- Stop after the first coherent repair or one accepted math change.
- Do not widen from math code into unrelated engine work.

## Session-End Reporting

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update durable findings only if a real lesson was observed
3. Update `LAB_BOOK.md` with the session outcome
4. End with a clean working tree

## Anti-Gaming Rules

- Do not treat numerical tolerance tightening as a product improvement.
- Do not accept a round on broad engine timing alone.
- Do not weaken test assertions to make them pass.

## Git Rules

- Start from a clean working tree.
- Dirty-tree maintenance is allowed only for lab repair and must be recorded.
- For rejected rounds: `git checkout -- .`
- For accepted rounds: commit with `math-lab: accept round NN <hypothesis>`
- End with a clean working tree.

## Keep/Revert Discipline

- `accept`: commit stands, update baselines.
- `reject`: `git checkout -- .` to discard, log failure.
- Never accumulate rejected changes.
