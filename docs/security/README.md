# Aestra Security Lab — Blue Team

## Methodology
Every vulnerability must have:
1. **Proof of existence** — automated test that demonstrates the vuln
2. **Fix** — code change that addresses the root cause
3. **Proof of fix** — same test now passes, proving the vuln is closed

No "trust me bro". Every claim backed by reproducible code.

---

## Vulnerability Register

| ID | Vulnerability | Severity | Status | Test | Fix |
|----|---|---|---|---|---|
| SEC-001 | `std::stoul` crash on malformed project color | Medium | 🔴 Open | `tests/security/stoul_crash.cpp` | `ProjectSerializer.cpp` |
| SEC-002 | JSON parser stack exhaustion (DoS) | High | 🔴 Open | `tests/security/json_dos.cpp` | `AestraJSON.h` |
| SEC-003 | `numChannels == 0` division by zero | Medium | 🔴 Open | `tests/security/div_zero_channels.cpp` | `ProjectSerializer.cpp` |
| SEC-004 | Path traversal in SamplerPlugin loadState | Low-Med | 🔴 Open | `tests/security/sampler_path_traversal.cpp` | `SamplerPlugin.cpp` |
| SEC-005 | Unbounded ID3v2 tag allocation (DoS) | Medium | 🔴 Open | `tests/security/id3_overflow.cpp` | `MetadataParser.cpp` |
| SEC-006 | Metronome WAV parser OOB read | Medium | 🔴 Open | `tests/security/wav_oob_read.cpp` | `MetronomeEngine.cpp` |
| SEC-007 | CLAP/VST3 plugin arbitrary code exec | Critical | 🟡 Monitor | — | By-design (documented risk) |
| SEC-008 | `popen` shell injection (latent) | Low | 🟡 Monitor | — | ShellEscape is correct today |

---

## Test Harness

Each test is a standalone C++ file that:
- Compiles with the project's existing build system
- Runs in CI as part of the confidence suite
- Returns exit code 0 if the fix is in place, non-zero if the vuln is exploitable

Build:
```bash
cmake --build build-dev --target SecurityTests
./build-dev/Tests/SecurityTests
```

---

## Status Legend
- 🔴 Open — vuln exists, fix needed
- 🟡 Monitor — by-design risk or latent, not exploitable today
- 🟢 Closed — fix deployed, test proves it
