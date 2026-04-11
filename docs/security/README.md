# Aestra Security Lab — Blue Team

## Private Disclosure
Report vulnerabilities privately to `makoridylangmail.com` with the subject prefix
`SECURITY: [summary]`. Acknowledgment SLA: 72 hours.

## Methodology
Every vulnerability must have:
1. **Proof of existence** — automated test that demonstrates the vuln
2. **Fix** — code change that addresses the root cause
3. **Proof of fix** — same test now passes, proving the vuln is closed

No "trust me bro". Every claim backed by reproducible code.

## Running Tests

```bash
cmake -S . -B build/tests-security -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests-security --parallel
ctest --test-dir build/tests-security -V
```

Targeted security-only runs:

```bash
ctest --test-dir build/tests-security -R 'Sec'
```

Keep this README aligned with the repository security targets in
`tests/security/CMakeLists.txt` and the CI workflow that runs them.

## Vulnerability Register

| ID | Vulnerability | Severity | Status | Test | Fix Commit |
|----|---|---|---|---|---|
| SEC-001 | `std::stoul` crash on malformed project color | Medium | 🟢 Fixed | `stoul_crash.cpp` | `d7c00a48` |
| SEC-002 | JSON parser stack exhaustion (DoS) | High | 🟢 Fixed | `json_dos.cpp` | `d7c00a48` |
| SEC-003 | `numChannels == 0` division by zero | Medium | 🟢 Fixed | `div_zero_channels.cpp` | `d7c00a48` |
| SEC-004 | Path traversal in SamplerPlugin loadState | Low-Med | 🟢 Fixed | `sampler_path_traversal.cpp` | Sprint 2 |
| SEC-005 | Unbounded ID3v2 tag allocation (DoS) | Medium | 🟢 Fixed | `id3_overflow.cpp` | `d7c00a48` |
| SEC-006 | Metronome WAV parser OOB read | Medium | 🟢 Fixed | (confidence suite) | `d7c00a48` |
| SEC-007 | CLAP/VST3 plugin arbitrary code exec | Critical | 🟡 Monitor | — | Documented risk |
| SEC-008 | `popen` shell injection (regression test) | Low | 🟢 Fixed | `shell_escape_test.cpp` | Sprint 2 |
| SEC-009 | Plugin cache binary spoofing | High | 🟡 Monitor | — | Requires local write access; no integrity on `plugin_cache.bin` |
| SEC-010 | Crash flag + autosave pre-seeding | High | 🟡 Monitor | — | Local attacker can force-load crafted project on startup |
| SEC-011 | Headless env var injection (`AESTRA_PROJECT`) | Medium | 🟡 Monitor | — | CI supply chain; env var overrides project path |

## Status Legend
- 🔴 Open — vuln exists, fix needed
- 🟡 Monitor — by-design risk or latent, not exploitable today
- 🟢 Closed — fix deployed, test proves it

## Sprint 2 — Blue Team
- **SEC-004**: Added path traversal guard to SamplerPlugin::loadState — rejects absolute paths and `..` components
- **SEC-008**: Added regression test for shellEscape() — verifies POSIX single-quote escaping handles all metacharacters correctly
- **Infrastructure**: Added CMakeLists.txt for security tests, README updated with status table

## Red Team Cross-Reference

Mapping between red team findings (RTM-xxx) and blue team vulnerabilities (SEC-xxx):

| Red Team | Blue Team | Relationship | Status Match |
|----------|-----------|-------------|-------------|
| RTM-001 (WAV div-by-zero) | SEC-003 (numChannels div-zero), SEC-006 (Metronome WAV) | RTM-001 covers the attack; SEC-003/SEC-006 cover the fixes | ✅ Both Fixed |
| RTM-002 (WAV heap exhaustion) | SEC-005 (ID3v2 unbounded alloc) | Same class of vuln (unbounded allocation), different parsers | ✅ Both Fixed |
| RTM-003 (UnitManager stoul) | SEC-001 (stoul crash) | Same root cause, different call sites | ✅ Both Fixed |
| RTM-004 (JSON stack exhaustion) | SEC-002 (JSON DoS) | Same vuln, different perspectives | ✅ Both Mitigated |
| RTM-005 (Plugin RCE) | SEC-007 (Plugin arbitrary code exec) | Same vuln | ✅ Both By-design |
| RTM-006 (Plugin cache spoofing) | — | No blue team entry yet | ⚠️ Needs SEC entry |
| RTM-007 (Crash flag + autosave) | — | No blue team entry yet | ⚠️ Needs SEC entry |
| RTM-008 (Headless env var) | — | No blue team entry yet | ⚠️ Needs SEC entry |

### Documentation Audit Notes
- RTM-001, RTM-002, RTM-003 were previously marked 🔴 Open in REDTEAM.md but verified as 🟢 Fixed in source code (as of 2026-04-10 red team session S001). Status corrected.
- RTM-006, RTM-007, RTM-008 need corresponding SEC entries in the blue team register for full traceability.
