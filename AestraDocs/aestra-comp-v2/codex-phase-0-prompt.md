# Codex Handoff: Aestra Comp v2 — Phase 0

You are working on Aestra Comp, a dynamics compressor plugin for the Aestra DAW.
Phase 0 is cleanup — fix correctness bugs, no new features.

## Context

Aestra is a C++17 DAW. The compressor is a single-header plugin at:
`AestraAudio/include/Plugin/AestraComp.h` (263 lines)

It implements `IPluginInstance` from `AestraAudio/include/Plugin/PluginHost.h`.
It has 8 parameters (threshold, ratio, attack, release, makeup, knee, mix, bypass).
It supports stereo processing with optional sidechain (4-channel input: 2 audio + 2 SC).

## Bugs to Fix (in order)

### BUG-1: Double Makeup Gain (CRITICAL)
The makeup gain is applied twice in the output formula:

```cpp
// Line 96: makeupLinear = pow(10, makeupGain / 20)  — converts dB to linear
// Line 146: gainDb = reductionDb + makeupGain  — makeup baked into gain dB
// Line 147: gain = pow(10, gainDb / 20)  — gain includes makeup
// Line 151: outL = inL * (1 + (gain * makeupLinear - 1) * wetMix)  — makeupLinear applied AGAIN
```

**Fix:** Remove makeupGain from gainDb. Keep gain computation pure (reduction only).
Apply makeup once at the output stage.

### BUG-2: Hard Output Clamp (lines 155-156)
Remove `std::clamp(outL, -1.0f, 1.0f)` and `std::clamp(outR, -1.0f, 1.0f)`.
Output should not be clamped — the host handles headroom.

### BUG-3: Dead Lookahead State (line 253)
Remove `uint32_t m_lookaheadDelay = 0` — it's declared but never used.

### BUG-4: Empty updateConstants() (line 248)
Remove `void updateConstants() {}` — it's called in initialize() but does nothing.

### BUG-5: Parameter Smoothing (lines 94-95)
Attack/release coefficients are computed per-block from raw atomic values.
Changing attack during playback causes zipper noise.

**Fix:** Add one-pole smoothing to attack and release coefficients:
```cpp
// After computing raw attackCoeff/releaseCoeff:
const float smoothRate = 0.01f; // ~10ms smoothing
m_attackCoeffSmoothed += (attackCoeff - m_attackCoeffSmoothed) * smoothRate;
m_releaseCoeffSmoothed += (releaseCoeff - m_releaseCoeffSmoothed) * smoothRate;
```

Add `float m_attackCoeffSmoothed = 0.999f` and `float m_releaseCoeffSmoothed = 0.999f` as members.

## Constraints

- C++17, no external dependencies beyond what's already included
- Must pass the test at `Tests/AestraAudio/AestraCompPhase0Test.cpp`
- No new parameters — keep the existing 8
- No feature additions — this is bug fixes only
- The test file has a typo on line 101: `comp.setInput` should be `comp.setParameter` — the test file has already been fixed, but if you see `setInput` anywhere, it's wrong
- Follow existing code style (4-space indent, PascalCase classes, camelCase methods)
- Keep it in a single header file — don't split into .cpp

## Verification

After your changes, the test `AestraCompPhase0Test` should pass all 6 checks:
1. Makeup gain applied exactly once (V-1)
2. No hard output clamp (V-2)
3. No zipper noise during automation (V-3)
4. Bypass transparency (V-4)
5. Metadata consistency (V-5)
6. Gain reduction metering works (V-6)

Build with:
```bash
cmake -S . -B build -DAESTRA_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target AestraCompPhase0Test
./build/bin/AestraCompPhase0Test
```

## What NOT to Change

- IPluginInstance interface (PluginHost.h)
- PluginFactory.cpp or BuiltInPlugins.cpp (no metadata changes)
- BiquadFilter in AestraEQ.h (reuse in Phase 1, not now)
- Test file (Tests/AestraAudio/AestraCompPhase0Test.cpp)
- CMakeLists.txt (test target already added)

## After Phase 0

Phase 1 will add: new parameters (detection mode, topology, hold, auto release,
range, lookahead, stereo link, sidechain filters, output trim, style, quality),
a ParamSmoother class, an RMSDetector class, and the BiquadFilter integration.

But first, Phase 0 must be solid. No new features on a broken foundation.
