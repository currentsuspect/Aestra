# Aestra Security Lab — Red Team

## Findings

| ID | Vulnerability | Impact | Difficulty | PoC | Status |
|----|---|---|---|---|---|
| RTM-001 | WAV div-by-zero (bitsPerSample=0) | SIGFPE crash | Script Kiddie | `poc_wav_divzero.py` | 🟢 Fixed |
| RTM-002 | WAV heap exhaustion (dataSize=0xFFFFFFFF) | OOM crash | Script Kiddie | `poc_wav_heap_exhaust.py` | 🟢 Fixed |
| RTM-003 | UnitManager std::stoul crash | Crash on project load | Script Kiddie | `poc_unitmanager_stoul.py` | 🟢 Fixed |
| RTM-004 | JSON parser stack exhaustion | Stack overflow (without fix) | Script Kiddie | `poc_json_stack.py` | 🟡 Mitigated |
| RTM-005 | VST3/CLAP arbitrary code execution | **RCE** | Script Kiddie | By-design (documented) | 🟢 Mitigated (first-load warning + trusted paths) |
| RTM-006 | Plugin cache binary spoofing | Indirect RCE | Skilled | Documented | 🟢 Fixed (mtime integrity verification) |
| RTM-007 | Crash flag + autosave pre-seeding | Forced recovery → exploit chain | Script Kiddie | Documented | 🟢 Fixed (timestamp delta check) |
| RTM-008 | Headless env var injection | CI supply chain | Script Kiddie | Documented | 🟢 Fixed (env var ignored, --project flag required) |
| RTM-009 | Clip color std::stoul crash | Crash on project load | Script Kiddie | `poc_clip_stoul.py` | 🟢 Fixed |
| RTM-010 | Plugin cache unbounded alloc | DoS (OOM) | Script Kiddie | `poc_plugin_cache_dos.py` | 🟢 Fixed |
| RTM-011 | MetronomeEngine fread unchecked | Uninitialized memory leak | Skilled | `poc_metronome_fread.py` | 🟢 Fixed |
| RTM-012 | FLAC vendorLen integer overflow | Theoretical OOB read | Expert | — | 🟢 Fixed (bounds check) |
| RTM-013 | JSON parseNumber stod crash | Crash on malformed number | Script Kiddie | — | 🟢 Fixed |
| RTM-014 | JSON asArray() mutable static | Cross-request contamination | Expert | — | 🟢 Fixed |
| RTM-015 | Silent autosave on recovery | Forced malicious project load | Script Kiddie | Documented | 🟢 Fixed |
| RTM-016 | Headless env var strtod silent failure | Silent parameter override | Script Kiddie | Documented | 🟢 Fixed |

---

## RTM-001: WAV Parser Division by Zero

**File:** `MiniAudioDecoder.cpp:135`, `MetronomeEngine.cpp:91-99`

**Original attack:** Craft a WAV with `bitsPerSample = 0`. Division by zero → SIGFPE.

**Fix — MiniAudioDecoder.cpp (line 123):**

```cpp
if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
    return false;
```

This guard runs 12 lines before the division at line 135, ensuring `bitsPerSample / 8` is always 2, 3, or 4 — never zero.

**Fix — MetronomeEngine.cpp (lines 91-94, 99):**

```cpp
if (bitsPerSample != 16 && bitsPerSample != 24) {
    fclose(file);
    return false;
}
```

Redundant guard at line 99:

```cpp
if (audioFormat != 1 || (bitsPerSample != 16 && bitsPerSample != 24)) {
    fclose(file);
    return false;
}
```

**Status:** 🟢 Fixed — both independent WAV parsers have equivalent guards.

---

## RTM-002: WAV Parser Heap Exhaustion

**File:** `MiniAudioDecoder.cpp:127-138`, `MetronomeEngine.cpp:103-106`

**Original attack:** Craft a WAV with `dataSize = 0xFFFFFFFF`. Allocates ~8.5 GB.

**Fix — MiniAudioDecoder.cpp dual-layer defense:**

Layer 1 — file size consistency (lines 127-130):

```cpp
file.seekg(0, std::ios::end);
std::streamoff fileSize = file.tellg();
std::streamoff expectedDataEnd = static_cast<std::streamoff>(dataPos) + static_cast<std::streamoff>(dataSize);
if (expectedDataEnd > fileSize || dataSize == 0) {
    return false;
}
```

Layer 2 — absolute sample cap (lines 136-138):

```cpp
constexpr size_t kMaxSamples = 500000000;
if (samplesCount > kMaxSamples) {
    return false;
}
```

**Fix — MetronomeEngine.cpp (lines 103-106):**
```cpp
const uint32_t numSamples = chunkLen / (numChannels * bytesPerSample);
// Guard against excessive allocation from malformed chunkLen
if (numSamples > 10000000) { // ~10M samples max (~200 seconds at 48kHz)
    fclose(file);
    return false;
}
```

**Status:** 🟢 Fixed — both parsers have hard caps on allocation size.

---

## RTM-003: UnitManager std::stoul Crash

**File:** `UnitManager.cpp:300-305`

**Original attack:** Crafted `.aes` project with `"color": "not_a_number"` in arsenal unit.

**Fix — try/catch wrapper (lines 300-305):**
```cpp
try {
    unit.color = static_cast<uint32_t>(std::stoul(ju["color"].asString()));
} catch (const std::exception&) {
    unit.color = 0xFFFFFFFF; // Default white on parse error
}
```

On any parse exception (empty string, non-numeric, out of range), the code falls back to a default color value rather than crashing.

**Status:** 🟢 Fixed — try/catch with safe fallback.

---

## RTM-004: JSON Parser Stack Exhaustion

**File:** `AestraJSON.h` — `kMaxJsonDepth = 1024`

**Attack:** 50,000-level nesting → before fix: stack overflow. After fix: returns empty JSON.


```bash
python3 tests/redteam/poc_json_stack.py /tmp/poc_deep.aes
# 100,000 bytes. Parser truncates at depth 1024.
```

**With the fix:** Parser returns empty JSON at depth 1025. The project loads as an empty shell — functional DoS (victim sees empty project).

**Without the fix:** Stack overflow at ~50k frames → SIGSEGV.

**Status:** 🟡 Mitigated — depth limit prevents crash but empty project is still a mild DoS.

---

## RTM-005: Plugin Arbitrary Code Execution

**Files:** `CLAPHost.cpp:139`, `VST3Host.cpp:93`
```cpp
// Linux
m_library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
// Windows
LoadLibraryW(path.wstring().c_str());
```

**Attack:** Drop malicious `.clap`/`.vst3` in scanned directory → RCE on startup.

**Exploit chain:**
1. Attacker places `malicious.clap` (shared library with `clap_entry` export) in `~/.clap/`
2. Aestra startup scans `~/.clap/` → `dlopen("malicious.clap")`
3. `clap_entry()` executes with full user privileges
4. Reverse shell, keylogger, persistence — anything

**Proof of concept (conceptual):**
```bash
# Build a shared library that spawns a shell on load
cat > /tmp/malicious.c << 'EOF'
#include <stdlib.h>
__attribute__((constructor)) void init() { system("xcalc &"); }
// CLAP exports
const void* clap_entry(const char* path, const clap_plugin_info** info) { return NULL; }
EOF
gcc -shared -fPIC -o ~/.clap/malicious.clap /tmp/malicious.c
# Next Aestra startup → xcalc pops
```

**Status:** 🟡 By-design — DAWs must load plugins. Mitigation would require sandboxing or signature verification.

---

## RTM-006: Plugin Cache Binary Spoofing

**File:** `PluginScanner.cpp:305-358`

The binary cache (`plugin_cache.bin`) has no integrity check. An attacker with local write access can inject fake plugin entries pointing to malicious DLLs.

**Status:** 🟡 Monitor — requires local write access to AppData directory.

---

## RTM-007: Crash Flag + Autosave Pre-seeding

**File:** `AestraApp.cpp:103-117`, `AestraApp.cpp:478-530`

Any local user can create `~/.local/share/Aestra/crash_flag` and plant a crafted `autosave.aes`. On next startup, Aestra force-loads the attacker's project, triggering any of the above vulnerabilities automatically.

**Status:** 🟡 Monitor — requires local file access.

---

## RTM-008: Headless Environment Variable Injection

**File:** `HeadlessMain.cpp:628`, `:685`

In CI environments, `AESTRA_PROJECT` overrides the project path. A malicious CI job can set this to trigger project-loading vulnerabilities during automated test runs.

**Status:** 🟡 Monitor — CI-specific attack surface.

---

## RTM-009: Clip Color std::stoul Crash

**File:** `Source/Core/ProjectSerializer.cpp:691`

**Attack:** Crafted `.aes` project with `"color": "not_a_number"` on a clip entry.

```json
{"clips": [{"color": "not_a_number", ...}]}
```

**Why it existed:** The SEC-001 fix wrapped `std::stoul` in try/catch for lane colors (line 628) but missed the identical pattern on clip colors (line 691). Both parse color strings from the same JSON project file.

**Fix (S002):** Wrapped in try/catch with `0xFFFFFFFF` fallback — same pattern as lane color fix.

**Status:** 🟢 Fixed — proof-of-fix test `clip_color_stoul.cpp` passes.

---

## RTM-010: Plugin Cache Unbounded Allocation

**File:** `AestraAudio/src/Plugin/PluginScanner.cpp:361-373`

**Attack:** Crafted `plugin_cache.bin` with `count=0xFFFFFFFF` or string `len=0xFFFFFFFF`.

**Exploit chain:**
1. Attacker writes crafted `plugin_cache.bin` to cache directory
2. `loadScanCache()` reads `count` with no validation
3. `plugins.reserve(0xFFFFFFFF)` → attempts ~17 billion entries
4. `readString()` reads unchecked `uint32_t` lengths → `std::string s(0xFFFFFFFF, '\0')` → ~4 GB

**Fix (S002):** Three-layer defense:
1. Count cap: `kMaxCachedPlugins = 10000`
2. String length cap: `kMaxStringLen = 65536`
3. `file.good()` checks after each read

**Status:** 🟢 Fixed — proof-of-fix test `plugin_cache_bounds.cpp` passes.

---

## RTM-011: MetronomeEngine fread Unchecked Return

**File:** `AestraAudio/src/Playback/MetronomeEngine.cpp:122,132`

**Attack:** Truncated WAV file — header claims more data than the file contains.

**Exploit chain:**
1. Attacker crafts WAV with header claiming 1000 samples, file contains only 50
2. `fread(rawData.data(), 2, 1000, file)` reads 50 items, returns 50 (not 1000)
3. Return value NOT checked — 950 samples of `rawData` contain uninitialized heap memory
4. Uninitialized memory converted to float audio samples and played
5. **Impact:** Information disclosure — prior heap contents audible as noise

**Fix (S002):** Both 16-bit and 24-bit `fread` calls now check return value; return `false` on short read.

**Status:** 🟢 Fixed — proof-of-fix test `fread_truncated_wav.cpp` passes.

---

## RTM-012: FLAC Vorbis Comment vendorLen Integer Overflow

**File:** `MetadataParser.cpp:279-281`

**Attack:** Set `vendorLen` near `UINT32_MAX` so `4 + vendorLen` wraps to a small value.

**Why it's not exploitable:** `blockSize` is capped at 10 MB (line 242), so `vendorLen` values that would overflow cannot be delivered in practice. The overflow is theoretical only.

**Status:** 🟡 Monitor — blocked by 10 MB block cap, but explicit bounds check would be better defense-in-depth.

---

## RTM-013: JSON parseNumber std::stod Without try/catch

**File:** `AestraCore/include/AestraJSON.h:340-356`

**Attack:** Crafted JSON with a malformed number like `"."`, `"-."`, or `"1e"` that passes the character-level parser but throws `std::invalid_argument` from `std::stod`.

**Why it's Low severity:** The parser's structure makes it unlikely to reach `stod` with an invalid substring (the loop requires at least one digit or minus sign). But any edge case in `stod` would crash the process.

**Status:** 🟡 Open — try/catch around `std::stod` would close this for defense-in-depth.

---

## RTM-014: JSON asArray() Returns Mutable Global Static

**File:** `AestraCore/include/AestraJSON.h:41-54`

**Issue:** `asArray()` and `asObject()` return `const std::vector<JSON>&` / `const std::unordered_map<...>&` to a static empty container on type mismatch. If a caller uses `const_cast` to modify the returned reference, they mutate a global static that persists across all future calls.

**Impact:** Cross-request data contamination. Data from one JSON document could leak into another.

**Status:** 🟡 Open — return by value instead of const reference to the static.

---

## RTM-015: Silent Autosave on Recovery Fallback

**File:** `Source/App/AestraApp.cpp:532-543`

**Attack:** Local attacker creates `crash_flag` and crafts `autosave.aes` with any of the above vulnerabilities. If the RecoveryDialog is not available, the fallback path loads the autosave SILENTLY — no user confirmation, no integrity check.

**Status:** 🟡 Open — add integrity validation or require user confirmation before loading autosave.

---

## RTM-016: Headless Environment Variable Silent Failure

**File:** `Source/App/HeadlessMain.cpp:628-686`

**Attack:** Set `AESTRA_MIN_PEAK=abc` → `strtod` returns 0.0 with no error → test's pass/fail criteria silently changed. Set `AESTRA_PROJECT=/etc/passwd` → project loader runs on arbitrary file.

**Status:** 🟡 Open — add error checking to `strtod`/`strtoul` endptr, validate env var values.

---

## Running All PoCs

```bash
cd tests/redteam
for p in poc_*.py; do python3 "$p" "/tmp/$(basename "$p" .py).test"; done
```

## Remaining Work Items

| Priority | Item | Action Required |
|----------|------|----------------|
| High | RTM-004: JSON empty-project DoS | Depth limit prevents crash, but loading a 50k-depth file produces empty project. Consider rejecting deeply nested files entirely. |
| N/A | RTM-005: Plugin arbitrary code exec | **Mitigated**: first-load warning dialog + trusted path allowlist. System paths auto-load, user paths prompt once. Still documented risk. |

## Completed Fixes (Verified in Source + Proof-of-Fix Tests)

| Fix | File | Lines | Description | Proof Test |
|-----|------|-------|-------------|------------|
| RTM-001 | MiniAudioDecoder.cpp | 123 | `bitsPerSample` whitelist (16/24/32 only) | Confidence suite |
| RTM-001 | MetronomeEngine.cpp | 91-94, 99 | `bitsPerSample` whitelist (16/24 only) | Confidence suite |
| RTM-002 | MiniAudioDecoder.cpp | 127-130, 136-138 | File size consistency + 500M sample cap | Confidence suite |
| RTM-002 | MetronomeEngine.cpp | 103-106 | 10M sample cap | Confidence suite |
| RTM-003 | UnitManager.cpp | 300-305 | try/catch around `std::stoul` with default fallback | Confidence suite |
| RTM-004 | AestraJSON.h | — | `kMaxJsonDepth = 1024` depth limit | `poc_json_stack.py` |
| RTM-005 | PluginScanner.cpp, .h | — | `isTrustedPath()` + `FirstLoadWarningCallback` + seen set | `plugin_trusted_path.cpp` |
| RTM-006 | PluginScanner.cpp | 332-335, 416-430 | Cache v2: file mtime stored + verified on load | `cache_mtime_integrity.cpp` |
| RTM-007 | AestraApp.cpp | 479-505 | Crash flag + autosave mtime delta ≤ 5 min check | Source verification |
| RTM-008 | HeadlessMain.cpp | 698-706 | `AESTRA_PROJECT` env var ignored, warning logged | Source verification |
| RTM-009 | ProjectSerializer.cpp | 691-695 | try/catch around clip color `std::stoul` | `clip_color_stoul.cpp` |
| RTM-010 | PluginScanner.cpp | 361-378 | Count cap (10K) + string len cap (64KB) + good() checks | `plugin_cache_bounds.cpp` |
| RTM-011 | MetronomeEngine.cpp | 122-127, 139-143 | fread return value checked on 16-bit and 24-bit paths | `fread_truncated_wav.cpp` |
| RTM-012 | MetadataParser.cpp | 281-282 | `vendorLen > blockSize - 4` bounds check | `flac_vendorlen_bounds.cpp` |
| RTM-013 | AestraJSON.h | 391-420 | Pre-parse validation + try/catch around `std::stod` | `json_parser_hardening.cpp` |
| RTM-014 | AestraJSON.h | 40-63 | `asArray()`/`asObject()` return by value, not mutable reference | `json_parser_hardening.cpp` |
| RTM-015 | AestraApp.cpp | 518-530 | RecoveryDialog-unavailable path discards autosave instead of loading | `autosave_recovery_guard.cpp` |
| RTM-016 | HeadlessMain.cpp | 628-637, 686-694 | endptr + isfinite validation on env var strtod | `env_var_validation.cpp` |
