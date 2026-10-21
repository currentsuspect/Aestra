# Optimization Runs — 2026-10-21

Founder-directed performance session. Target: the 4 GB / AVX1-only Folio 9470m
(Intel i5-3337U, 2C/4T). This box now has 16 GB, so build parallelism is `-j4`;
the *optimization target* is unchanged.

## Baseline source

- Aestra `develop` @ `def874133` (branch `perf/optimization-runs`)
- Build: `build-linux`, Release, UI ON, `-march=x86-64` (SSE2 baseline, no AVX)

## Fixture (revised — the frozen one was stale)

`labs/perf/fixtures/folio-baseline-v1` **does not load its effect chain on
develop**: the fixture was written with keyed-v1 plugin blobs, and develop's
`InternalPluginBase::loadState` accepts v2 only — so the Chord lane's filter+sat
were rejected and the slots rendered EMPTY. Every baseline measurement of
develop was therefore taken on a graph with no effect chain at all.

Rebuilt fixture: `/tmp/opencode/fixture-devrev` (same generator, develop's
format v4; `CAPABILITIES.json` requiredVersion 3 -> 4). Validator 80/80 PASS;
probe `effect_enabled_rms=0.340` vs `effect_disabled_rms=0.071` — the chain is
genuinely in the render.

## Instruments

| Purpose | Tool |
|---|---|
| Audio instruction cost | `valgrind --tool=callgrind` on `FolioCollect --mode offline` |
| Audio wall clock + RSS | `FolioCollect` (protocol collector) -> `labs/perf/bench-audio.py` |
| Live callback timing | `FolioCollect --mode playback` (real device) |
| Frame attribution | `AESTRA_FRAME_STATS=2` -> `labs/perf/bench-frames.py` |
| Drive the app | `ydotool` (uinput) + Muse socket (`AESTRA_MUSE_PORT`) |

No `perf` on this box.

---

# RUN 1 — Audio: the 4x oversampler (AestraSat / AestraComp / AestraLimit)

## What was wrong

`DSP::Oversampler` is a 47-tap halfband at 2fs + a 29-tap halfband at 4fs. The
scalar implementation spent ~840 **cycles** per input sample per channel on one
plugin, and none of that was the multiply count:

1. The FIR accumulator was a serial dependency chain — 47 taps x 4 cycles of
   FP-add latency, not 47 slots of throughput.
2. The decimator's inner loop advanced its ring index with `% kMaxTaps`: a
   runtime modulo in the innermost loop.
3. The interpolator's history was shifted one element at a time per sample.

## What changed (no quality change)

- The FIR now sums with **SSE2** — four independent accumulators, so the partial
  sums do not wait on each other. SSE2 is part of the x86-64 baseline ABI: no
  compiler flag, no runtime dispatch, no per-translation-unit targeting, and it
  is available on the 4 GB Folio target as-is. A scalar fallback covers every
  other architecture.
- The ring is **power-of-two and stored twice**, so the FIR window is one
  contiguous run: no modulo, no split loop, no history shift.
- The dot product is **templated on the tap count** and unrolled (the loop
  control had cost as much as the arithmetic: nine dot products per sample).
- The interpolator phases **skip the exactly-zero halfband taps**. One phase of
  a halfband is a single tap (the centre); the other carries all the work.

The kernel, the tap set, the tap order and the arithmetic are unchanged. Only
the order of the sums differs.

## Measured

### Instruction cost (offline fixture render, 5 s, callgrind)

| | baseline | after | |
|---|---:|---:|---|
| total Ir | 1,909,735,597 | **1,102,829,000** | **−42.3 %** |
| `AestraSat::process` | 1,160,097,695 (60.8 %) | 152,052,695 + 201,120,000 (FIR) | 35 % |
| `tanhf` (libm) | 33,600,000 | 31,200,000 | — |

### CPU time in the render window (protocol collector, median of 4)

| | baseline | after | |
|---|---:|---:|---|
| process CPU (quiet box, median of 3) | 0.85 s | **0.48 s** | **−43.5 %** |
| process CPU (box under a parallel build) | 1.105 s | 0.67 s | −39.4 % |
| wall (incl. project load) | 3.385 s | 2.73 s | −19.4 % |

### Live playback, real device (512 frames @ 48 kHz, budget 10.667 ms)

| | baseline | after |
|---|---:|---:|
| callback average | 1.189 / 1.207 ms | **0.755 / 0.756 ms** (−37 %) |
| callback maximum | 1.529 / 4.022 ms | **1.252 / 1.141 ms** (−72 % worst case) |
| deadline misses | 0 | 0 |
| xruns / underruns | 0 | 0 |
| RT misuse violations | 0 | 0 |

### The sound did not change

- canonical peak: `0.999700010` -> `0.999700010` (identical)
- canonical RMS: `0.442925296` -> `0.442925295` (1 ULP)
- every per-lane RMS identical to 7–9 significant digits
  (chord `0.334416579` -> `0.334416580`, 1 ULP)
- round-trip magnitude at 1/5/10/15/18/20 kHz matches the reference to
  **1e-6 dB**

### Tests

- `OversamplerParityTest` (new, 5/5) — carries its own copy of the original
  scalar implementation and compares sample by sample:
  max abs delta **2.98e-07**, RMS relative **−143 dB**, latency unchanged,
  magnitude response unchanged.
- `AestraSatTest` 16/16 PASS (incl. `OversamplingSuppressesAliasing` < −50 dB)
- `AestraCompOversamplingTest` PASS
- `FolioProbeFixture` PASS (63 checks, 0 failures)

---

# RUN 2 — Frames: the never-idle loop

## What was wrong

With **zero user input** the app presented 816 frames in 20 seconds (30 fps)
and burned 27 % of a core doing it. The reason was a self-sustaining
invalidation loop:

1. `FileBrowser`'s static render re-applied the search field's placeholder text
   **on every frame** (`FileBrowser.cpp:530`).
2. `NUITextInput::setPlaceholderText` invalidated **unconditionally** — even
   when the string was byte-identical to the one already set.
3. So the field dirtied -> the browser dirtied -> the browser invalidated its
   own FBO cache -> the cache rebuild re-ran the render path -> step 1.

The whole window therefore repainted 30 times a second, forever, for nothing.
The July 2026 baseline had predicted exactly this ("~4,100 icon invalidations a
second with nothing changing") and fixed half of it; the rest was still there.

A second, separate waste: the browser draws its file-list rows by **mutating a
shared icon instance per row** (`setBounds` + `setColor` + `onRender`), so one
icon per visible row was invalidated 435 times a second for a component that
never changed. And `NUIIcon::setColor` re-dirtied an icon that had been given a
theme colour even when the resolved colour was identical.

## What changed

- `NUITextInput::setPlaceholderText` invalidates only on a real change.
- `NUIIcon::renderAt(rect, colour)` draws the icon at an explicit rect and
  colour **without changing the component's own state**; the browser uses it.
- `NUIIcon::setColor` invalidates only when the rendered colour changes.
- The frame probe now also reports **where** a dirty origin is
  (`[DirtyAt] Type<Parent>@x,y`) — that is the probe that found this.

## Measured (673x716 window, fixture loaded, 20 s, quiet box, final binary)

| Workload | Build | Presented | fps | **Process CPU** | work avg | p95 | over 16.7 ms | over 33 ms | tree ms | TrackMgrUI | FileBrowser |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| idle | baseline | 791 | 30.4 | **27.3 %** | 7.09 ms | 16.8 | 3 | 1 | 5.67 | 3.74 | 1.29 |
| idle | **after** | 131 | 4.9 | **7.4 %** | 7.77 ms | 18.5 | 3 | 2 | 5.34 | 3.54 | 1.22 |
| sweep | baseline | 761 | 29.3 | **28.3 %** | 8.33 ms | 44.2 | 24 | 4 | 6.70 | 4.40 | 1.55 |
| sweep | **after** | 116 | 4.5 | **7.4 %** | 9.41 ms | 42.9 | 10 | 4 | 5.68 | 3.78 | 1.29 |
| playback | after | — | 30 | — | 5.24–6.00 ms | 7.6–9.8 | **0** | 0 | 2.33–5.03 | 0.78–3.42 | — |

**The app now idles at the 3 fps heartbeat instead of repainting at 30 fps:
3.7x less UI CPU**, and it still presents every frame while the transport runs
(zero frames over the 16.7 ms budget).

### The FBO cache finally works

With the invalidation storm gone, `TrackManagerUI`'s cache holds:

| | ms/frame |
|---|---:|
| cache valid | **0.78** |
| cache invalidated | 3.42 |

The July baseline said the cache was "a massive FPS boost" that never materialised
because the cache was invalidated every frame. It now is not.

---

# Candidates — measured, not yet done

Ordered by size in the current profile.

| # | Target | Evidence | Idea |
|---|---|---|---|
| 1 | `TruePeakMeter::processStereo` | 115,989,881 Ir (10.5 %) | The same serial-accumulator pattern as the oversampler: 4 polyphase branches x 12 taps + a 12-float history shift per sample. It is a meter, so it cannot touch the sound. |
| 2 | `AudioEngine::mixAndMeterTrack` | 138,855,682 Ir (12.6 %) | The engine's per-track mix + meter loop. Needs a profile of its own (it is one big function). |
| 3 | `SamplerPlugin::process` | 116,321,640 Ir (10.5 %) | The sampler voices; likely the same accumulator pattern. |
| 4 | `AestraFilter::process` | 59,609,208 Ir (5.4 %) | The filter's biquads — a textbook SSE2 case. |
| 5 | `Sinc64Turbo::resolve` | 39,991,401 Ir (3.6 %) | The resampler's coefficient resolution, called per block. |
| 6 | `tanhf` | 31,200,000 Ir (2.8 %) | A vectorised tanh would be ~4x faster but changes the waveform by a polynomial's worth of ULP. It needs an **error budget**, not a speed number. |
| 7 | Remaining `AestraSat` cost | 35 % of the render | The plugin's own per-sample work (DC blocker, tone one-pole, dry-delay tap, mix) and the `shape()` switch, which is loop-invariant and should be hoisted out of the sample loop. |
| 8 | `TrackManagerUI` dynamic pass | 0.78–3.42 ms/frame | Control strips, meters and the playhead are redrawn every frame. Caching them per region would cut the worst case. |
| 9 | The present gate's transport branch | — | It reads `engine->isTransportPlaying()` and `tm->isPlaying()`. The Muse transport verbs set only the engine's, so an agent-started transport schedules no timeline and moves no playhead (pre-existing; the gate itself predates this work). |
| 10 | Idle heartbeat | 7.6 % CPU at idle | With the loop gone there is room to drop the heartbeat below 3 fps, or to make the idle rate adaptive to the measured cost. |
| 11 | Window size | — | The app asks for 1366x768 maximised; Hyprland tiled it to 673x716. The Folio's real screen is 1366x768, so this is a WM artefact, but the frame cost scales with area and the 4 GB target should be measured at its native size. |

## Second sweep — leads from reading the code (not yet measured)

A second pass over the engine, the effect chain, the clip renderer, the GL
renderer, the widget tree and file import produced these. They are **leads, not
results** — each needs a measurement before it becomes a claim.

### Audio

| Lead | Where | Note |
|---|---|---|
| **Silent tracks still run their whole chain** | the engine's per-track path | Every track clears a buffer, renders clips, converts, runs every effect, converts back, then mixes and meters — even when nothing on the track is playing. Built-in plugins report no tail (`getTailSamples()` returns 0 in `InternalPluginBase.h`), so the engine cannot skip safely today. If most tracks are silent most of the time, this is the largest audio win left — but it is an architecture change (real tails + a skip rule), not a micro-fix. **The highest-value item on this list.** |
| Sidechain buffer converted per track per block, even with no sidechain | `AudioEngine.cpp:2602` | The loop converts and peak-checks a buffer that nothing feeds. Also reads a buffer that is never cleared for tracks with no sidechain. A few lines to gate on "this track actually has a sidechain this block". |
| Delay-compensation ring does two `%` per sample | `AudioEngine.cpp:2659` | The same defect Run 1 removed from the oversampler. The send-delay path already uses a power-of-two mask; the track ring should too. |
| Clip fades worked out per sample, with two divisions | `ClipRenderKernel.cpp:128` | Decide once per block whether the block touches a fade; if not, use a plain multiply-add loop that vectorises, and drop the per-sample sanitize. |
| Post-effect sample validation exits early per sample | `EffectChain.cpp:70` | The early exit blocks vectorisation and it runs after every plugin. A branch-free accumulate, or one check at the end of the chain. |
| Plugin-id string comparison per slot per block | `EffectChain.cpp:579, :695` | Cache it as a flag when the snapshot is built. |
| Instruments matched to MIDI by searching all units, per track | `renderTrackUnits` | — |

### Build

- **LTO is on for `Source/` but not for `AestraAudio/`** (`Source/CMakeLists.txt:237`
  sets `-flto`; `AestraAudio`'s flags are `-O3 -DNDEBUG` with no `-flto`). The DSP
  lives in the library that does *not* get it, so its kernels cannot inline
  across the boundary. Extending it is a one-line change — **but it is a
  one-line change to the whole audio library's link time and RAM on a 4 GB
  box, so it must be measured, not assumed.**
- **`cmake/LowMemory.cmake` disables auto-vectorisation** (`-fno-tree-vectorize`,
  `-fno-vectorize`) and drops to `-O2`. That is fine as long as no shipped or
  *measured* build uses it. **This session's build did not** (flags verified:
  `-O3 -DNDEBUG`, no `-fno-tree-vectorize`), so the measurements above stand. A
  CMake warning would stop anyone benchmarking the wrong build.
- **The Folio's i5-3337U has AVX1 (256-bit float) but no AVX2 and no FMA.**
  Run 1 used SSE2 because it needs no compiler flag at all. An AVX1 path via
  `target_clones("avx","default")` or runtime dispatch would double the FIR
  width — allowed by AGENTS.md ("SIMD must use runtime dispatch or isolated
  per-translation-unit targeting"), and the only global-flag rule still holds.

### Graphics and UI

| Lead | Where | Note |
|---|---|---|
| **4x MSAA on the main window** | `PlatformWindowLinux.cpp:58` | Shapes and text already smooth their edges in the shader, so on the HD 4000 this is mostly fill rate and a resolve at every swap. Swap is ~1.2 ms of the frame. An env toggle to turn it off and compare swap time + screenshots would settle it in one run. |
| **The transform stack is recombined for every vertex** | `addVertex` → `applyTransform` → `composeTransformStack` | Four times per quad, pure CPU in the "tree" phase. Combine once per push/pop instead. |
| **17 floats per vertex, and the index buffer is rebuilt every frame** | the GL renderer | Most of the vertex data is per-quad values copied four times (~300 bytes/quad), and the index pattern never changes. A static index buffer is the quick win; instanced quads are the real one (~6x less data). |
| **Extra flushes, each one a draw call** | `ensureBasicPrimitive`, clip-rect changes, font-atlas switches | Changing the primitive type flushes even though the type is now per-vertex; a clip-rect change flushes even when the rect is identical; each font atlas switch flushes. Draw calls are expensive in the Mesa driver here. `drawCallCount_` already exists — **log it per frame and look**. |
| **Every label re-runs layout every frame** | text draw path | UTF-8 decode, glyph lookup and kerning-pair lookup per character, per frame, plus a redundant `glBlendFuncSeparate`. A cache keyed by (string, size) would skip most of it. |
| **No off-screen culling in `renderChildren`** | `NUIComponent.cpp:596` | Every visible child renders even when it is outside the clip area, and it copies a `shared_ptr` per child per frame (an atomic inc/dec). A bounds-against-clip check in the base class would cover scrolled-off mixer strips, track rows and piano-roll notes. |
| **Waveform peak memo keyed by source only** | `TrackUIComponent.cpp:900` | The same sample used in several clips with different trims overwrites the previous entry, so it misses every frame. Key by (source, range, width), or use a small LRU. |

### Loading and memory

| Lead | Where | Note |
|---|---|---|
| **Project open decodes every audio file serially** | `ProjectSerializer.cpp:1691` | `SamplePool` already has a thread pool and `acquireAsync`, and nothing calls them. Parallelising the decodes should cut open time 2–4x on 2 cores. |
| **Dropping N files starts N threads** | `TrackManagerUIDragDrop.cpp:501`, `AestraContentImport.cpp:74` | All N full float decodes are in memory at once before compaction. On the 4 GB target, dropping 12 tracks could spike hard. Route them through the bounded pool. |
| **Waveform peaks are not saved to disk** | — | They are rebuilt from the full decode on every project open. A small cached peaks file per source makes reopening faster. |

## Findings that are not performance

- **The frozen Folio fixture no longer loads its effect chain on develop** (v1
  plugin blobs vs the keyed-v2 reader). It measured a graph with no effects.
  Rebuilding it against develop also needs `CAPABILITIES.json`'s
  `requiredVersion` moved 3 -> 4.
- **`InternalPluginBase::loadState` has no v1 reader.** A project written by an
  earlier Aestra loses its built-in plugin settings on reload: the slots go
  empty rather than run on defaults. It is not user-visible yet (the format
  changed before any release), but the same hole will open the next time the
  blob format moves.
- **The Muse `play`/`stop` verbs drive the engine only.** They advance the
  engine's clock but schedule no timeline, so an agent-started transport
  produces silence and no moving playhead.
- The `labs/perf` apparatus is gitignored on `develop` and the CMake hook for it
  exists only on `perf/folio-baseline-v1`; this session added the hook to the
  working branch so the apparatus can measure `develop` without moving the
  frozen apparatus.

## Status

- [x] Baseline build, audio harness, frame harness, baseline numbers
- [x] Run 1: oversampler SIMD — measured, verified, output unchanged
- [x] Run 2: never-idle loop — measured, verified, 3.5x less UI CPU
- [x] Tests: oversampler parity (new), AestraSat, AestraComp oversampling,
      Folio probe fixture, UI scenario suite
- [ ] Run 3 (next): TruePeakMeter + the engine's per-track mix/meter loop
- [ ] Candidate list (above) for the founder to pick from
