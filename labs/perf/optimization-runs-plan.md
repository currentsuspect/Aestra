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
  **This needed a second commit.** `sinc()` at an even offset leaves
  `sin(pi*m)` at ~1e-17 rather than exactly 0, so the float coefficient came
  out a tiny non-zero number and the phase builder could not tell it from a
  real tap — it kept all 23 terms of the short phase and multiplied 22 of them
  by nothing. CodeRabbit caught it. Snapping those taps to an exact zero makes
  the kernel the halfband it is meant to be and makes the skip fire: the short
  phase becomes one tap instead of 23, the render goes 0.48 s -> 0.47 s, and
  the fixture's canonical digest is **bit-identical**.

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

### Manual click-through (the gate for merging Run 2)

The real risk here is UI that updates late: while the app redrew 30 times a
second, a state change that forgot to mark itself dirty still showed up right
away. Now the same change can take up to the 300 ms heartbeat. So it was
clicked through by hand, with the pointer driven by `ydotool` and the app's own
dirty-origin probe as the evidence:

| Check | Result |
|---|---|
| Play — transport starts, playhead moves | **pass** — 56 fps, playhead confirmed moving between two captures |
| Hover states | **pass** — `NUIButton<TrackUIComponent>` dirties on hover |
| Click a control (mute) | **pass** — button and label repaint; the state persists |
| **Fader drag while stopped** | **pass** — the master fader's readout went 0.0 -> 3.0 dB live |
| File preview + meters | **pass** — the preview plays, the visualiser runs, the position advances |
| The blinking caret | **pass** — 7 of 9 consecutive 200 ms captures differ in the caret region |
| Menus open and close | **pass** — the File menu opens with every item and closes on Escape |
| Clip drag in the timeline | **pass** — the drop preview and the minimap update |
| Typing in the browser's search field | **pass** — the list filters, the empty state renders |

The "late update" risk did not materialise.

> Tooling note, because it cost an hour: `ydotool mousemove` is **relative** by
> default (it needs `--absolute`), and in this ydotool version `0x01` is the
> RIGHT mouse button — a left click is `0xC0`. The first attempts were
> right-clicking a window that was in fact receiving pointer events all along.

### The FBO cache finally works

With the invalidation storm gone, `TrackManagerUI`'s cache holds:

| | ms/frame |
|---|---:|
| cache valid | **0.78** |
| cache invalidated | 3.42 |

The July baseline said the cache was "a massive FPS boost" that never materialised
because the cache was invalidated every frame. It now is not.

---

# RUN 3 — Link-time optimisation for every compiler (PR #1202)

`Source/CMakeLists.txt` added `-flto` to the **app target only**, and only for
non-MSVC compilers. So the **Windows release build** — the one
`private-release.yml` ships — got no LTO in any library, and `AestraAudio`,
where the DSP lives, got none on any platform.

Now each first-party target carries `INTERPROCEDURAL_OPTIMIZATION_RELEASE`, so
CMake picks the right spelling per compiler: `/GL` + `/LTCG` on MSVC, `-flto`
elsewhere. `-DAESTRA_ENABLE_IPO=OFF` turns it back off.

| | before | after | |
|---|---:|---:|---|
| stripped app binary + assets | 12,220,644 B | **11,702,300 B** | −4.2 % |
| AestraHeadless peak RSS, stress | 28,692 kB | **26,528 kB** | −7.5 % |
| AestraHeadless peak RSS, tone | 24,744 kB | **22,340 kB** | −9.7 % |
| unstripped app binary | 15,033,344 B | **11,925,640 B** | −21 % |
| offline render CPU | 0.48 s | 0.50 s | **no change** |
| frame tree cost | 5.34 ms | 5.44 ms | **no change** |

**This is a size and memory win, not a speed win.** The SSE2 oversampler
already took the arithmetic win that was available in the render; LTO adds
nothing on top of it. It is worth having on the 4 GB target for the RSS and
for the installer budget, and it closes the Windows gap — and it is not worth
having for speed, which is what this says plainly.

The budget gate passes on all three metrics.

---

# RUN 4 — The meter and the mix (PRs #1203, #1204)

Both are the same pattern as Run 1, applied to the two remaining measured
hotspots.

## The true-peak meter (PR #1203)

It was the second-largest single item in the render — 115,989,881 instructions,
6.1% — for a meter that cannot touch the sound. The cost was structural: the
polyphase coefficients were stored phase-major and the hot loop ran **four
separate 12-tap dot products**, one per phase, each with its own serial
accumulator. But the four phases read the SAME history, so the whole thing is
one matrix-vector product that fits in four lanes of a single vector.

Storing the coefficients **transposed** (`m_polyphaseT[tap][phase]`) lets one
12-iteration loop accumulate all four phase sums at once, and the magnitude
reduction over the four lanes replaces four separate reductions. The history
shift goes from twelve scalar copies to three vector moves.

**The tap order is unchanged, so each lane accumulates exactly the same
products in exactly the same order — the result is BIT-IDENTICAL, not close.**
`TruePeakMeterParityTest` holds a copy of the original implementation and
asserts exact equality over noise, sines at 997 Hz / 15 kHz / 19 kHz and an
impulse.

- meter instructions per 5 s render: **115,989,881 -> 37,457,353 (-67.7%)**
- render CPU, develop baseline: 0.93 s -> 0.81 s (-13%)

## The strip's mix (PR #1204)

`mixAndMeterTrack` was 138,855,682 instructions, 7.3%. Its line-level profile
showed three unrelated costs:

1. The magnitude was not `fabs`. `(outL >= 0.0) ? outL : -outL` is a
   compare-and-select, because for -0.0 the ternary returns -0.0 while `fabs`
   returns +0.0, so the compiler cannot use a sign-bit clear. Those two lines
   cost 27.7M instructions on their own.
2. The delay-compensation ring did **two `%` per sample** — the same defect
   Run 1 removed from the oversampler. The ring length is not a power of two,
   so this was a real division sequence, twice, per sample, in the one loop
   that runs for every sample of every track with a delay to compensate.
3. The stereo arithmetic was scalar. The strip's samples are interleaved L/R
   doubles, so one 2-wide vector carries both channels through the gain, the
   store and the mix, and the peak tracking becomes one vector maximum.

The kernel moved out of `AudioEngine.cpp` into a new
`AestraAudio/include/Core/TrackMixKernel.h`, so the pinned file **shrank** from
4393 to 4380 lines and its file-size ratchet row was lowered to match.

**Bit-identical**: the fixture's canonical digest is UNCHANGED at
`910d3c3e2cc03ee3` — the same digest the develop baseline produces.

- `mixAndMeterTrack` instructions per 5 s render: **138,855,682 -> 97,305,441 (-30%)**
- render CPU, develop baseline: 0.87 s -> 0.79 s (-9.2%)

## All three audio PRs together

| | develop | after | |
|---|---:|---:|---|
| render instructions per 5 s | 1,909,735,597 | **935,405,401** | **-51.0 %** |
| render CPU (box idle) | 0.865 s | **0.395 s** | **-54.3 %** |

The profile after all three:

| Function | Ir | % |
|---|---:|---:|
| `HalfbandStage::dotProduct` | 156,480,000 | 16.7 % |
| `AestraSat::process` | 151,573,164 | 16.2 % |
| `SamplerPlugin::process` | 116,321,640 | 12.4 % |
| `mixAndMeterTrack` | 95,160,449 | 10.2 % |
| `AestraFilter::process` | 59,609,208 | 6.4 % |
| `processBlock` | 47,007,495 | 5.0 % |
| `Sinc64Turbo::resolve` | 39,991,401 | 4.3 % |
| `TruePeakMeter::processStereo` | 37,457,353 | 4.0 % |
| `tanhf` | 31,200,000 | 3.3 % |

---

# RUN 5 — The sampler (PR #1205)

## What was wrong

A line-level profile of a four-second render put the sampler's voice loop at
**74 million instructions** for the loop header and the `if (!v.active)` test
alone, against 90 million for the whole plugin. The render loop walked all 32
voice slots on every sample to find the one or two that were sounding.

Two smaller pieces of per-sample work that did not change from sample to sample
went with it:

- The playable window (start, end, loop geometry) was rebuilt per voice per
  sample — eight double operations and four branches — for a value identical
  for every sample of a voice in a block. It is cached now, keyed on the
  voice's frame count **and** the block's loop points.
- The voice compensation is `1/sqrt(soundingVoices)`: a square root and a
  division on every sample of every block. It is computed when the count
  changes.

## Measured

| | before | after | |
|---|---:|---:|---|
| `SamplerPlugin::process` per 5 s render | 116,321,640 Ir | **14,097,586 Ir** | **-88 %** |
| offline render instructions | 1,909,735,597 | 1,807,537,206 | -5.4 % |
| offline render CPU (median of 5, 15 s runs) | 1.34 s | **1.27 s** | **-5.2 %** |

**Bit-identical**: the fixture's canonical digest is unchanged at
`910d3c3e2cc03ee3` — the same digest develop produces.

## Two things that were measured and dropped

| Lead | Result |
|---|---|
| De-interleave the sampler's stereo samples with two loads and two shuffles instead of gathering them with eight scalar loads | **No change.** A micro-benchmark of the 64-tap kernel in isolation: 42.2 ns with the gather, 42.8 ns with the shuffles. The kernel is latency-bound, not gather-bound. The AVX2 path has always done it the shuffle way; the SSE4.1 path's gather was not the cost. |
| The window cache on its own | 116,321,640 -> 112,425,360 Ir (-3.4 % of the plugin, 1.2 % of the render). Real, but most of the win was the compact voice list, and the window cache alone was not worth a change on its own. It is in PR #1205 because it rides along with the list and the two share the same reasoning. |

---

# Candidates — measured, not yet done

Ordered by size in the current profile.

| # | Target | Evidence | Idea |
|---|---|---|---|
| 1 | `TruePeakMeter::processStereo` | 115,989,881 Ir (6.1 %) | **DONE — PR #1203.** 115.9M -> 37.5M, bit-identical peaks. |
| 2 | `AudioEngine::mixAndMeterTrack` | 138,855,682 Ir (7.3 %) | **DONE — PR #1204.** 138.9M -> 97.3M, bit-identical render. |
| 3 | `SamplerPlugin::process` | 116,321,640 Ir (12.4 % of the new total) | **DONE — PR #1205.** 116.3M -> 14.1M, bit-identical. The cost was walking 32 voice slots per sample to find one or two. |
| 4 | `AestraFilter::process` | 59,609,208 Ir (6.4 %) | The filter's biquads. An IIR cannot be vectorised across TIME, but it can be vectorised across the two CHANNELS, which are adjacent in memory — bit-identical, because each channel's recurrence is unchanged. |
| 5 | `HalfbandStage::dotProduct` | 156,480,000 Ir (16.7 %) | The oversampler's FIR is now the largest single item. It is already SSE2 and already skips the zero taps; what remains is the tap count itself (47 + 29 taps per stage, twice). A different filter design (fewer taps, a different prototype) would change the transfer function, so this is bounded by the -79 dB round-trip figure, not by the code. |
| 6 | `Sinc64Turbo::resolve` | 39,991,401 Ir (4.3 %) | The resampler's coefficient resolution, called per block. |
| 7 | `tanhf` | 31,200,000 Ir (3.3 %) | A vectorised tanh would be ~4x faster but changes the waveform by a polynomial's worth of ULP. It needs an **error budget**, not a speed number. |
| 8 | Remaining `AestraSat` cost | 16.2 % of the render | The plugin's own per-sample work (DC blocker, tone one-pole, dry-delay tap, mix) and the `shape()` switch, which is loop-invariant and should be hoisted out of the sample loop. |
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

## Leads that were measured and turned out not to be worth doing

Both were on the list as small, safe wins. Both measured as noise, and neither
is shipped:

| Lead | Why it was dropped |
|---|---|
| Only convert the sidechain buffer when something feeds it (`AudioEngine.cpp:2602`) | The whole function, `processTrackEffects`, is 0.47 % of the render, so halving its work is worth at most a quarter of a percent. The stale-buffer worry is also unfounded: the engine clears the buffer for every receiver, so a track whose plugins consume a sidechain has a buffer that was written or cleared this block. |
| Combine the transform stack once on push and pop instead of per vertex | The stack is empty almost everywhere and depth 1 inside the FBO cache, so composing it per vertex is already a handful of multiply-adds. The cached version measured the same frame cost (tree 5.34 -> 5.44 ms, noise) and would have added a stale-cache failure mode for nothing. |

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

## Where it all landed

All six PRs are merged into `develop`.

| PR | What | Render CPU | Instructions |
|---|---|---|---|
| baseline | develop @ `def874133` | 1.35 s | 1,909,735,597 Ir |
| #1199 | the oversampler FIR in SSE2 | | 1,102,829,000 Ir |
| #1203 | the true-peak meter in one 4-wide accumulator | | |
| #1204 | the strip's mix in one 2-wide vector | | |
| #1205 | the sampler's compact voice list | | |
| #1202 | link-time optimisation for every compiler | | |
| **develop now** | | **0.63 s (-53.3 %)** | **830,884,104 Ir (-56.5 %)** |

Live playback on the real device (512 frames @ 48 kHz, budget 10.667 ms):

| | before | after |
|---|---:|---:|
| callback average | 1.008 / 1.018 ms | **0.423 / 0.443 ms** |
| callback worst case | 2.716 / 2.749 ms | **1.342 / 1.249 ms** |
| deadline misses | 0 | 0 |
| xruns | 0 | 0 |

**The live audio callback is 57 % faster and its worst case is more than
halved.** On the 4 GB target that is the difference between a callback that
touches a quarter of its budget and one that touches a twentieth.

### What the sound did

Everything is bit-identical **except** the oversampler's zero-tap snap, which
changes the kernel by about 1e-17 relative — below one float32 ULP. The
per-lane RMS values are unchanged to the last digit, the canonical peak is
identical, and the parity test bounds the difference at 2.98e-07 sample delta
and -143 dB relative. The canonical *digest* changed, because a digest is a
hash of the bits and one ULP is one bit.

## Status

- [x] Baseline build, audio harness, frame harness, baseline numbers
- [x] Run 1: oversampler SIMD — measured, verified, output unchanged (PR #1199)
- [x] Run 2: never-idle loop — measured, verified, 3.5x less UI CPU
- [x] Tests: oversampler parity (new), AestraSat, AestraComp oversampling,
      Folio probe fixture, UI scenario suite
- [ ] Run 3 (next): TruePeakMeter + the engine's per-track mix/meter loop
- [ ] Candidate list (above) for the founder to pick from
