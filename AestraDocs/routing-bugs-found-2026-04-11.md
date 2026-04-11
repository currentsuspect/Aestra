# Routing Bugs Found — 2026-04-11
Found during surface-level code review of routing stack.

## Current Status

- Bug 1: fixed
- Bug 2: fixed
- Bug 3: fixed enough for beta
- Bug 4: fixed
- Bug 5: fixed

## Bug 1: Send gains NOT smoothed
**File:** `AestraAudio/src/Core/AudioEngine.cpp`
**Original problem:** Main track volume used smoothed interpolation, but sends applied raw gain changes and could zipper on automation or UI moves.
**Current state:** Fixed. Sends now use per-send smoothed RT gain state in `TrackRTState`, and the engine advances those smoothers during block routing.

## Bug 2: Send pan used a different pan law than main pan
**File:** `AestraAudio/src/Core/AudioEngine.cpp`
**Original problem:** Main pan used `fastPanGainsD(...)`, while send pan used a different curve.
**Current state:** Fixed. Send routing now uses the same `fastPanGainsD(...)` constant-power law as the main track path.

## Bug 3: No cycle detection in routing graph
**File:** `AestraAudio/src/Core/AudioEngine.cpp`
**Original problem:** Cyclic routes could silently degrade into broken or silent paths.
**Current state:** Fixed enough for beta. UI/model routing now blocks self-routes and cycles, and the engine also detects cyclic graphs and logs an explicit warning if one still appears.

## Bug 4: `std::unordered_map` allocation every callback
**File:** `AestraAudio/src/Core/AudioEngine.cpp`
**Original problem:** Routing scratch structures were rebuilt every callback, creating avoidable RT allocation churn.
**Current state:** Fixed. The engine now reuses member-owned scratch containers for track-index maps, routing edges, indegree, eligibility, and process-order state.

## Bug 5: Per-sample send iteration
**File:** `AestraAudio/src/Core/AudioEngine.cpp`
**Original problem:** Every sample iterated every send, mixing routing logic into the hot metering/render path.
**Current state:** Fixed. The main sample loop now handles gain/metering, while send fanout is prepared once per block and applied in dedicated block routing passes. Pre-fader scratch is only copied when needed.

## Notes

- Audible sends now prove themselves on the destination strip VU.
- Sidechain sends stay non-audible and prove themselves through `GR` / `SC live`.
- Offline export parity, route-state roundtrip tests, solo/mute semantics, and illegal-route protection were all verified in this routing tranche.
