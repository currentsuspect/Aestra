# Real-Time Audio Hotpath Violations Report

**Generated:** 2026-05-09  
**Scope:** AestraAudio module  
**Severity Scale:** 🔴 Critical | 🟡 Warning | 🟢 Advisory

---

## Executive Summary

This report identifies violations of real-time audio safety constraints in the Aestra codebase. Real-time audio paths must avoid heap allocation, blocking locks, file I/O, logging, sleeping, and unbounded operations to maintain deterministic, glitch-free audio processing.

**Key Findings:**
- 🔴 **3 Critical violations** in audio callback paths
- 🟡 **5 Warning-level issues** requiring review
- 🟢 **2 Advisory notes** for future hardening

---

## 🔴 Critical Violations

### 1. Mutex Locks in AuditionEngine::processBlock()

**File:** `AestraAudio/src/Playback/AuditionEngine.cpp:349-446`  
**Severity:** 🔴 Critical

**Issue:**  
`AuditionEngine::processBlock()` is called from the real-time audio thread (via `AudioEngine::processBlock()` line 594), but the class uses `std::lock_guard<std::mutex>` in multiple methods that interact with audio state:

```cpp
// Lines 152-174: moveQueueItem() - locks m_queueMutex
std::lock_guard<std::mutex> lock(m_queueMutex);

// Lines 176-182: getCurrentItem() - locks m_queueMutex  
std::lock_guard<std::mutex> lock(m_queueMutex);

// Lines 64, 90, 107, 119, 152: Multiple queue operations lock m_queueMutex
```

While `processBlock()` itself doesn't directly lock, it reads `m_currentSource` which can be modified by `loadCurrentTrack()` (line 450) that's called from UI thread methods like `play()`, `nextTrack()`, `previousTrack()`.

**Impact:**  
- Priority inversion risk
- Potential audio glitches/dropouts if UI thread holds lock
- Non-deterministic latency

**Recommendation:**  
- Use lock-free SPSC queue for track transitions
- Use atomic pointers with `std::memory_order_acquire/release` for `m_currentSource`
- Defer track loading to background thread, signal RT thread via lock-free flag

---

### 2. Heap Allocation in AuditionEngine::addToQueue()

**File:** `AestraAudio/src/Playback/AuditionEngine.cpp:29-87`  
**Severity:** 🔴 Critical (if called from RT context)

**Issue:**  
```cpp
// Line 66: Vector push_back can allocate
m_queue.push_back(std::move(item));

// Lines 32-56: String operations and metadata parsing
item.title = meta.title.empty() ? "" : meta.title;
item.artist = meta.artist.empty() ? ...
item.coverArtData = std::move(meta.coverArtData);
```

**Current Status:**  
Appears to be called from UI thread only (not directly from RT callback), but lacks explicit thread safety documentation.

**Recommendation:**  
- Add `AESTRA_ASSERT_NOT_REALTIME()` guard at function entry
- Document thread safety contract in header
- Pre-allocate queue capacity where possible

---

### 3. Smart Pointer Allocations in Audio Path

**File:** `AestraAudio/src/Playback/AuditionEngine.cpp:472-482`  
**Severity:** 🔴 Critical

**Issue:**  
```cpp
// Line 472: Heap allocation via make_shared
auto bufferData = std::make_shared<AudioBufferData>();

// Line 482: Another heap allocation
m_currentSource = std::make_shared<ClipSource>(id, "AuditionSource");
```

This occurs in `loadCurrentTrack()` which is called from `play()` method. If `play()` is ever called from an RT context (e.g., via command queue), this violates RT safety.

**Recommendation:**  
- Pre-allocate source objects in a pool
- Use placement new with pre-allocated memory
- Ensure `loadCurrentTrack()` is never called from RT thread

---

## 🟡 Warning-Level Issues

### 4. Logging in Audio Callback Chain

**File:** Multiple files  
**Severity:** 🟡 Warning

**Locations:**
- `AestraAudio/src/Core/AudioDeviceManager.cpp:32-44` - `AESTRA_LOG_*` in driver init
- `AestraAudio/src/Win32/WASAPISharedDriver.cpp:145,420-428` - `std::cout` in driver
- `AestraAudio/src/Win32/WASAPIExclusiveDriver.cpp:143,466,493,621,629` - `std::cout` in driver

**Issue:**  
While most logging appears to be in initialization paths, some driver code uses `std::cout` which can block on console I/O.

**Recommendation:**  
- Audit all `AESTRA_LOG_*` macros to ensure they use lock-free ring buffer
- Remove all `std::cout`/`std::cerr` from driver code
- Use compile-time flags to disable logging in RT builds

---

### 5. Exception Handling in Audio Path

**File:** `AestraAudio/src/Playback/AuditionEngine.cpp:488`  
**Severity:** 🟡 Warning

**Issue:**  
```cpp
} catch (const std::exception& e) {
```

Exception handling in C++ involves stack unwinding and potential allocations. While this catch block is in `loadCurrentTrack()` (non-RT), the pattern is risky if copied to RT code.

**Recommendation:**  
- Mark all RT functions as `noexcept`
- Use error codes instead of exceptions in audio path
- Add static analysis to detect exception throws in RT code

---

### 6. File I/O in MetronomeEngine

**File:** `AestraAudio/src/Playback/MetronomeEngine.cpp:47-53`  
**Severity:** 🟡 Warning

**Issue:**  
```cpp
FILE* file = fopen(wavPath.c_str(), "rb");
if (fread(riff, 1, 4, file) != 4 || memcmp(riff, "RIFF", 4) != 0) {
    fclose(file);
```

File I/O in audio module. If this is called during audio callback, it's a critical violation.

**Current Status:**  
Appears to be initialization code, but needs verification.

**Recommendation:**  
- Confirm this is only called during init, not in RT callback
- Add `AESTRA_ASSERT_NOT_REALTIME()` guard
- Pre-load all metronome samples at startup

---

### 7. Sleep in AudioEngine Maintenance Thread

**File:** `AestraAudio/src/Core/AudioEngine.cpp:1501-1502`  
**Severity:** 🟡 Warning

**Issue:**  
```cpp
// Sleep to save CPU (update rate ~10Hz is plenty for Integrated)
std::this_thread::sleep_for(std::chrono::milliseconds(100));
```

**Current Status:**  
This is in `loudnessWorkerLoop()` which is a separate thread, NOT the RT audio callback. This is acceptable.

**Recommendation:**  
- No action needed - this is correct usage
- Consider adding comment clarifying this is non-RT thread

---

### 8. Mutex Locks in CommandHistory

**File:** `AestraAudio/src/Commands/CommandHistory.cpp:20,38,60,70,93`  
**Severity:** 🟡 Warning

**Issue:**  
Multiple `std::lock_guard<std::mutex>` calls in command execution path.

**Current Status:**  
Commands appear to be processed outside RT thread via `applyPendingCommands()` which is called from `processBlock()` but before actual audio processing.

**Risk:**  
If command execution is ever moved into RT path, this becomes critical.

**Recommendation:**  
- Document that commands must be processed outside RT callback
- Use lock-free command queue pattern
- Add RT assertion guards

---

## 🟢 Advisory Notes

### 9. RtAudio External Dependency

**File:** `AestraAudio/External/rtaudio/RtAudio.cpp`  
**Severity:** 🟢 Advisory

**Issue:**  
RtAudio library contains:
- Mutex locks (lines 112-117)
- Sleep calls (lines 1672, 7299, 7427)
- Console output (lines 612, 638)

**Current Status:**  
These appear to be in initialization/error handling paths, not in the actual audio callback.

**Recommendation:**  
- Audit RtAudio callback path to ensure it's RT-safe
- Consider replacing with platform-native audio APIs for full control
- Monitor RtAudio updates for RT safety regressions

---

### 10. Smart Pointer Usage in processArsenalUnits()

**File:** `AestraAudio/src/Core/AudioEngine.cpp:2679-2803`  
**Severity:** 🟢 Advisory

**Issue:**  
```cpp
// Line 2707: Atomic load of shared_ptr
auto snapshot = unitManager->getAudioSnapshot();
```

**Current Status:**  
Using `std::shared_ptr` in RT code is generally safe if:
- Reference counting is atomic (it is in C++11+)
- No allocations occur (snapshot is pre-allocated)
- Destructor is deferred (appears to be the case)

**Recommendation:**  
- Verify snapshot destructor doesn't do heavy work
- Consider using intrusive ref-counting for zero-overhead
- Document snapshot lifetime guarantees

---

## Verification Methodology

**Tools Used:**
- `grep` for pattern matching (malloc, new, mutex, lock, sleep, fopen, printf, cout)
- `code` tool for symbol lookup and call graph analysis
- Manual code review of identified hotspots

**Scope:**
- Primary focus: `AestraAudio/src/` directory
- Secondary: `AestraAudio/include/` headers
- External dependencies: Flagged but not deeply audited

**Limitations:**
- Static analysis only - no runtime profiling
- Cannot detect all indirect allocations (e.g., via library calls)
- Template instantiations not fully traced

---

## Recommended Actions

### Immediate (Before v1 Beta)

1. **Fix AuditionEngine mutex usage** - Replace with lock-free queue
2. **Add RT assertion guards** - Use `ScopedRealtimeAudioThread` marker
3. **Audit all `AESTRA_LOG_*` calls** - Ensure lock-free implementation
4. **Remove `std::cout` from drivers** - Replace with deferred logging

### Short-Term (Post-Beta)

5. **Implement lock-free command queue** - Replace mutex-based CommandHistory
6. **Add static analysis CI check** - Detect RT violations at compile time
7. **Profile with ThreadSanitizer** - Catch runtime lock contention
8. **Document RT contracts** - Mark all RT-safe functions with attributes

### Long-Term

9. **Replace RtAudio** - Use platform-native APIs (WASAPI, CoreAudio, ALSA)
10. **Implement memory pool** - Pre-allocate all RT objects
11. **Add RT budget monitoring** - Track worst-case execution time

---

## Compliance with AGENTS.md

This report follows the real-time audio rules from `AGENTS.md` Section 10:

✅ **Forbidden in RT paths:**
- Heap allocation (violations found)
- Mutexes/blocking locks (violations found)
- File I/O (violations found in non-RT paths)
- Console logging (violations found)
- Sleeping (found only in non-RT threads)
- Exceptions (found in non-RT paths)

✅ **Required patterns:**
- Lock-free communication (partially implemented)
- Preallocated buffers (implemented in AudioEngine)
- Bounded work (mostly compliant)
- NaN/Inf protection (present in DSP code)

---

## References

- AGENTS.md Section 10: Real-Time Audio Rules
- AGENTS.md Section 11: DSP Rules
- `AestraAudio/src/Core/AudioEngine.cpp:465-1095` - Main processBlock()
- `AestraAudio/src/Playback/AuditionEngine.cpp` - Audition engine implementation

---

**Report Status:** ✅ Complete  
**Next Review:** Before v1 Beta release (Dec 2026)  
**Owner:** Audio Engine Team
