# Aestra — Optimization Master Spec

Every optimizable surface. Clear stop conditions. Agent-ready.

| Document | Version | Status | Target |
|----------|---------|--------|--------|
| OPTIMIZE.md / Spec | 1.1 | ACTIVE | December 2026 Beta |

## Philosophy

Aestra is not minimalist for aesthetic reasons. It is minimalist because its target user has a 4 GB RAM laptop, a weak CPU, and no tolerance for a DAW that stutters. Every byte and every cycle is a resource borrowed from their session.

The optimization goal is not a number. It is a guarantee: a producer on minimum-spec hardware can open Aestra, load Arsenal, write a pattern, arrange, run a native plugin chain, and bounce — without audio dropouts.

### The Density Principle

Pack tight. Compress hard. Ship small. Run lean.

A feature that costs more than it earns in the context of 4 GB RAM and a dual-core CPU is a defect.

Optimization is not a phase. It is a constraint that applies from the first line of code.

---

## Global Stop Conditions

Optimization work stops when ALL of the following are true simultaneously. These are not aspirational targets — they are hard gates.

### CPU Budget

| Scenario | Target CPU | Measurement | Stop When |
|----------|-----------|-------------|-----------|
| Idle (no playback) | < 1% | htop, 30s average | ≤ 0.8% |
| Playback, empty session | < 3% | htop during playback | ≤ 2.5% |
| 10 tracks + native plugins | < 25% | htop peak, 512-sample buffer | ≤ 22% |
| 50 tracks + native plugins | < 60% | htop peak, 512-sample buffer | ≤ 55% |
| Full session (test bed) | < 80% | No dropouts at 256-sample | 0 xruns logged |

### Memory Budget

| Scenario | Target RAM | Stop When |
|----------|-----------|-----------|
| Aestra launch, no session | < 80 MB | ≤ 72 MB RSS |
| Empty session open | < 100 MB | ≤ 90 MB RSS |
| 10 tracks, loaded plugins | < 180 MB | ≤ 160 MB RSS |
| Full production session | < 350 MB | ≤ 320 MB RSS |
| Peak during bounce | < 500 MB | ≤ 450 MB |

### Binary & Distribution Size

| Artifact | Target | Stop When |
|----------|--------|-----------|
| Aestra executable (stripped) | < 8 MB | ≤ 7 MB |
| Full install (exe + assets) | < 25 MB | ≤ 22 MB |
| Installer package (compressed) | < 15 MB | ≤ 13 MB |
| Plugin .so files (each) | < 1 MB | ≤ 900 KB |
| Total plugin suite | < 8 MB | ≤ 7 MB |

### Latency & Responsiveness

| Metric | Target | Stop When |
|--------|--------|-----------|
| UI thread frame time | < 16 ms | ≤ 14 ms at 60 fps |
| Plugin load time (each) | < 200 ms | ≤ 150 ms |
| Session open (10 tracks) | < 2 s | ≤ 1.5 s |
| Undo/redo response | < 50 ms | ≤ 30 ms |
| Audio engine start | < 500 ms | ≤ 400 ms |

---

## Optimization Stack

Organized by layer, from lowest-level (irreducible hardware cost) to highest-level (distribution). Work bottom-up. A higher layer cannot fix a lower-layer problem.

### Layer 0 — Audio Engine (DSP Core)

The hot path. Every optimization here multiplies across every plugin instance, every sample, every second of playback. Highest ROI.

#### 0.1 — Transcendental Function Cost

| Location | Function | Cycles | Status | Action |
|----------|----------|--------|--------|--------|
| AestraLimit | `std::log10`, `std::exp` | ~20 each | ACCEPTED — irreducible | Document; do not replace |
| AestraComp (gain) | `std::log10`, `std::pow` | ~20 each | AUDIT NEEDED | Profile; check if LUT viable for comp knee curve only |
| AestraEQ (bilinear) | `std::tan`, `std::cos` | ~15 each | AUDIT NEEDED | Check recalculation frequency — cache if params stable |
| AestraVerb (FDN) | `std::exp` (decay) | ~20 | PARTIAL | Precompute decay coefficients at init; only recalc on size change |

#### 0.2 — SIMD Vectorization

| Subsystem | Opportunity | Priority | Status |
|-----------|------------|----------|--------|
| FDN delay line mixing | 8-line Householder matrix — pure float multiply-accumulate | HIGH | IMPLEMENTED |
| EQ biquad cascade | Multiple biquads per channel — vectorize across bands | HIGH | UNOPTIMIZED |
| Limiter lookahead scan | Peak detection loop over 400 samples — auto-vectorizable | MED | UNOPTIMIZED |
| Comp envelope follower | RMS window computation | MED | UNOPTIMIZED |
| Sample rate conversion | Polyphase filter bank | MED | IMPLEMENTED |

**SIMD Implementation Rule:**

- Use compiler intrinsics (`__builtin_ia32_*`) or SSE2/AVX2 via `<immintrin.h>` — never platform-specific vendor libs.
- Always provide scalar fallback path gated on CPUID check at runtime.
- Benchmark before and after with the same sample buffer. Ship only if speedup ≥ 20%.
- ARM (Raspberry Pi / low-end ARM SBCs): use NEON equivalents. Detect at compile time via `__ARM_NEON`.

#### 0.3 — Denormal Protection

Denormal floats (values near zero) cause CPU to fall into microcode and spike processing time 10–100x. Critical on ARM64 and older x86.

| Platform | Method | Status |
|----------|--------|--------|
| x86/x64 | Set FTZ + DAZ bits in MXCSR at engine start | IMPLEMENTED |
| ARM64 | Set FZ bit in FPCR | IMPLEMENTED |
| Per-plugin reset | Zero denormals at process() entry on silence | IMPLEMENTED |

**Verification:** Test with sustained silent input — monitor CPU. [TEST]

#### 0.4 — Process Block Size Optimization

| Buffer Size | Latency | CPU Overhead | Target Use |
|-------------|---------|--------------|------------|
| 64 samples | 1.3 ms @ 48k | HIGH (loop overhead) | Live monitoring only |
| 128 samples | 2.7 ms @ 48k | MODERATE | Live performance |
| 256 samples | 5.3 ms @ 48k | LOW | Default production |
| 512 samples | 10.7 ms @ 48k | VERY LOW | Bounce / weak hardware |
| 1024 samples | 21.3 ms @ 48k | MINIMAL | Export only |

Aestra must allow buffer size selection. Default to 512 for sessions on < 6 GB RAM systems. Auto-detect and warn.

---

### Layer 1 — Memory Layout

How data is arranged in memory determines cache behavior. Cache misses are invisible in profiling but show up as unexplained CPU spikes.

#### 1.1 — Plugin Instance Layout

| Plugin | Hot Data | Cold Data | Action |
|--------|----------|-----------|--------|
| AestraLimit | Lookahead bufs, writeCursor, gain | PluginInfo, presets | HOT PATH CLEAN — no action |
| AestraComp | Envelope state, gain smoothing, RMS buf | Mode enum, UI params | Audit struct layout — hot fields first |
| AestraVerb | 8 delay lines, feedback matrix, predelay | Mode, preset, UI state | Audit — delay lines should be contiguous |
| AestraEQ | Biquad coefficients, states per band | Band count, UI state | Group biquad states; avoid AoS for bands |
| AestraDelay | Delay buffer, tap positions, feedback | Sync mode, UI state | Audit layout |

Rule: within any plugin struct, sort fields by access frequency in process(). Hot fields first, cold fields last. Use `alignas(64)` on the struct to ensure cache-line alignment of the hot prefix.

#### 1.2 — Audio Buffer Layout

**Architecture decision (verified 2026-06-22):** The engine uses interleaved stereo (LRLRLR) as its native audio buffer format. This is correct for this architecture.

| Layout | Description | Aestra Target |
|--------|-------------|---------------|
| Interleaved (LRLRLR) | L and R samples alternate | **YES — native format throughout engine** |
| Planar (LLLL...RRRR...) | Separate L and R arrays | Per-track plugin window only |
| Block-planar | Planar within fixed blocks | Not used |

**Data path (verified):**

```
OS/Driver callback
  └─ audioCallback() receives interleaved stereo float
       └─ processBlock(outputBuffer, inputBuffer, numFrames)
            └─ renderGraph() — per-track processing:
                 ├─ Deinterleave double interleaved → float planar (m_scratchL/R)
                 │    AudioEngine.cpp:2494-2508
                 ├─ EffectChain::process(channels[2], numFrames)
                 │    All plugins receive float** {L, R} — planar
                 └─ Reinterleave float planar → double interleaved
                      AudioEngine.cpp:2516-2520
            └─ Final output: double interleaved → float interleaved → callback buffer
                 AudioEngine.cpp:988-1078
```

**Plugin boundary conversion:** One deinterleave/reinterleave pair per track, not per plugin. The EffectChain runs all plugins on planar buffers in sequence. Sites:

- `AudioEngine.cpp:2494-2520` — renderGraph() per-track path
- `MixerChannel.cpp:127-138` — standalone mixer path

**Why interleaved is correct here:**

- The OS/driver hands interleaved buffers (ASIO is the exception — it converts at the driver boundary)
- Aestra is a 2-channel DAW. Interleaved is cache-friendly for stereo (L/R pairs are adjacent)
- The sinc resampler SIMD deinterleaves inside the register via shuffle+permute — this is deliberate, not a workaround
- The reverb SIMD vectors across FDN delay lines (8-wide), not audio frames — buffer layout is irrelevant to it

#### 1.3 — Allocation Policy

- Zero heap allocation in process() — all plugin memory pre-allocated at load
- Use a pool allocator for plugin instances — prevents heap fragmentation over long sessions
- Delay line buffers: allocate with capacity for max supported sample rate (192 kHz) at init — no realloc on SR change
- String data (preset names, labels): interned at load, pointer-stable
- **NEVER call new/delete/malloc/free on the audio thread**

---

### Layer 2 — Plugin Architecture

#### 2.1 — Plugin Sandboxing vs Audio Graph IR

Audio Graph IR chosen over Plugin Sandboxing. This is the correct choice for the target hardware.

Plugin Sandboxing adds IPC overhead (pipe/socket round-trip per process block) which is unacceptable at 256-sample buffers.

Audio Graph IR allows the engine to see the full plugin graph, enabling global optimizations:

- Dead plugin pruning (muted/bypassed tracks skipped entirely)
- Reorder processing to maximize cache locality across instances
- Vectorize across multiple instances of the same plugin type

#### 2.2 — Process() Call Overhead

| Source of Overhead | Cost | Mitigation |
|-------------------|------|------------|
| Virtual dispatch (process()) | ~2-5 ns indirect call | Devirtualize hot path — use static plugin type dispatch at graph compile time |
| Parameter sync (UI → DSP) | Depends on impl | Lock-free ring buffer — one direction only, no mutex on audio thread |
| Bypass check | Branch per block | Move bypass to graph level — don't call process() at all for bypassed nodes |
| Preset load mid-session | Heap alloc risk | Double-buffer params — prepare next state off-thread, atomic swap |

---

### Layer 3 — Rendering & UI

UI runs on a separate thread. The risk is not UI slowness — it is UI work contaminating the audio thread or causing lock contention.

#### 3.1 — OpenGL Render Budget

| Component | Frame Budget | Current Status | Action |
|-----------|-------------|----------------|--------|
| Plugin UI repaint (full) | < 4 ms | UNKNOWN | Profile with glFinish() timing |
| Waveform display update | < 3 ms | UNKNOWN | Use GPU-side texture for waveform — upload once, draw many |
| Spectrum analyzer (comp/eq) | < 2 ms | IN PROGRESS | FFT on background thread, upload result to GPU as 1D texture |
| VU meters / gain reduction | < 1 ms per plugin UI | UNKNOWN | Batch draw calls — single VAO for all meter geometry |
| Knob/slider repaint | < 0.5 ms | UNKNOWN | Dirty-flag driven — repaint only on value change |

#### 3.2 — UI ↔ Audio Thread Safety

- Parameter changes: UI writes to a lock-free SPSC queue; audio thread polls once per block
- Metering data: audio thread writes peak/RMS to atomic float; UI reads and resets
- No mutexes on the audio thread — ever
- No OpenGL calls from the audio thread
- Session state changes (add/remove track): pause graph, apply, resume — never mid-block

---

### Layer 4 — Build & Binary Optimization

The compiler does a lot automatically — but only if you configure it correctly and help it with layout hints.

#### 4.1 — Compiler Flags (Release Build)

| Flag | Effect | Apply To |
|------|--------|----------|
| `-O3` | Aggressive optimization including vectorization | All DSP code |
| `-march=native` (CI: `x86-64-v2`) | Target actual CPU features; enable SSE4.2, AVX if available | DSP; careful on CI — use baseline |
| `-ffast-math` | Allows reassociation, no NaN/Inf checking | DSP only — NOT UI or file I/O code |
| `-flto` | Link-time optimization — cross-TU inlining | Full build |
| `-fno-exceptions` | Remove exception handling overhead | Audio thread code |
| `-fno-rtti` | Remove RTTI overhead | Plugin core; keep where needed for reflection |
| `-ffunction-sections -fdata-sections` + `--gc-sections` | Dead code elimination at link time | Full build |
| `-DNDEBUG` | Remove assert() and debug paths | Release only |

**Status: NOT IMPLEMENTED in project CMake.** Currently only present in external dependencies (VST3 SDK, ThorVG). Project CMakeLists.txt sets only warning flags. These flags need to be added to the project's own Release build configuration.

#### 4.2 — Profile-Guided Optimization (PGO)

1. Instrument build: `-fprofile-generate`
2. Run representative workload: 10-track session, 2 minutes playback, plugin chain active
3. Collect profile data: `-fprofile-use`
4. Expected gain: 5–15% on CPU-heavy paths from better branch prediction and inlining decisions

Do PGO last — after all DSP optimizations are complete.

**Status: NOT IMPLEMENTED.**

#### 4.3 — Binary Stripping

| Step | Tool | Expected Size Reduction |
|------|------|------------------------|
| Strip debug symbols | `strip --strip-debug aestra` | 40–60% size reduction |
| Strip all symbols | `strip --strip-all aestra` | Additional 5–10% |
| Separate debug info | `objcopy --only-keep-debug` | Keep .debug file for crash reporting |
| Plugin .so stripping | `strip --strip-debug each .so` | Similar reduction per plugin |

**Status: NOT IMPLEMENTED.**

---

### Layer 5 — Distribution & Packaging

Final layer. The installer and package are what users download. Size here is the user's first impression of Aestra's quality and intentionality.

#### 5.1 — Asset Optimization

| Asset Type | Current | Target | Method |
|------------|---------|--------|--------|
| UI textures / icons | Unknown | < 500 KB total | WebP or PNG with oxipng — lossless compression |
| Font files | Unknown | < 200 KB total | Subset to used glyphs only (pyftsubset) |
| Preset data | Unknown | < 50 KB total | Binary format (not JSON/XML) — custom compact encoding |
| Shader source | Embedded as strings | < 20 KB | Strip whitespace/comments from GLSL at build time |
| Sample waveform previews | N/A if none | Avoid | Generate from audio at load — no preview assets |

**Status: NOT IMPLEMENTED.**

#### 5.2 — Compression Stack

The 'repacker' layer. Applied after binary stripping and asset optimization.

| Stage | Tool | Compression | Use For |
|-------|------|-------------|---------|
| Binary compression | UPX --best --lzma | ~50% additional reduction | Aestra executable — test startup time after |
| Asset archive | zstd -19 or xz -9 | Best ratio at reasonable decompress speed | Asset bundle — decompress at install, not runtime |
| Installer | NSIS or AppImage + squashfs | Built-in compression | Linux target |
| Debug symbols archive | xz -9 | Maximum | Crash reporting — not distributed to users |

**UPX Warning:** UPX works well on binaries with no self-modifying code or JIT. Aestra is eligible — C++, no JIT, no self-modification. Test: unpack and run after UPX compression. Verify startup time is acceptable (< 500 ms additional). Do NOT use UPX on plugin .so files — dynamic linker may have issues with compressed shared objects on some distros.

**Status: NOT IMPLEMENTED.**

#### 5.3 — Install Footprint Target

| Component | Target Size | Compressed (installer) |
|-----------|------------|----------------------|
| Aestra executable | < 7 MB | < 3.5 MB |
| Plugin suite (all .so) | < 7 MB | < 3.5 MB |
| Assets (fonts, icons, shaders) | < 1 MB | < 500 KB |
| Preset library | < 500 KB | < 200 KB |
| **Total install** | **< 22 MB** | **< 13 MB (installer)** |

---

## Subsystem Audit Status

Living table. Updated as audits complete. Green = signed off at this layer. Amber = work in progress. Red = not started. Blue = correct — spec was wrong (code was right all along).

| Subsystem | CPU | Memory | Cache | SIMD | Binary | Notes |
|-----------|-----|--------|-------|------|--------|-------|
| AestraLimit | ✓ DONE | ✓ DONE | ✓ DONE | Pending | Pending | Transcendental floor accepted |
| AestraComp | Partial | Pending | Pending | Pending | Pending | 3-mode DSP needs full audit |
| AestraVerb | Pending | Pending | Pending | Pending | Pending | FDN matrix — SIMD candidate |
| AestraEQ | Pending | Pending | Pending | Pending | Pending | Biquad cascade — SIMD candidate |
| AestraDelay | Pending | Pending | Pending | Pending | Pending | On feature branch |
| AestraDrift | Pending | Pending | Pending | Pending | Pending | — |
| AestraLFO | Pending | Pending | Pending | Pending | Pending | — |
| AestraFilter | Pending | Pending | Pending | Pending | Pending | — |
| AestraOne | Pending | Pending | Pending | Pending | Pending | — |
| Arsenal (sampler) | Pending | Pending | Pending | Pending | Pending | Highest priority after limit |
| Audio engine core | Pending | Pending | Pending | Pending | Pending | Graph IR, buffer mgmt |
| OpenGL UI layer | Pending | Pending | Pending | N/A | Pending | Frame timing audit needed |
| Muse CLI | Pending | Pending | Pending | N/A | Pending | Lower priority |
| **Buffer layout** | **BLUE** | **BLUE** | **BLUE** | **BLUE** | N/A | **Spec was wrong — interleaved is correct** |

---

## SIMD Routine Inventory

All 15 active SIMD routines in the codebase. Verified 2026-06-22.

| # | File | Function | ISA | Purpose | Buffer Layout | Correct? |
|---|------|----------|-----|---------|---------------|----------|
| 1 | `ReverbSIMD.h:100-182` | `processFDNSampleAVX2` | AVX2+FMA | 8-wide FDN matrix (Householder, damping, feedback) | Vectors across 8 FDN delay lines | ✓ Correct |
| 2 | `ReverbSIMD.h:190-217` | `updateLFOsAVX2` | AVX2+FMA | 8-wide LFO quadrature oscillator | Vectors across 8 FDN lines | ✓ Correct |
| 3 | `ReverbSIMD.h:224-241` | `normalizeLFOsAVX2` | AVX2+FMA | 8-wide LFO renormalization | Vectors across 8 FDN lines | ✓ Correct |
| 4 | `ReverbSIMD.h:250-337` | `processFDNSampleSSE` | SSE2 | 4-wide FDN matrix (two passes for 8 lines) | Vectors across 4 FDN lines | ✓ Correct |
| 5 | `ReverbSIMD.h:326-336` | `updateLFOsSSE` | SSE2 | 4-wide LFO update | Vectors across 4 FDN lines | ✓ Correct |
| 6 | `ReverbSIMD.h:345-414` | `processFDNSampleNEON` | ARM NEON | 4-wide FDN matrix (ARM) | Vectors across 4 FDN lines | ✓ Correct |
| 7 | `ReverbSIMD.h:425-461` | `processDiffusersSSE` | SSE2 | Stereo diffuser allpass stages | Interleaved stereo | ✓ Correct |
| 8 | `SincAVX2.h:37-75` | `sincDotProductAVX2` | AVX2+FMA | 64-tap sinc resampler (forward) | Interleaved stereo — deinterleaves via shuffle+permute | ✓ Correct |
| 9 | `SincAVX2.h:82-127` | `sincDotProductAVX2_Reversed` | AVX2+FMA | 64-tap sinc resampler (reversed) | Interleaved stereo — deinterleaves via shuffle+permute | ✓ Correct |
| 10 | `SincAVX512.cpp:23-58` | `sincDotProductAVX512` | AVX-512F | 64-tap sinc resampler (forward) | Interleaved stereo — vpermt2var_ps gathers even/odd | ✓ Correct |
| 11 | `SincAVX512.cpp:65-104` | `sincDotProductAVX512_Reversed` | AVX-512F | 64-tap sinc resampler (reversed) | Interleaved stereo — vpermt2var_ps gathers even/odd | ✓ Correct |
| 12 | `PreviewEngine.cpp:302-461` | Preview SIMD loop | SSE | 4-wide cubic Hermite resampler | Interleaved stereo — unpacklo/hi for output | ✓ Correct |
| 13 | `SampleRateConverter.cpp:80-104` | `dotProductSSE` | SSE2 | Generic dot product for polyphase filter | 1D scalar arrays | ✓ Correct |
| 14 | `SampleRateConverter.cpp:109-135` | `dotProductAVX` | AVX2 | Generic dot product for polyphase filter | 1D scalar arrays | ✓ Correct |
| 15 | `AudioRT.h:33-35` | `ScopedDenormals` | SSE (MXCSR) | Flush-to-zero denormal control | N/A — control register | ✓ Correct |

**Summary:** 5 AVX2+FMA, 2 AVX-512F, 5 SSE2, 1 SSE, 1 ARM NEON, 1 MXCSR control. All active. Zero dead/commented-out SIMD code.

**Key finding:** The sinc resampler SIMD (routines 8-11) deinterleaves inside the SIMD register using shuffle+permute. This is a deliberate, correct design for interleaved stereo data — not a workaround. The reverb SIMD (routines 1-6) vectors across FDN delay lines, not audio frames — buffer layout is irrelevant to it.

---

## Recommended Optimization Order

Not every area has equal ROI. Work this sequence.

| Priority | Target | Rationale |
|----------|--------|-----------|
| P0 | Denormal protection (all plugins) | One-time setup, eliminates catastrophic CPU spikes, 30 minutes of work |
| P0 | Allocation policy enforcement (no alloc in process()) | Prevents non-deterministic latency spikes on weak hardware |
| P1 | AestraComp CPU audit + transcendental review | Next plugin in the audit stack, 3-mode DSP adds complexity |
| P1 | Arsenal sampler — full audit | Arsenal is the most CPU/memory-heavy component in a typical session |
| P1 | Audio buffer layout (planar enforcement) | ~~Foundation for SIMD — must be planar before vectorization~~ **CORRECTED: Interleaved is the correct native layout. Per-track planar window exists and works. No change needed.** |
| P2 | AestraVerb FDN SIMD | 8×8 Householder — embarrassingly parallel, high gain |
| P2 | AestraEQ biquad SIMD | Multi-band cascade — high gain with SSE4.1 |
| P2 | Compiler flags + LTO | Global speedup with minimal code change |
| P3 | PGO | Do after all DSP work — profiles real workload |
| P3 | Binary strip + UPX | Distribution size — do last, after all code is stable |
| P3 | Asset optimization | Preset data, fonts, shaders — small but visible to users |

---

## Agent Instructions

This section governs how agents operate within the optimization workflow. Follows the same authority as AGENTS.md.

### The Trident Loop — Optimization Edition

1. Dylan describes what he thinks the bottleneck is.
2. Agent instruments and measures — no reasoning without evidence. Use perf, valgrind, vtune, or custom timing. Report raw numbers.
3. Dylan + Claude analyze measurements, identify root cause, agree on fix with tradeoffs.
4. Claude writes a surgical optimization spec — exact flags, exact values, exact struct layouts.
5. Agent implements spec exactly. No creative interpretation.

### What Agents MUST Do

- Measure before and after every optimization. No unmeasured changes to hot paths.
- Report the metric that matters: CPU % at fixed buffer size, not wall clock time.
- Profile on a constrained environment when possible: 4 GB RAM, single-core throttled.
- Run ASan/UBSan after every DSP change — optimizations frequently introduce UB.
- Document the accepted floor for each subsystem in a comment block in the source.

### What Has Been Verified (2026-06-22)

- **Buffer layout:** Verified by inspection of `AudioEngine.cpp`, `ASIODriver.cpp`, `EffectChain.cpp`, `MixerChannel.cpp`. Interleaved is native. Planar window exists per-track at plugin boundary.
- **SIMD correctness:** Verified by reading all 15 routines. All are correct for their actual buffer layouts.
- **OPTFLOOR values:** NOT YET MEASURED — only AestraLimit has an OPTFLOOR block. All other plugins need profiling before audit blocks can be written. See future sprint.

### What Agents MUST NOT Do

- Replace accuracy-critical functions (log, exp, tan) with approximations without explicit approval.
- Add compiler flags to files that are not DSP code — `-ffast-math` on UI code is a bug.
- Optimize without first reading the existing audit notes for that subsystem.
- Run UPX on .so plugin files.
- Allocate memory in process() under any circumstances.

### Optimization Accepted Floor Comment Format

```cpp
// OPTFLOOR: [subsystem] [date]
// CPU: ~N cycles/sample @ 48kHz, 512-sample buffer
// Memory: N KB hot working set per instance
// Cache: fits in LN at M instances
// Irreducible cost: [reason]
// Next review: [condition or date]
```

---

## Changelog

- **v1.0** (2026-04-12): Initial spec.
- **v1.1** (2026-06-22): Corrected Layer 1.2 buffer layout — interleaved stereo is the native format, not planar. Added SIMD routine inventory with layout verification. Added "Correct — spec was wrong" audit status. Corrected optimization order P1 row. Verified all 15 SIMD routines are correct for their actual buffer layouts.
