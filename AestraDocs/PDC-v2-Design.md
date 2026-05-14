# Plugin Delay Compensation v2 — Design

**Status:** Approved with revisions — ready for P0 → P1 transition
**Owner:** *(unassigned — drafted by Cascade)*
**Last Updated:** 2026-05-14
**Scope:** Internal architecture doc. Not public-facing.

**Revision history:**
- 2026-05-14 r0 — Initial draft.
- 2026-05-14 r1 — Reviewer feedback integrated: solver/application separation (`LatencyGraph`/`SolvedLatencyTopology` as first-class artifacts); compensation is derived render state, never stored on `AudioRoute`; `LatencyDomain` concept introduced; G3 smooth-recompute uses historical-sample duplication instead of silence smear; G5 mandates power-of-two ring masking on both paths; branching-convergence stress test added; sidechain node-boundary approximation explicitly marked as future work. Open questions resolved.
- 2026-05-14 r2 — P3 implementation note: `SolvedLatencyTopology` is double-buffered under its **own** atomic index (`m_activeSolvedTopologyIndex`), separate from the existing `m_activeRenderTrackIndex` used for `AudioGraphState`. §9's "shared index" recommendation is deferred to P4+, when the engine reads compensation values from `SolvedLatencyTopology` directly rather than from `TrackRTState`. Unifying earlier would force a full `AudioGraphState` copy on every PDC recalc, which is more expensive and risky than the win it delivers in P3.

---

## 1. Context — What Already Exists (v1)

`@AestraAudio/src/Core/AudioEngine.cpp:3081` (`AudioEngine::calculateLatencyCompensation`) and `@AestraAudio/include/Core/AudioGraphState.h:48` (`TrackRTState::pluginLatencySamples`, `compensationDelaySamples`, `compensationBuffer`) implement a working v1:

1. For each non-muted track, sum `EffectChain::getTotalLatency()` (`@AestraAudio/src/Plugin/EffectChain.cpp:575`), which sums each non-bypassed plugin's `getLatencySamples()`.
2. Find max across tracks → `m_maxProjectLatency`.
3. Per-track `compensationDelay = max - trackLatency`, applied via a fixed-size (16384 frames stereo) ring-buffer inside `processBlock` (`@AestraAudio/src/Core/AudioEngine.cpp:2364`).
4. Recompute triggered on effect-chain change via `setEffectChainLatencyCallback` (`@AestraAudio/src/Core/AudioEngine.cpp:474`) and on `markLatencyDirty()`.

**v1 is correct for the simple case:** N parallel tracks → master, all FX local to each track.

---

## 2. v1 Gaps (real, verified in code)

| # | Gap | Evidence | Impact |
|---|-----|----------|--------|
| **G1** | **Bus/send chains not accounted for.** Per-track latency includes only that track's own `EffectChain`. Latency of a downstream bus's plugins is not propagated back to feeder tracks. | `calculateLatencyCompensation` iterates `trackManager->getChannel(i)->getEffectChain()` only. No traversal of `AudioRoute::targetChannelId` chains. | Any project routing through a bus with FX (mastering bus, parallel drum bus) has mis-aligned timing between tracks routed through different bus depths. |
| **G2** | **Sidechain inputs are not latency-aligned.** Sidechain feeders ride a separate buffer path (`@AestraAudio/src/Core/AudioEngine.cpp:2490`) but no per-feeder compensation delay is applied to match the host plugin's signal arrival time. | The sidechain routing graph is built (`addTrackEdge` line 1654, `m_rtSidechainIncoming` line 1666) but no PDC math runs against those edges. | Sidechain compressors trigger early — ducking lands before the kick on any signal path with upstream latency. Audible. |
| **G3** | **Latency change during playback causes dropout.** `calculateLatencyCompensation` resets `compensationBuffer` to zero on every recompute (`line 3131`). | `rtState.compensationBuffer.fill(0.0f)` on every recalc, even if the new delay is similar to the old. | Plugin reports a latency change → click + silence the size of the new delay. VST3 `kLatencyChanged` and CLAP `plugin.latency` can fire mid-playback. |
| **G4** | **Mute on the highest-latency track shifts all other tracks instantly.** | `if (muted) continue;` skips muted tracks when finding `maxLatency` (`line 3102-3103`). | Mute the slowest track → all other tracks' compensation drops to a new lower max → audible time jump for every track. |
| **G5** | **Compensation buffer is fixed at 16384 frames (≈340 ms @ 48 kHz).** | `std::array<float, 32768>` in `TrackRTState` (`@AestraAudio/include/Core/AudioGraphState.h:55`). Guard at line 2367: `if (delay < capacity)` — silently no-ops if over. | High-latency mastering/look-ahead plugins (>340 ms @ 48 kHz, or >170 ms @ 96 kHz) silently fail to align. No diagnostic. |
| **G6** | **No master-bus latency contribution.** Master may have its own FX but its latency does not enter the calculation. | `calculateLatencyCompensation` only walks tracks. | Recording compensation (the latency the user must monitor-shift by) underreports when master is non-trivial. |
| **G7** | **No tail handling on stop.** Adjacent but related. Plugins report `getTailSamples()` (per-plugin interface, `@AestraAudio/include/Plugin/PluginHost.h:262`) but engine doesn't drive the tail. | Searching shows `getTailSamples` defined in 6 plugins, called nowhere in engine. | Reverbs/delays cut off the moment transport stops. |
| **G8** | **Recompute touches the active `AudioGraphState` directly.** | Line 3138 writes `m_graphStates[activeIdx].maxProjectLatencySamples` while audio thread reads the same state. | Race on read of `latencyCompensationEnabled` and `maxProjectLatencySamples`. Not catastrophic (single-word reads), but undefined behavior. |
| **G9** | **No automated test coverage.** | `grep -r 'LatencyCompensation\|PDC' Tests/` returns build-time and runtime test scaffolding but no headless offline test asserting impulse-alignment across branches. | Regressions ship undetected. The recent `ArsenalExportLiveParityTest` failure is a related-domain canary. |

---

## 3. v2 Goals (in priority order)

1. **G1 — Graph-aware latency propagation through sends and bus chains.**
2. **G2 — Sidechain feeder compensation.**
3. **G9 — Headless impulse-response regression tests** (built alongside #1 and #2 to verify them).
4. **G3 — Smooth response to mid-playback latency changes** (no dropouts).
5. **G4 — Stable behavior when mute toggles** on the highest-latency contributor.
6. **G5 — Bounded compensation buffer with diagnostic on overflow** (and an off-RT path to grow it).
7. **G6 — Include master-bus latency in reported "monitor compensation" only** (no effect on inter-track alignment, since all tracks already share it).
8. **G8 — Make recompute publish atomically through the existing graph-state double-buffer flip**, not in place.
9. **G7 — Tail-on-stop handling.** (Stretch; can defer to a separate doc if scope blows out.)

**Non-goals (v2):**
- Negative-latency / look-ahead plugins reporting reverse delay. (Future v3.)
- Per-plugin variable latency within a single block. (Future v3.)
- Cycle detection in the routing graph beyond what AudioEngine already does. (Existing concern, not PDC-specific.)

---

## 4. Design — Graph-Aware Latency Propagation (G1)

### 4.0 Architectural principle — solver / application separation

**PDC v2 splits cleanly into two subsystems.** Latency solving and delay application are different concerns and must not share state:

```
Routing state (TrackManager, EffectChain, AudioRoute)
        │
        ▼   (off-RT, on graph change)
┌─────────────────────────┐
│   LatencyGraph snapshot │   ← pure immutable input artifact
└────────────┬────────────┘
             ▼   (off-RT solver, pure logic, deterministic)
┌─────────────────────────┐
│  SolvedLatencyTopology  │   ← pure immutable output artifact
└────────────┬────────────┘
             ▼   (atomic publish via existing graph-state double-buffer flip)
       RT engine consumes
```

- The solver is pure logic: `solve(LatencyGraph) -> SolvedLatencyTopology`. No engine references. Trivially unit-testable, replayable, snapshottable for diagnostics.
- Compensation values are **derived render state**. They are never stored on user-visible structures like `AudioRoute`, never serialized, never enter undo history.
- The RT engine consumes `SolvedLatencyTopology` read-only; it owns the ring buffers and the cursor arithmetic, but no policy decisions.

This separation also enables future tooling: visualization, deterministic repro from a saved `LatencyGraph` snapshot, and replay-based regression tests.

### 4.1 Data structures

```cpp
// Off-RT immutable input — built from routing state.
struct LatencyGraph {
    struct Node {
        uint32_t channelId;          // Track / bus / master
        uint32_t intrinsicLatency;   // Sum of non-bypassed plugin latencies
        bool muted;
        LatencyDomain domain;        // See §4.4
    };
    struct Edge {
        uint32_t srcNodeIdx;
        uint32_t dstNodeIdx;
        bool sidechainOnly;
    };
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    uint64_t generation;             // Monotonic; bumped on rebuild
};

// Off-RT immutable output — consumed by RT engine.
struct SolvedLatencyTopology {
    struct NodeSolution {
        uint32_t intrinsicLatency;
        uint32_t downstreamLatency;
        uint32_t totalPathLatency;
        uint32_t outputCompensationSamples; // Delay applied at this node's audible output
    };
    struct EdgeSolution {
        uint32_t srcNodeIdx;
        uint32_t dstNodeIdx;
        uint32_t compensationSamples;        // Delay applied at the feeder side
        bool sidechain;
    };
    std::vector<NodeSolution> nodes;
    std::vector<EdgeSolution> edges;

    // Diagnostics & reported metrics
    uint32_t projectAlignmentLatency;        // Max totalPathLatency across audible leaves
    uint32_t monitoringLatency;              // projectAlignmentLatency + master + device output (G6)
    std::vector<std::string> warnings;       // Cycles refused, oversize compensations, etc.
    uint64_t generation;                     // Mirrors LatencyGraph generation
};

// RT-side, per-send, derived. NOT a member of AudioRoute.
struct SendRTState {
    uint32_t compensationSamples;
    // Ring buffer state lives here, not on AudioRoute.
    // Storage may be inline (common case) or in oversize vector (§8).
};
```

### 4.2 Model

Treat the routing graph as a DAG with nodes = channels (tracks + buses + master) and directed edges = sends (`AudioRoute`). For each node, define:

- `intrinsicLatency(node)` = sum of non-bypassed plugin latencies on that node's effect chain. (Already computed.)
- `downstreamLatency(node)` = max over all output paths from `node` to master of (sum of `intrinsicLatency` along the path including master).
- `totalPathLatency(node)` = `intrinsicLatency(node)` + `downstreamLatency(node)`.

The required compensation for a leaf track `t` to align with the slowest leaf track is:

```
compensation(t) = max_over_all_leaves(L) totalPathLatency(L) - totalPathLatency(t)
```

For internal nodes (buses), no compensation is added at the bus itself; alignment is enforced **at the feeder boundary** before mixing into the bus. This means: when multiple leaves feed the same bus, each leaf is delayed so that its signal arrives at the bus input at the same project sample.

### 4.3 Solver implementation sketch

1. Memoized post-order DFS over `LatencyGraph` from master backward to leaves. Pure function: `LatencyGraph -> SolvedLatencyTopology`.
2. For each node: compute `intrinsicLatency`, `downstreamLatency`, `totalPathLatency` and write to `NodeSolution`.
3. For each edge (send): `edge.compensationSamples = max_sibling_totalPathLatency_at_target - this_feeder_totalPathLatency_at_target`. Written to `EdgeSolution`. **Never written back to `AudioRoute`.**
4. The existing per-track `compensationDelaySamples` (v1, lives in `TrackRTState`) is repurposed as `outputCompensationSamples` and now sourced from `NodeSolution`. Sidechain branches read the post-compensation tap (§5).

### 4.4 Cycle handling

`@AestraAudio/src/Core/AudioEngine.cpp` already rejects self-edges (`if (destIndex == srcIndex) return;` line 1662). The solver additionally rejects general cycles via three-color DFS, sets `EdgeSolution::compensationSamples = 0` on any edge that closes a cycle, emits a warning into `SolvedLatencyTopology::warnings`, and continues. The warning is consumed off-RT (logged once per generation) — never logged from the audio thread.

### 4.5 Latency domains — architectural seam for the future

PDC v2 ships with only one effective domain, but the concept is first-class so later work (low-latency monitoring, hardware inserts, frozen paths, anticipative processors) does not require rewriting the solver:

```cpp
enum class LatencyDomain : uint8_t {
    FullyCompensated = 0,   // Default. Participates in graph alignment.
    Realtime         = 1,   // Reserved: live-monitored path, exempt from compensation.
                            //           v2 does not produce this domain; reserved for v3.
};
```

Solver rules for v2:
- Every node defaults to `FullyCompensated`.
- A `Realtime` node, if introduced, contributes `intrinsicLatency = 0` and `outputCompensationSamples = 0` regardless of incoming edges. Its downstream consumers treat it as a zero-latency feeder.
- Domains do not cross-couple: a `Realtime`-fed bus mixes with `FullyCompensated` feeders normally; only the `Realtime` feeder is exempted.

Introducing the enum and threading it through the data structures now is cheap. Removing it later is expensive.

---

## 5. Design — Sidechain Compensation (G2)

The sidechain signal at a plugin's input must arrive at the same project sample as the main signal at that plugin's input.

- For a plugin `P` on node `N` at intra-chain offset `k` (samples of in-chain latency before `P` on `N`), the main signal at `P`'s input has accumulated `upstream(N) + k` samples of delay.
- The sidechain feeder `F` routing to `N` provides the sidechain bus. The sidechain signal at `P`'s input has accumulated `upstream(F)` samples plus the engine-side sidechain routing delay (currently zero).
- Required sidechain compensation: `(upstream(N) + k) - upstream(F)`. If negative (feeder is slower than host), the **host** path must be delayed at the feeder boundary instead — that's already handled by G1.

**Practical scope cut (v2):** Sidechain compensation operates at the **node boundary**: we treat `k = 0`, as if all of `N`'s plugins were at the start of the chain. Per-plugin-position sidechain alignment is deferred.

**This is an explicit approximation. Document it loudly in code and in user-facing docs when the time comes.** Both `EdgeSolution::compensationSamples` for sidechain edges and the RT-side sidechain-tap logic must carry a comment marking this as "v2 node-boundary approximation — see PDC-v2-Design §5 and PDC-v3 TODO." The error is bounded by `intrinsicLatency(N)`; for typical mix scenarios this is a few ms and inaudible, but a 1024-sample-look-ahead limiter on the host track would push it audible. The solver should also emit an informational warning into `SolvedLatencyTopology::warnings` for any sidechain edge where `intrinsicLatency(N) > kSidechainApproximationThresholdSamples` (suggest 128 samples) so the user can see when the approximation matters.

**Future work (v3):** per-plugin-position sidechain alignment requires the solver to know the slot index of the sidechain-receiving plugin within the chain. That needs `EffectChain` to expose per-slot pre-latency. Not a v2 concern, but the data-structure boundary above must remain clean to allow it.

---

## 6. Design — Smooth Recompute (G3)

Replace the "zero the ring buffer and reset read/write" behavior with a **read-cursor migration**. The two directions are asymmetric because the perceptual failure modes are asymmetric:

- **New `delay > old delay` (latency increased).** Shifting the read cursor back exposes a region that has not been written to recently. Naive zero-fill is mathematically correct but perceptually wrong: on sustained material it produces an audible micro-suck-out at the transition. Instead:
  - Hold the most recent valid sample (per channel) across the newly-exposed region (sample-and-hold), then cross-fade over `min(64, new - old)` samples from the held value to the actual delayed signal once the cursor catches the real buffered data.
  - Equivalent and simpler implementation: copy the earliest historical sample available into the pre-buffer region and do a short ramp; the brain treats a held DC offset of a few ms as imperceptible compared to a silence gap.
  - Rationale: humans detect discontinuities (clicks, gaps) far more readily than sub-millisecond temporal inaccuracy in already-misaligned material.
- **New `delay < old delay` (latency decreased).** Cross-fade from old read position to new read position over `min(64, old - new)` samples. This is the standard approach.

**RT-safety:** the recompute is off-RT; it publishes new `outputCompensationSamples` and a target read-cursor offset via the `SolvedLatencyTopology` flip. The audio thread sees the new values on the next block boundary and performs the sample-hold + crossfade or the position-crossfade inline. The crossfade length is bounded at 64 samples, allocation-free, lock-free.

---

## 7. Design — Mute Stability (G4)

Two options. Recommend **B** for v2.

- **A.** Always include all tracks (muted or not) in the `max` calculation. Pro: simplest, stable. Con: muted tracks with unused plugins (e.g. user inserted a high-latency limiter on a muted reference track) inflate compensation for the whole project.
- **B.** Include muted tracks in `max` calculation **only while transport is playing**. Pro: stability during playback (no shifts on mute toggle). Con: stopping → toggling mutes → starting can cause a stop/start delta. Acceptable since transport stops already drop tails.

Implementation: a single bool flag `m_transportLatched` set when `transportPlaying` flips to true and cleared on stop. `calculateLatencyCompensation` reads it (off-RT, racy-read OK — worst case a one-cycle late update).

---

## 8. Design — Buffer Sizing & Diagnostics (G5)

Hybrid inline + heap design:

- Keep `compensationBuffer` fixed at 16384 frames inline for the common path (cache-local, zero heap pressure).
- If `compensationDelay > inline_capacity`, off-RT:
  1. Allocate a per-track `std::vector<float> oversizedCompensationBuffer` sized to **the nearest power-of-two ≥ `delay`**, owned by the inactive slot of the `AudioGraphState` double-buffer.
  2. Emit a one-shot warning into `SolvedLatencyTopology::warnings`, logged once per (channel, generation) by the off-RT consumer.
  3. RT-side `processBlock` selects the oversize buffer when its size is non-zero; otherwise falls back to inline.

**Both code paths must use power-of-two masking arithmetic on the ring index:**

```cpp
// capacity is a power of two (16384 inline; oversize is rounded up to power of 2).
const uint32_t mask = capacity - 1;
const uint32_t writeIdx = writePos & mask;
const uint32_t readIdx  = readPos  & mask;
```

Reasons:
- Eliminates the modulo on the RT path → cheap bit-and instead of integer divide.
- Keeps the branch behavior identical between inline and oversize paths — the compiler emits the same hot loop for both, predictable for the branch predictor, friendly to SIMD widening if we ever vectorize.
- Avoids the subtle off-by-one bugs that arise from non-power-of-two ring buffers under negative deltas.

This is a small constraint with outsized correctness payoff; the inline `std::array<float, 32768>` (16384 frames × 2 channels) is already power-of-two friendly, and the oversize allocator is explicit about rounding up.

---

## 9. Design — Atomic Publish (G8)

Currently `m_graphStates[activeIdx]` is written in place by `calculateLatencyCompensation`. v2 changes this to: **off-RT, write a new `SolvedLatencyTopology` into the inactive slot, then flip the corresponding atomic index**.

Key points:
- `SolvedLatencyTopology` lives in a parallel `std::array<SolvedLatencyTopology, 2>`. **P3 ships with its own atomic index (`m_activeSolvedTopologyIndex`) separate from `m_activeRenderTrackIndex`.** Unifying the two indices (so both swap together via a single atomic store) is deferred to P4+, when the engine reads compensation values from `SolvedLatencyTopology` directly rather than from `TrackRTState`. See r2 note in §0.
- The RT thread reads `SolvedLatencyTopology` strictly read-only. No in-place mutation.
- Per-edge `SendRTState` ring-buffer state (read/write cursors and the inline/oversize buffer) lives in the RT-side slot mirror, not in the solved topology. The solved topology only carries values; the RT mirror carries the running state.
- Generation counter on `SolvedLatencyTopology` lets the RT path detect transitions and trigger the §6 sample-hold/crossfade migration exactly once per generation change.
- **P3 lock-free publish protocol:**
  1. Writer: build new topology in a local; read `prevIdx = m_activeSolvedTopologyIndex.load(relaxed)`; assign topology into slot `1 - prevIdx`; `m_activeSolvedTopologyIndex.store(1 - prevIdx, release)`.
  2. Reader: `idx = m_activeSolvedTopologyIndex.load(acquire)`; read slot `idx`. No locks anywhere.

---

## 10. Tests (G9) — built in lockstep with implementation

All under `@Tests/Headless/`:

1. **`PDCFlatChainTest`** — Two tracks, one with a 256-sample-latency plugin, one without. Render an impulse on each. Assert peak appears at the same project sample in both render outputs. Asserts v1 still works.
2. **`PDCBusChainTest`** — Two tracks, both routed to a bus with a 512-sample-latency plugin; one track also has a local 256-sample plugin. Assert impulses align at master output. Covers G1.
3. **`PDCSidechainAlignmentTest`** — Sidechain feeder track → compressor on a 256-sample-latency-prefixed track. Verify the sidechain triggers exactly at the host's main signal peak, not 256 samples early. Covers G2.
4. **`PDCLatencyChangeMidPlaybackTest`** — Render a sustained tone, change a plugin's reported latency mid-render, assert RMS continuity ±0.5 dB across the change point (no dropout). Covers G3.
5. **`PDCMuteToggleStabilityTest`** — Same scenario as #2 but toggle mute on the high-latency track mid-render. Assert continuity across the toggle. Covers G4.
6. **`PDCOversizeBufferTest`** — Plugin reports 32768-sample latency. Assert (a) warning is emitted once, (b) alignment still correct, (c) RT path remained allocation-free during playback (check telemetry / atomic counter). Covers G5.
7. **`PDCMasterBusReportedLatencyTest`** — Pure read-side: assert `SolvedLatencyTopology::monitoringLatency` includes master FX + reported device output; `projectAlignmentLatency` does not. Covers G6 and the dual-API decision in §13 D5.
8. **`PDCBranchingConvergenceTest`** — *Mandatory stress test for graph-aware solver.* Topology:
   - Track A → Bus 1 → Master
   - Track B → Bus 2 → Master
   - Track C → Bus 1 + Bus 2 simultaneously (two outgoing sends from C)
   - Distinct plugin latencies on A, B, C, Bus 1, Bus 2, Master
   - Render an impulse on each leaf, assert all impulses arrive at the master output at the same project sample.
   - Specifically guards against: duplicated compensation (C delayed twice because it has two sends), branch-local overdelay (Bus 1's latency leaking into C's Bus 2 path), and reconvergence drift (Bus 1 and Bus 2 outputs misaligned at the master mixer).
   - This class of bugs survives ordinary single-branch tests; it is the canonical PDC correctness gate.
9. **`PDCSolverPurityTest`** — Pure logic test against the solver in isolation: feed a hand-crafted `LatencyGraph`, assert exact `SolvedLatencyTopology` output. No engine, no audio. Lets the solver be unit-tested without setting up a render context. Required to keep solver/application separation honest.
10. **`PDCDomainExemptionTest`** — Constructs a `LatencyGraph` with a `Realtime`-domain node, asserts solver emits zero compensation for that branch and that other branches still align correctly. Even though v2 won't expose a UI for this, the solver must already honor it.

These should all be headless-only and registered unconditionally (cheap, fast).

---

## 11. Risk Notes

- **RT-safety surface area.** All recompute work is off-RT. The only RT-side changes are:
  - Read of an additional atomic for transport-latched flag (G4).
  - Optional dereference of `oversizedCompensationBuffer.data()` instead of inline array (G5).
  - Crossfade ramp during recompute transition (G3) — bounded ≤64 samples per change, no allocation, no locks.
- **Serialization impact.** None. PDC state is render-time only; project files don't carry it.
- **Bypass parity.** Per AGENTS.md §11: bypassing a plugin must not shift timing. Today bypass already excludes that plugin's latency from `getTotalLatency()` (`EffectChain.cpp:577`); recompute fires on bypass toggle via the existing callback. v2 preserves this.
- **Mid-playback graph rebuild.** Already handled by double-buffer flip in the engine. v2 piggybacks on the existing flip.
- **Test flakiness.** Impulse-response tests must use bit-exact comparison at known sample positions, no FP tolerance for position, ≤1 LSB tolerance for amplitude.

---

## 12. Implementation Phases

| Phase | Deliverable | Verification |
|-------|-------------|--------------|
| **P0** | This doc, reviewed and approved. | User sign-off (r1 ✅). |
| **P1** | Test scaffolding: `PDCFlatChainTest` against v1 unmodified + `PDCSolverPurityTest` stub asserting the solver-purity contract. | Red→green confirms test infra; v1 must still pass. No production code changed yet. |
| **P2** | Introduce `LatencyGraph` and `SolvedLatencyTopology` types + solver skeleton that exactly reproduces v1 behavior (flat, no graph traversal). Wire engine to consume from `SolvedLatencyTopology`. | `PDCFlatChainTest` green; `PDCSolverPurityTest` green for the flat-equivalent case. v1 semantics preserved. |
| **P3** | G8 — atomic publish via double-buffer flip alongside existing graph state. | All prior tests green; TSan run clean on `ctest`. |
| **P4a** | G1 (solver) — graph-aware DFS, per-edge compensation, three-color cycle detection, audible-source identification. Engine RT path unchanged. | `PDCBusChainTest` + `PDCBranchingConvergenceTest` green at solver level; existing PDC tests still green; reconvergence-arithmetic check inside `PDCBranchingConvergenceTest` confirms all paths from each source arrive at master at the same sample. |
| **P4b** | G1 (engine) — engine consumes `EdgeSolution::compensationSamples` via per-send ring buffers in `processBlock`. Audio-rendering versions of `PDCBusChainTest` and `PDCBranchingConvergenceTest` assert sample-accurate impulse alignment at master output. | Solver-level and audio-rendering versions both green. |
| **P5** | G2 — sidechain compensation (node-boundary). | `PDCSidechainAlignmentTest` green. |
| **P6** | G3 — smooth recompute (sample-hold on increase, crossfade on decrease). | `PDCLatencyChangeMidPlaybackTest` green. |
| **P7** | G4 — transport-latched mute stability. | `PDCMuteToggleStabilityTest` green. |
| **P8** | G5 — oversize buffer path with power-of-two masking on both code paths. | `PDCOversizeBufferTest` green; allocation-counter telemetry shows zero RT-side allocations. |
| **P9** | G6 — dual reported-latency API (`projectAlignmentLatency` + `monitoringLatency`). | `PDCMasterBusReportedLatencyTest` green. |
| **P10** | `LatencyDomain` plumbing (no UI; solver honors it). | `PDCDomainExemptionTest` green. |
| **P11** | (Deferred, separate doc) G7 — tail handling on stop. |

Each phase = one commit + targeted test addition. Each commit must keep `ctest --test-dir build-headless` green. **No phase bundling.** No skipping the solver-purity test in P1 — keeping the solver pure is the architectural commitment that makes everything else cheap.

---

## 13. Resolved Decisions

Reviewer-confirmed answers to the original open questions:

| # | Topic | Decision |
|---|-------|----------|
| **D1** | **G4 — mute policy.** | **Option B** — include muted tracks in the `max` calculation only while transport is playing. Transport stop/start is the synchronization boundary. The stop-then-toggle-then-start delta is acceptable. |
| **D2** | **Sidechain v2 scope.** | **Node-boundary approximation accepted**, but only with explicit code comments at every site marking it as a v2 approximation and pointing at this doc + a tracked PDC-v3 follow-up. Solver also emits an informational warning when the approximation error exceeds a threshold (§5). |
| **D3** | **G5 oversize path.** | **Allow off-RT heap allocation.** Power-of-two ring masking is mandatory for both inline and oversize paths (§8). |
| **D4** | **G7 tail-on-stop.** | **Deferred** to a separate design doc. Tail handling is its own subsystem (transport semantics, offline bounce, loop behavior, plugin suspend policy, silence detection, CPU sleep). Not v2 scope. |
| **D5** | **Reported-latency API.** | **Provide both numbers.** `SolvedLatencyTopology::projectAlignmentLatency` is the inter-track-alignment value (existing semantics of `getMaxProjectLatency`). `SolvedLatencyTopology::monitoringLatency` is the user-visible "monitor-shift" number (includes master FX + device output). Both surfaced through dedicated accessors on `AudioEngine`; the existing `getMaxProjectLatency()` keeps its semantics for source compatibility. |

## 14. Reviewer Architectural Commitments

Folded into r1 of this doc:
- **Solver / application separation is now first-class** (§4.0–§4.3). `LatencyGraph` is the immutable input, `SolvedLatencyTopology` is the immutable output. The RT engine is a pure consumer.
- **Compensation never lives on `AudioRoute`** or any user-visible / serialized structure. It is render-time derived state, in `SendRTState` / `SolvedLatencyTopology` only (§4.1).
- **`LatencyDomain` enum is introduced now** even though v2 only exercises `FullyCompensated` (§4.5). Architectural seam for hardware inserts, low-latency monitoring, frozen paths, anticipative processors.
- **Sample-hold / short ramp on delay-increase** replaces the silence-smear approach (§6).
- **Power-of-two ring masking is mandatory** for both inline and oversize compensation buffers (§8).
- **`PDCBranchingConvergenceTest`** is required to land before P3 (graph-aware solver) is considered green (§10 test #8).

---

*Status: r1 approved. Proceeding to P1 (test scaffolding) on user signal.*
