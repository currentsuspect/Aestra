# Aestra Security Lab — Blue Team

## Methodology
Every vulnerability must have:
1. **Proof of existence** — automated test that demonstrates the vuln
2. **Fix** — code change that addresses the root cause
3. **Proof of fix** — same test now passes, proving the vuln is closed

No "trust me bro". Every claim backed by reproducible code.

## Running Tests
```bash
cd tests/security
g++ -std=c++17 -o stoul_crash stoul_crash.cpp && ./stoul_crash
g++ -std=c++17 -o json_dos json_dos.cpp && ./json_dos
g++ -std=c++17 -o div_zero_channels div_zero_channels.cpp && ./div_zero_channels
g++ -std=c++17 -o sampler_path_traversal sampler_path_traversal.cpp && ./sampler_path_traversal
g++ -std=c++17 -o id3_overflow id3_overflow.cpp && ./id3_overflow
g++ -std=c++17 -o shell_escape_test shell_escape_test.cpp && ./shell_escape_test
```

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

## Status Legend
- 🔴 Open — vuln exists, fix needed
- 🟡 Monitor — by-design risk or latent, not exploitable today
- 🟢 Closed — fix deployed, test proves it

## Sprint 2 — Blue Team
- **SEC-004**: Added path traversal guard to SamplerPlugin::loadState — rejects absolute paths and `..` components
- **SEC-008**: Added regression test for shellEscape() — verifies POSIX single-quote escaping handles all metacharacters correctly
- **Infrastructure**: Added CMakeLists.txt for security tests, README updated with status table
