# Accepted Patterns

## Dual-Layer Defense for Unbounded Allocation

**Where**: `MiniAudioDecoder.cpp` lines 127-138
**What**: Two independent guards before any allocation from file data:
1. File size consistency check: `expectedDataEnd > fileSize` catches truncated files
2. Absolute cap: `kMaxSamples = 500000000` prevents any single allocation from exceeding ~2 GB

**Why it works**: Layer 1 catches structural inconsistencies (file says it has more data than the file physically contains). Layer 2 catches the edge case where the file is internally consistent but still unreasonably large. Both must fail for an allocation to proceed.
**Session**: S001

## Equivalent Guards on Duplicate Parsers

**Where**: `MiniAudioDecoder.cpp` and `MetronomeEngine.cpp`
**What**: Both independent WAV parsers have equivalent `bitsPerSample` whitelists and allocation caps.

**Why it matters**: A guard on one parser is not sufficient if the other parser handles the same format independently. Both must be hardened to the same standard.
**Session**: S001

## Try/Catch with Safe Fallback for External stoul

**Where**: `UnitManager.cpp` lines 300-305, `ProjectSerializer.cpp` lines 628-632, 692-696
**What**: All `std::stoul` calls on data from external sources (project files, plugin state) are wrapped in try/catch with a defined default value on parse failure.

**Why it works**: Prevents `std::invalid_argument` and `std::out_of_range` from crashing the process. The fallback value (0xFFFFFFFF for color) is visible to the user as an error indicator.

**Audit protocol**: When fixing one `stoul` call site, scan ALL call sites in the same module for the same pattern. SEC-001 fixed line 628 but missed line 691 — caught in S002.
**Session**: S001, S002

## Plugin Cache Binary Bounds Validation

**Where**: `PluginScanner.cpp` lines 361-378
**What**: Three-layer defense for binary cache deserialization:
1. Count cap: `kMaxCachedPlugins = 10000` — no more than 10K cached plugins
2. String length cap: `kMaxStringLen = 65536` — no metadata string > 64KB
3. `file.good()` checks after every `read()` — return empty string on failure

**Why it works**: Each layer blocks a different attack vector. Count cap prevents massive reserve. String cap prevents massive individual allocations. `file.good()` prevents reading garbage past EOF.
**Session**: S002

## fread Return Value Must Be Checked

**Where**: `MetronomeEngine.cpp` lines 122-127, 139-143
**What**: Both 16-bit and 24-bit WAV `fread` calls check return value against expected count. If fewer bytes read than expected, the file is truncated and the parser returns `false`.

**Why it matters**: Unchecked `fread` leaves the remainder of the buffer as uninitialized heap memory. Converting this to audio output creates an information disclosure channel — prior heap contents become audible.
**Session**: S002

## JSON Number Parsing: Pre-Parse Validation + try/catch

**Where**: `AestraJSON.h` lines 391-420
**What**: Two-layer defense for number parsing:
1. Pre-parse structural checks: reject bare `-`, `.`, `-.`, trailing `e`/`E`/`+`
2. try/catch around `std::stod` → returns `JSON(0.0)` on any exception

**Why it works**: The character-level parser can accept structurally-invalid strings that pass the loop but throw from `stod`. The pre-parse check catches known-bad patterns; try/catch catches anything else.
**Session**: S003

## JSON Accessors: Return by Value for Safety

**Where**: `AestraJSON.h` lines 40-63
**What**: Non-const `asArray()` and `asObject()` return by value (not reference). Const versions still return const reference to static empty container.

**Why it works**: Callers who modify the returned value on a type-mismatched JSON get their own temporary copy. No cross-call contamination.
**Session**: S003

## Crash Recovery: Discard > Silent Load

**Where**: `AestraApp.cpp` lines 518-530
**What**: When RecoveryDialog is unavailable, discard the autosave file rather than silently loading it.

**Why it works**: A silently-loaded autosave could have been planted by a local attacker. Discarding it eliminates the attack surface. Trade-off: legitimate autosave lost if dialog unavailable (rare).
**Session**: S003

## Environment Variable Parsing: endptr + isfinite

**Where**: `HeadlessMain.cpp` lines 628-637, 686-694
**What**: All `strtod` calls on env vars use endptr check + `std::isfinite()` + log warning.

**Why it works**: `strtod(nullptr)` silently returns 0.0 on bad input. The endptr check catches all non-numeric input. isfinite rejects nan/inf.
**Session**: S003

## Plugin Cache Integrity: Modification Time Verification

**Where**: `PluginScanner.cpp` cache save/load
**What**: Cache format v2 includes each plugin file's `last_write_time`. On load, current mtime is compared to cached mtime. Mismatch → entry skipped (triggers rescan).

**Why it works**: An attacker replacing a legitimate plugin binary changes the file's mtime. The cache detects this and refuses the stale entry. Backward compatible: v1 caches still load (mtime check skipped for v1).
**Session**: S004

## Crash Recovery: Temporal Consistency Check

**Where**: `AestraApp.cpp` lines 479-505
**What**: Before showing recovery dialog, compare crash flag mtime with autosave mtime. If delta > 5 minutes → discard both files.

**Why it works**: Legitimate crashes produce flag + autosave within seconds. A planted attack creates files at different times. The 5-minute window is generous for real crashes but defeats most naive pre-seeding attacks.
**Session**: S004

## Plugin Loading: Trusted Path Allowlist + First-Load Warning

**Where**: `PluginScanner.h/.cpp`
**What**: `isTrustedPath()` classifies system paths as trusted. Untrusted paths trigger a callback on first load. Seen-plugins set prevents repeated prompts.

**Why it works**: System paths (`/usr/lib/*`, Program Files, `/Library/*`) are assumed safe (require root to write). User paths (`~/.vst3`, custom installs) prompt once — like browser extension warnings. The callback is optional (legacy behavior preserved if not wired).
**Session**: S004

## FLAC Vorbis Comment: vendorLen Bounds Check

**Where**: `MetadataParser.cpp:281-282`
**What**: `if (vendorLen > blockSize - 4) continue;` before using vendorLen to compute pos.

**Why it works**: Prevents theoretical integer overflow where `4 + vendorLen` wraps below `blockSize`. The 10MB block cap makes exploitation infeasible, but this check is defense-in-depth at zero cost.
**Session**: S004

## CLI Flags Over Env Vars for Security-Sensitive Configuration

**Where**: `HeadlessMain.cpp`
**What**: `AESTRA_PROJECT` env var ignored with warning. Users must use `--project <path>`.

**Why it works**: CLI flags are visible in CI logs (auditable, can be reviewed). Env vars are silent (supply-chain risk). This eliminates a CI injection vector while preserving functionality.
**Session**: S004
