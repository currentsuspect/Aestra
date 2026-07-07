# Clip Prefilter Lifecycle Design (Phase 4, F1 fix)

Status: implemented 2026-07-07 (this document is the pre-implementation plan the
mission required; measured results live in `audio-research-bench.md` §10).
Scope: downsampled clip anti-aliasing via the Phase-3 Option B architecture —
a designed Kaiser low-pass applied ONCE off the audio thread, feeding the
existing render kernels unchanged.

## 1. Where decoded clip audio lives now

`AudioBufferData` (interleaved float + sampleRate/channels/frames,
`Models/ClipSource.h:30`) owned as `shared_ptr` by `ClipSource::m_buffer`,
inside `SourceManager` (a `TrackManager` member). Producers — all non-audio
threads: sample import (`AestraContent.cpp:3310`, synchronous decode on the
main thread), project load (`ProjectSerializer.cpp:1313`), recorded takes
(`SourceManager::createRecordedSource`). Live render graphs co-own buffers via
`ClipRenderState::bufferOwner` (`AudioGraphBuilder.cpp:189`), so buffer
replacement never invalidates an in-flight graph.

## 2. Where the rates become known

Source rate: `AudioBufferData::sampleRate`, fixed at decode (native rate; no
import-time resampling). Session/output rate: `TrackManager::setOutputSampleRate`
→ `PlaylistModel::m_projectSampleRate` (callers: `AestraAudioController.cpp:342`,
`AestraApp.cpp:772`, `HeadlessMain.cpp:351`, and `AudioExporter.cpp:203/277`
temporarily around an export). The two meet in
`PlaylistModel::buildRuntimeSnapshot` (`PlaylistModel.h:411+`), which resolves
source buffers into the runtime snapshot — that is the natural selection point.

## 3. Ownership of filtered copies

* Storage: `ClipSource` gains one filtered slot
  (`m_filteredBuffer` + key fields). Exactly one filtered variant per source —
  the current session rate only. The slot is only ever read/written on the
  graph-build thread (see §7), so it needs no lock.
* Computation: a new `ClipPrefilterService` (owned by `TrackManager`, declared
  as its last member so it shuts down first): one lazily-started worker thread
  with a mutex/cv job queue. The worker runs pure DSP on its own owned copies
  and NEVER touches `SourceManager`/`ClipSource` (the source map is not
  thread-safe); completed results are drained back on the graph-build thread.
* DSP: `DSP/ClipPrefilter.{h,cpp}` pure functions — the Phase-3 Option B
  design: Kaiser low-pass, passband edge 0.9× destination Nyquist, stopband
  edge at destination Nyquist, 100 dB attenuation, odd length, delay-compensated
  by its integer group delay, double-precision accumulation.

## 4. Keying

`(ClipSourceID, ClipSource::contentRevision, source sampleRate, target/session
rate, kClipPrefilterSpecVersion)`. Channel count is implicit in the buffer.
Quality tier is deliberately NOT part of the key: prefiltering happens upstream
of every interpolation kernel and applies uniformly to all tiers.

## 5. Invalidation

Stale = key mismatch, evaluated lazily at graph-build time (no observers):

| Event | Effect |
| --- | --- |
| Session rate change | next `ensureClipPrefilters()` sees `filteredRate != projectRate` → slot cleared, falls back to original, re-enqueues if still downsampling; if new rate ≥ source rate the slot is simply cleared |
| Clip source replacement / decoder reload | `setBuffer` bumps `contentRevision` → key mismatch; worker results carrying an old revision are discarded at drain time |
| Quality setting change | no-op (not keyed) |
| Project reload | `SourceManager::clear()` destroys sources and their filtered slots naturally |

## 6. Memory policy (4 GB target)

A filtered copy is the same size as its source (same rate/channels/float). Only
sources with `sourceRate > sessionRate` get one → 2× RAM for downsampled clips
only, 1× for everything else. Worst-case example: a 4-minute 96 kHz stereo clip
≈ 184 MB → +184 MB. No eviction in this phase: invalidation already drops
copies that stop qualifying, and typical 4 GB-machine sessions are not built
from long 96 kHz sources. A size-capped eviction policy is the documented
follow-up if telemetry ever shows pressure. ("Replace the original" was
rejected: the original serves waveform UI and future rate changes, and
replacing it would change `ClipSource` semantics — a bigger, riskier PR.)

## 7. Fallback while the filtered copy is not ready

`buildRuntimeSnapshot` prefers a ready, key-matching filtered buffer and
otherwise uses the original — i.e. exactly the pre-Phase-4 render path. Nothing
blocks anywhere; the audio thread never learns the service exists (graphs swap
atomically as today). When a job completes, the worker calls
`requestAudioGraphRebuild(...)` — fully atomic (`TrackManager.h:810`) — so the
app's `PlaybackGraphController::drainIfDirty` pump rebuilds with the filtered
copy in place. Deterministic completion for offline/export/tests:
`TrackManager::waitForClipPrefilters()` blocks the CALLING (non-audio) thread
until the queue is idle.

## 8. Render integration points

Exactly ONE selection point: the buffer resolution in
`PlaylistModel::buildRuntimeSnapshot`. Realtime playback (`renderGraph`),
full-mix export (`AudioExporter` → the same `processBlock`), and isolated-track
bounce (`AudioRenderer`, same compiled graph) all consume that snapshot —
**parity is preserved by construction**. The enqueue hook is
`AudioGraphBuilder::buildFromTrackManager` → `TrackManager::ensureClipPrefilters()`
(drains completed results into sources, clears stale slots, enqueues missing
work, dedupes in-flight keys) before the snapshot is taken. The filtered buffer
keeps the SOURCE sample rate (filtering does not resample), so
`clip.sourceSampleRate`, `sampleOffset`, and all timing math are unchanged.
Export at a different rate than the session: the exporter renders the engine's
CURRENT graph (it does not rebuild at the export rate), so this pre-existing
edge keeps its pre-Phase-4 behavior and is documented as uncovered rather than
half-fixed; exports at the session rate — the `bounceRangeToWav` case all
parity tests exercise — inherit the filtered copies from the live graph.

## 9. Tests

Before implementation (baseline, all green on develop@5761b370): research suite
4/4, `ctest -L audio` 59/59. After:

* `SessionResamplingTruthTest`: downsampling pairs wait for prefilters +
  rebuild, then the alias gates flip from "KNOWN LIMITATION −1.1/−0.0 dBc" to
  **< −95 dBc**; identity nulls compare against a
  production-`ClipPrefilter` + legacy-interpolator replica; same-rate,
  upsampling, control, export-parity and isolated-bounce unification gates all
  stay as-is (proving no behavior change where none is intended).
* `RTAllocationTrapTest`: stays green (the worker never touches the audio
  thread; the audio callback path is unmodified).
* Parity tests (`RealtimeExportParityTest`, `ExportBounceParityTest`,
  Arsenal export tests): stay green unchanged.
* Full `ctest -L audio` sweep before push.

Out of scope (documented uncovered paths): PreviewEngine (F7), SamplerPlugin
(F8), AuditionEngine — they own separate buffers/pipelines and keep their
current behavior.
