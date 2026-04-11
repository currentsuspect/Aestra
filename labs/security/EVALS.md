# Security Lab — Eval Documentation

## Build Commands

### Security Tests (CMake targets)

```bash
cmake -S . -B build-autoresearch \
  -DAestra_CORE_MODE=ON \
  -DAESTRA_HEADLESS_ONLY=ON \
  -DAESTRA_ENABLE_UI=OFF \
  -DAESTRA_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

# Build all security test targets
cmake --build build-autoresearch --target SecStoulCrash SecJsonDos SecDivZero SecSamplerPath SecId3Overflow SecShellEscape --parallel 2
```

### Individual Security Tests (standalone compilation)

If CMake targets are not available, compile and run individually:

```bash
cd tests/security
g++ -std=c++17 -o stoul_crash stoul_crash.cpp && ./stoul_crash
g++ -std=c++17 -o json_dos json_dos.cpp && ./json_dos
g++ -std=c++17 -o div_zero_channels div_zero_channels.cpp && ./div_zero_channels
g++ -std=c++17 -o sampler_path_traversal sampler_path_traversal.cpp && ./sampler_path_traversal
g++ -std=c++17 -o id3_overflow id3_overflow.cpp && ./id3_overflow
g++ -std=c++17 -o shell_escape_test shell_escape_test.cpp && ./shell_escape_test
```

### Red Team PoC Execution

```bash
cd tests/redteam
python3 poc_wav_divzero.py /tmp/poc_divzero.wav
python3 poc_wav_heap_exhaust.py /tmp/poc_heap.wav
python3 poc_unitmanager_stoul.py /tmp/poc_stoul.aes
python3 poc_json_stack.py /tmp/poc_deep.aes
```

## Eval Lanes

### Lane 1: Security Tests (Hard Gate)

Runs all security test binaries. Each must exit 0.

| Test | What it proves |
|------|---------------|
| `stoul_crash` | `std::stoul` on malformed input is caught (SEC-001 / RTM-003) |
| `json_dos` | JSON parser enforces depth limit, no stack overflow (SEC-002 / RTM-004) |
| `div_zero_channels` | Zero-channel WAV does not cause SIGFPE (SEC-003 / RTM-001) |
| `sampler_path_traversal` | Sampler rejects `..` and absolute paths (SEC-004) |
| `id3_overflow` | ID3v2 tag with huge size does not cause OOM (SEC-005 / RTM-002) |
| `shell_escape_test` | `shellEscape()` handles all POSIX metacharacters (SEC-008) |

**Gate**: HARD — every test must exit 0.

### Lane 2: Red Team PoC Mitigation (Hard Gate)

Runs red team PoC scripts against the current codebase. Each PoC that previously caused a crash must now either:
- Fail gracefully (return error, not crash)
- Be demonstrably mitigated by a guard in the code

| PoC | Target vuln | Expected result after fix |
|-----|------------|--------------------------|
| `poc_wav_divzero.py` | RTM-001: WAV div-by-zero | Parser rejects bitsPerSample=0 |
| `poc_wav_heap_exhaust.py` | RTM-002: WAV heap exhaustion | Parser caps allocation at file size |
| `poc_unitmanager_stoul.py` | RTM-003: UnitManager stoul | try/catch returns default color |
| `poc_json_stack.py` | RTM-004: JSON stack exhaustion | Parser truncates at depth limit |

**Gate**: HARD — all open/critical PoCs must be mitigated. Monitor-status PoCs (RTM-005 through RTM-008) are advisory only.

### Lane 3: Regression Tests (Hard Gate)

Runs existing correctness tests to ensure security guards do not break valid functionality:

| Test | What it proves |
|------|---------------|
| `MathTests` | Security changes to math code (if any) do not break correctness |
| `AestraFilterTest` | Security changes to DSP code (if any) do not break filter behavior |

**Gate**: HARD — exit code must be 0.

## Hard Gates vs Advisory Gates

| Gate | Type | Failure Action |
|------|------|----------------|
| Security tests (all) exit 0 | HARD | Reject + revert |
| Red team PoCs mitigated (RTM-001 through RTM-004) | HARD | Reject + revert |
| Build warnings in security code | HARD | Reject + investigate |
| Regression tests pass | HARD | Reject + investigate |
| Dirty worktree | ADVISORY | Record maintenance context |
| Monitor-status vulns (RTM-005 through RTM-008) | ADVISORY | Document, do not gate |

## Baseline Policy

- The first clean pass captures `results/summary.json` with the current state of all security tests.
- No performance baseline is required — security tests are pass/fail, not benchmarked.
- Red team PoC outputs are captured as text files in `results/` for audit trail.

## Noise Policy

- Security tests are fully deterministic — no concurrency, no I/O beyond file parsing.
- Red team PoCs are Python scripts that generate deterministic test files.
- If a test fails or PoC behaves unexpectedly, mark the run `inconclusive`,
  re-run once, and do not accept any security claims from that round.

## Acceptance Thresholds

| Metric | Threshold |
|--------|-----------|
| Security test pass rate | 100% |
| Red team PoC mitigation rate | 100% for open/critical (RTM-001 through RTM-004) |
| Build warnings | 0 new warnings in security-critical code |
| Regression test pass rate | 100% |
