# Routing Bugs Found — 2026-04-11
Found during surface-level code review of routing stack.

## ALL FIXED — Verified 2026-04-11

## Bug 1: Send gains NOT smoothed ✅ FIXED
**File:** `AestraAudio/src/Core/AudioEngine.cpp` ~line 2055
**Problem:** Main track volume uses `state.gainL`/`state.gainR` smoothed interpolation. Sends apply `send.gain` as raw per-sample multiplier. Zipper noise on send level changes.
**Fix Applied:** Send gains now use `SmoothedParamD` via `state.sendGainL[sendIndex]` / `state.sendGainR[sendIndex]` with `.setTarget()` and `.next()`.

## Bug 2: No cycle detection in routing graph ✅ FIXED
**File:** `AestraAudio/src/Core/AudioEngine.cpp` ~line 1522
**Problem:** Topological sort has no cycle check. Cyclic routes silently produce silence.
**Fix Applied:** `cycleDetected = m_rtProcessOrder.size() != graph.tracks.size()` with warning log. Self-routes also blocked at edge construction.

## Bug 3: Send pan uses different pan law than main pan ✅ FIXED
**File:** `AestraAudio/src/Core/AudioEngine.cpp` ~line 2058-2061
**Problem:** Main pan uses constant-power (`fastPanGainsD`). Sends used `(pan+1) * PI/4` — different curve.
**Fix Applied:** Sends now use `fastPanGainsD()` — same constant-power pan law as main track.

## Bug 4: `std::unordered_map` allocation every callback ✅ FIXED
**File:** `AestraAudio/src/Core/AudioEngine.cpp` ~line 1353
**Problem:** `trackIndexById` heap-allocates every audio callback.
**Fix Applied:** `m_rtTrackIndexById` is a pre-allocated member. `m_rtIndexQueue` replaces `std::queue` with pre-allocated vector.

## Bug 5: Per-sample send iteration ✅ FIXED
**File:** `AestraAudio/src/Core/AudioEngine.cpp` ~line 2048
**Problem:** For every sample, iterates all sends with routing decisions per-sample.
**Fix Applied:** Sends pre-computed as `PreparedSendRoute` structs in a preparation loop, then processed in a batch loop. Routing decisions done once per send.
