# Security Lab — Program Constitution

## Scope

This lab targets the security surface of the entire Aestra codebase. It covers:

- **File I/O parsers**: WAV, ID3v2, JSON project files, plugin cache binary
- **Deserialization**: `ProjectSerializer.cpp`, `UnitManager.cpp`, JSON parser depth limits
- **Plugin loading**: CLAP/VST3 `dlopen`/`LoadLibrary` attack surface
- **Shell execution**: `popen`-based file dialogs and shell escape functions
- **Environment variable injection**: Headless mode env var overrides
- **Crash recovery**: crash flag + autosave pre-seeding attack surface
- **Security test harnesses**: `tests/security/`, `tests/redteam/`
- **Security documentation**: `docs/security/`, `SECURITY.md`

No UI, audio engine, DSP algorithm, or unrelated platform code is in scope unless
it has a direct security implication.

## Allowed Files

An autonomous agent may modify:

1. **Security-critical implementation**: Any parser, deserializer, loader, or sanitizer that processes untrusted input
2. **Security test harnesses**: `tests/security/`, `tests/redteam/`
3. **Lab infrastructure**: `labs/security/`
4. **Security documentation**: `docs/security/`, `SECURITY.md`
5. **Build files**: Only if required for security test targets (`tests/security/CMakeLists.txt`, root `CMakeLists.txt`)
6. **Shell escape utilities**: `AestraPlat/src/Linux/PlatformUtilsLinux.cpp`

## Forbidden Behavior

- **DO NOT** modify unrelated DSP, audio, UI, or platform code.
- **DO NOT** weaken or remove test assertions in security tests.
- **DO NOT** disable or bypass existing security guards.
- **DO NOT** change filter/audio algorithm semantics to fix security issues.
- **DO NOT** remove plugin loading functionality to eliminate plugin attack surface (documented risk is acceptable).
- **DO NOT** invent baseline files or fake security test outputs.
- **DO NOT** claim a security fix without a proof-of-fix test that demonstrates the vuln is closed.
- **DO NOT** accept "monitor" status as a fix — monitor means documented, not resolved.

## Invariants

1. **No unguarded division on external input**: Any value from a file, env var, or plugin must be validated before use in division, modulo, or array indexing.
2. **No unbounded allocation from external input**: Any size field from a file must be capped at a reasonable maximum before `resize()`, `malloc()`, or `new[]`.
3. **No uncaught `std::stoul`/`std::stoi`/`std::stod` on external input**: All string-to-number conversions from untrusted sources must be wrapped in try/catch.
4. **No shell metacaracter injection**: Any value passed to `popen()`, `system()`, or equivalent must be escaped with POSIX single-quote wrapping (`shellEscape()`).
5. **JSON parse depth must be bounded**: Recursive JSON parsing must enforce a hard depth limit (`kMaxJsonDepth`).
6. **Path traversal must be rejected**: File paths constructed from project state must reject `..` components and absolute paths outside allowed directories.
7. **No silent crash on malformed input**: Malformed files must produce error returns, not crashes, NaN, or undefined behavior.
8. **Plugin loading is accepted risk**: VST3/CLAP arbitrary code execution is by-design. The lab documents it but does not attempt to eliminate it.
9. **Every fix must have a proof test**: A security fix is not complete without a test that (a) demonstrated the vuln before the fix, and (b) passes after the fix.
10. **Crash flag must not trigger silent project load**: The crash recovery flow must validate the autosave file before loading it.

## Acceptance Logic

### Hard Gates (must pass)

| Gate | Target |
|------|--------|
| All security tests pass | Every test in `tests/security/` exits 0 |
| All red team PoCs are mitigated | PoCs that previously crashed now return error gracefully |
| Build compiles cleanly | No new warnings in security-critical code |
| No regressions in existing tests | `MathTests`, `AestraFilterTest` still pass |
| Proof-of-fix for every new vulnerability | Each new vuln fix has a corresponding test |

### Advisory Gates (tracked, non-blocking)

| Gate | Target |
|------|--------|
| Dirty worktree | Logged as maintenance context |
| Monitor-status vulnerabilities | Logged for tracking, not a gate failure |
| Performance regression on hot paths | Parser guards must not add > 1 µs overhead on normal input |

### Decision Status

- **`accept`**: All hard gates pass, every new vuln has proof-of-fix.
- **`reject`**: Any hard gate fails, or a vuln fix lacks a proof test.
- **`inconclusive`**: Build succeeded but security tests could not run.

## Default Read Set

1. `program.md` — rules, scope, gates
2. `EVALS.md` — build commands, eval lanes
3. `LAB_BOOK.md` — session summaries
4. `findings/invariants.md` — things that must never break
5. `docs/security/REDTEAM.md` — current red team findings
6. `docs/security/README.md` — blue team vulnerability register

## Session Budget

- One vulnerability fix or security hardening task at a time.
- Stop after the first coherent repair or one accepted security change.
- Do not widen from security-critical code into unrelated engine work.
- Red team analysis sessions are allowed to survey and document without fixing.

## Session-End Reporting

1. Write a session log to `sessions/<date>_session_<NNN>.md`
2. Update durable findings only when an observation is real and reusable
3. Update `LAB_BOOK.md`
4. End with a clean working tree

## Anti-Gaming Rules

- Do not treat removing a test as a security improvement.
- Do not accept "monitor" status as a resolution — the vuln is still open, just documented.
- Do not weaken input validation to make a parser "faster".
- Do not claim a vuln is fixed without a proof-of-fix test.
- Do not accept broad code timing as a security benchmark.

## Git Rules

- Start from a clean working tree.
- Dirty-tree maintenance is allowed only for lab repair and must be recorded.
- For rejected rounds: `git checkout -- .`
- For accepted rounds: commit with `security-lab: accept round NN <hypothesis>`
- End with a clean working tree.

## Keep/Revert Discipline

- `accept`: commit stands, update docs and findings.
- `reject`: `git checkout -- .` to discard, log failure.
- Never accumulate rejected changes.

## Red Team Protocol

Red team sessions operate under a different workflow than blue team fix sessions:

1. **Recon**: Read current security docs, source code, and test outputs. Document findings.
2. **Exploit**: Write or update PoC scripts that demonstrate vulnerabilities.
3. **Report**: Update `docs/security/REDTEAM.md` with new findings or status changes.
4. **No code fixes during red team sessions**: Red team documents vulns; blue team fixes them.

Red team sessions end with documentation updates, not code commits.
