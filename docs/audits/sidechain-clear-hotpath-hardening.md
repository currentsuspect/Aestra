# Aestra Sidechain Clear Hotpath Hardening

## 1. Git State
- Starting branch: `perf/sidechain-clear-hotpath`
- Starting SHA: `cab4c181cc5ec5b42d863d9ac8b0efeeb0a17ce6`
- Final branch: `perf/sidechain-clear-hotpath`
- Final SHA: c75f28ac7e4d22e25cb3e418af86fca2595805dd

## 2. Baseline

**Full test suite (before changes):**
```
ctest --output-on-failure -j$(nproc)
Total Test time (real) =  17.54 sec
62/62 passed, 1 skipped (SecPluginScanIsolation)
```

**AestraAudioPerformanceTest (before):**
```
Buffer: 32 frames (666.7 us budget)
Avg Time: 9.1 us
Min Time: 9.0 us
Max Time: 31.0 us
Jitter Range: 22.0 us
-> Stability: ROCK SOLID (Max load 4.7%)
```

**AestraRealtimePathStressTest (before):**
```
localOverruns=0, engineOverruns=0, rtLockViolations=0, rtLogViolations=0
```

## 3. Current Behavior

Before this change, `renderGraph()` unconditionally cleared **every** track's
sidechain buffer every audio block:

```cpp
for (const auto& track : graph.tracks) {
    // ...
    std::memset(buffer.data(), 0, ...);           // main buffer — always needed
    std::memset(sidechainBuffer.data(), 0, ...);  // sidechain — wasteful for non-receivers
}
```

The sidechain buffer (`m_trackSidechainBuffersD`) is only **written to** via
sidechain-only sends (line ~2285: `route.dest = m_trackSidechainBuffersD[scDestSlot].data()`).
It is **read from** during effect chain processing (line ~2142) for every track
that has an active effect chain.

In a typical session with 100+ tracks and only 2–3 sidechain sends, this meant
~97 unnecessary `memset` calls per audio block, each zeroing
`numFrames × 2 × sizeof(double)` bytes (e.g. 8 KiB at 512 frames).

## 4. Change Summary

**Files changed:**
- `AestraAudio/include/Core/AudioEngine.h` — added `m_rtSidechainReceiverFlags` member
- `AestraAudio/src/Core/AudioEngine.cpp` — modified clear loop in `renderGraph()`,
  added pre-allocation in `setBufferConfig()`

**What changed:**
1. Added `std::vector<uint8_t> m_rtSidechainReceiverFlags` (pre-allocated to
   `kMaxTracks` in `setBufferConfig()`). Indexed by `trackIndex`. Value is `1`
   if the track received sidechain input in the previous block, `0` otherwise.

2. Modified the per-block clear loop in `renderGraph()` to skip the sidechain
   buffer `memset` for tracks that:
   - Do NOT receive sidechain input this block (`m_rtSidechainIncoming[gi].empty()`), AND
   - Did NOT receive sidechain input last block (`m_rtSidechainReceiverFlags[trackIdx] == 0`)

3. After the conditional clear, updates `m_rtSidechainReceiverFlags[trackIdx]`
   for the next block based on whether the track receives sidechain this block.

## 5. Correctness Protection

**Why stale sidechain data cannot leak:**

| Scenario | `receivesThisBlock` | `receivedLastBlock` | Clear? | Correct? |
|---|---|---|---|---|
| Track receives sidechain this block | true | any | YES | ✓ Must clear before `+=` routing |
| Track received sidechain last block, not this | false | true | YES | ✓ Stale data removed |
| Track never receives sidechain | false | false | SKIP | ✓ Buffer stays at 0 |
| First block after engine init | false | false | SKIP | ✓ Buffer initialized to 0 in `setBufferConfig()` |
| Send muted between blocks | false | true | YES | ✓ Stale data from previous block cleared |

The two-flag guard (`receivesThisBlock || receivedLastBlock`) ensures:
- **Routing accumulation safety**: Any track that receives sidechain routing this
  block has its buffer cleared before the `+=` accumulation pass.
- **Stale data safety**: Any track that had sidechain data written last block has
  its buffer cleared even if routing changed (e.g., send muted/removed).
- **Zero-init safety**: Tracks that never participate in sidechain routing have
  buffers that remain at their initialization value (0.0) from `setBufferConfig()`.

## 6. RT Safety

**No allocations, locks, logging, or blocking work added to the audio thread:**

- `m_rtSidechainReceiverFlags` is pre-allocated to `kMaxTracks` bytes in
  `setBufferConfig()` (called from the non-RT thread during configuration).
- The RT path only performs:
  - `uint8_t` reads from `m_rtSidechainReceiverFlags[trackIdx]` (cache-hot,
    co-located with other per-track data)
  - `uint8_t` writes to `m_rtSidechainReceiverFlags[trackIdx]`
  - `.empty()` on `m_rtSidechainIncoming[gi]` (reads pre-allocated vector size)
  - `std::memset` (same as before, just conditional)
- No `push_back`, `resize`, `reserve`, `new`, `delete`, `std::mutex`,
  `std::lock_guard`, or logging calls in the modified path.

## 7. After Results

**Full test suite (after changes):**
```
ctest --output-on-failure -j$(nproc)
Total Test time (real) =  18.15 sec
62/62 passed, 1 skipped (SecPluginScanIsolation)
```

**AestraAudioPerformanceTest (after):**
```
Buffer: 32 frames (666.7 us budget)
Avg Time: 9.1 us
Min Time: 9.0 us
Max Time: 27.0 us
Jitter Range: 18.0 us
-> Stability: ROCK SOLID (Max load 4.0%)
```

**AestraRealtimePathStressTest (after):**
```
localOverruns=0, engineOverruns=0, rtLockViolations=0, rtLogViolations=0
```

**Performance note**: The jitter range improved from 22.0 µs to 18.0 µs and max
load dropped from 4.7% to 4.0%. These improvements are within normal variance
for a single-run measurement on a shared system. The real benefit is reduced
memory bandwidth pressure on sessions with many tracks and few sidechain sends,
which would be more visible under sustained load with higher track counts.

## 8. Remaining Risk

- **Low risk**: The `m_rtSidechainReceiverFlags` vector adds 4 KiB
  (`kMaxTracks = 4096` × 1 byte) to the AudioEngine heap footprint. This is
  negligible compared to the existing per-track double-precision buffers.
- **Low risk**: If a future code path writes to a track's sidechain buffer
  outside the normal routing mechanism, the stale-data tracking would not
  account for it. The current codebase has exactly one write path (the routing
  accumulation loop), so this is not a current concern.
- **No risk**: The optimization is behavior-preserving. The sidechain buffer
  state for every track at every effect chain processing point is identical
  to the pre-optimization behavior.

## 9. Recommended Next Session

**Next surgical target**: The topological sort in `renderGraph()` rebuilds
`m_rtTopoIndegree`, `m_rtTopoEdges`, and `m_rtProcessOrder` every block even
when the graph topology hasn't changed. A "dirty topology" flag set by the
graph compile path could skip the entire topo sort when routing is unchanged,
saving O(tracks × edges) work per block.
