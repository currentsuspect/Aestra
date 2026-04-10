# Rejected Patterns

## Fix-One-Miss-Parallel

**Where**: `ProjectSerializer.cpp` — lane color (line 628) vs clip color (line 691)
**What**: The SEC-001 fix wrapped `std::stoul` in try/catch for lane colors but missed the identical pattern on clip colors. Both call sites parse color strings from the same JSON project file with the same untrusted origin.

**Why it's rejected**: A security fix that addresses one call site but not its parallel twins is incomplete. Every time a fix is applied to an input validation pattern, ALL call sites that perform the same operation on the same class of input must be audited and fixed simultaneously. The clip color `stoul` (RTM-009) was a crash vector because the fix was not applied comprehensively.

**Remediation**: The accepted pattern now includes an audit protocol — when fixing one `stoul` call site, scan ALL call sites in the same module.
**Session**: S001, S002

## Simulated Security Tests

**Where**: `tests/security/` — stoul_crash.cpp, json_dos.cpp, div_zero_channels.cpp, id3_overflow.cpp
**What**: The current security tests are standalone demonstrations that prove a vulnerability concept exists. They do NOT exercise the actual vulnerable code paths in the compiled application.

**Why it's rejected**: A test that simulates a vulnerability without testing the actual fix provides no proof that the fix works. Security tests must be integration tests that exercise the actual code paths. The existing tests are useful as documentation but insufficient as proof-of-fix.

**Remediation**: New proof-of-fix tests (`clip_color_stoul.cpp`, `plugin_cache_bounds.cpp`, `fread_truncated_wav.cpp`) test the actual fixed parsing logic with real attack inputs.
**Session**: S002

## Documentation Lag

**Where**: `docs/security/REDTEAM.md` — RTM-001, RTM-002, RTM-003
**What**: Three vulnerabilities were marked as "Open" in the red team docs long after they were fixed in source code. This wasted red team effort during S001 recon and creates false confidence for anyone reading the docs.

**Why it's rejected**: Security documentation must be verified against source code at every session start. Stale docs are worse than no docs — they create a false model of the system. The lab protocol now requires source code verification as part of the recon phase.

**Remediation**: S001 recon included source code verification for all existing vuln statuses. S002 proof-of-fix tests ensure docs stay accurate.
**Session**: S001, S002

## Unbounded Binary Deserialization

**Where**: `PluginScanner.cpp` — loadScanCache()
**What**: The plugin cache binary loader read `count` and string lengths from a file with zero validation. A crafted cache file triggers massive memory allocation (DoS).

**Why it's rejected**: Any binary format that reads a count or length from untrusted input must cap both values before allocating. The 3-layer defense (count cap, string cap, file.good()) is the accepted pattern.
**Session**: S002
