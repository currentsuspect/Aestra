# Codex Handoff: Aestra Comp v2 — Phase 1

You are building the v2 core DSP for AestraComp, a dynamics compressor plugin
for the Aestra DAW. This is a full rewrite of the DSP while keeping the same
file structure (single header).

## Context

Aestra is a C++17 DAW. The compressor is at:
  `AestraAudio/include/Plugin/AestraComp.h`

It implements `IPluginInstance` from `AestraAudio/include/Plugin/PluginHost.h`.
Phase 0 is complete — double makeup gain fixed, hard clamp removed, zipper
smoothing added. You're building on that clean foundation.

The test for Phase 1 is at:
  `Tests/AestraAudio/AestraCompPhase1Test.cpp`

**Do NOT modify the test file.** Your job is to make AestraComp.h pass all 10 tests.

## Reference Files

Read these before starting:

1. `/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/AestraComp.h` — current source (Phase 0 state)
2. `/home/currentsuspect/Dev/Aestra/Tests/AestraAudio/AestraCompPhase1Test.cpp` — test expectations
3. `/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/AestraEQ.h` — contains `BiquadFilter` class and `designBiquad()` cookbook (reuse for SC filters)
4. `/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/PluginHost.h` — `IPluginInstance` interface, `PluginParameter` struct
5. `/home/currentsuspect/Dev/Aestra/AestraDocs/aestra-comp-v2/phase-1-core-dsp.md` — full Phase 1 spec

## What to Build

### New Parameters (add to enum)

```cpp
enum Param : uint32_t {
    kThreshold = 0, kRatio, kAttack, kRelease, kMakeup, kKnee, kMix, kBypass,
    kDetectorMode,    // 0=Peak, 1=RMS (continuous blend)
    kTopology,        // 0=Feed-forward, 1=Feedback
    kHold,            // 0-1 → 0-50ms hold time
    kAutoRelease,     // 0=Off, 1=On
    kRange,           // 0-1 → 0 to -60dB max GR cap
    kLookahead,       // 0-1 → 0-20ms (stub for Phase 2, must be gettable/settable)
    kStereoLink,      // 0-1 → 0-100%
    kStereoLinkLaw,   // 0=Max, 0.5=Average, 1=Energy
    kSCHPF,           // 0-1 → 20-500Hz
    kSCLPF,           // 0-1 → 1k-20kHz
    kSCListen,        // 0=Off, 1=On
    kOutputTrim,      // 0-1 → -24dB to +24dB
    kStyle,           // 0=Clean, 1=Punch, 2=Glue, 3=Smooth
    kQuality,         // 0=Live, 1=Normal, 2=High Quality
    kParamCount
};
```

### Detection Modes

- **Peak** (detectorMode=0): `abs()` per-sample, max of L/R
- **RMS** (detectorMode=1): windowed RMS. Use a simple circular buffer approach.
  Window size linked to attack time. For efficiency, use a power-of-2 buffer with
  running sum of squares.
- **Blend** (detectorMode=0.5): interpolate between peak and RMS values

### Topology

- **Feed-forward** (topology=0): detection from input signal (current behavior)
- **Feedback** (topology=1): detection from previous output. Store `m_prevOutput[2]`.
  The detection signal comes from the *previous sample's* output, not current input.

### Envelope

- Keep existing attack/release coefficient smoothing from Phase 0
- **Hold**: after a GR increase triggers attack, hold the envelope for N ms before
  releasing. Use a per-channel sample counter.
- **Auto release**: when enabled, scale release time based on GR depth.
  More compression → longer release. Formula: `releaseTime = 50ms + grDepth * 950ms`
  where grDepth = abs(reductionDb) / 60.

### Gain Computer

- **Range**: cap max GR. `rangeDb = -getParam(kRange) * 60`. After computing
  reductionDb, clamp: `reductionDb = std::max(reductionDb, rangeDb)` (reductionDb is negative)
- **Knee**: keep existing soft/hard knee logic. The knee param is 0-1, mapped to 0-24dB width.

### Sidechain Filter

Reuse `BiquadFilter` and `designBiquad()` from AestraEQ.h. The EQ header defines:
- `FilterType::LowCut` for HPF
- `FilterType::HighCut` for LPF
- `designBiquad(type, freq, gainDb, q, sampleRate)` returns `FilterCoeffs{b0,b1,b2,a0,a1,a2}`
- `BiquadFilter::setCoeffs(b0, b1, b2, a0, a1, a2)` normalizes by a0 internally

**Important:** `designBiquad()` returns unnormalized coefficients where a0 is the
normalizer. `BiquadFilter::setCoeffs()` divides by a0 internally. So pass the
raw coefficients directly.

Set up 2 HPF + 2 LPF (L/R). Apply to sidechain signal before detection.
Update coefficients when kSCHPF or kSCLPF change (check every block, only
recompute when values change).

HPF freq: `20.0f + kSCHPF * 480.0f` (20-500Hz)
LPF freq: `1000.0f + kSCLPF * 19000.0f` (1k-20kHz)

### Stereo Link

When `kStereoLink > 0`, compute GR for each channel independently, then blend:
```cpp
float linkedGR;
switch (linkLaw) {
    case 0: linkedGR = std::max(grL, grR); break;
    case 1: linkedGR = (grL + grR) * 0.5f; break;
    case 2: linkedGR = std::sqrt(grL*grL + grR*grR) * 0.707f; break;
}
grL = grL * (1-link) + linkedGR * link;
grR = grR * (1-link) + linkedGR * link;
```

When `kStereoLink = 0`: dual mono (each channel independent).
When `kStereoLink = 1`: fully linked (current behavior).

### Output

- **Makeup gain**: apply once at output (Phase 0 fix, keep it)
- **Output trim**: additional ±24dB gain at output. `trimDb = -24 + kOutputTrim * 48`.
  Apply after makeup.
- **Mix**: wet/dry with dry path passthrough (existing behavior)

### State Serialization

- Bump `kStateMagic` to `0x434D5002` ('CMP' v2)
- saveState/loadState must handle all kParamCount params
- loadState should accept v1 states (magic 0x434D5001) — load the first 8 params, zero the rest

### getParameters() and getParameterDisplay()

Every param in the enum must have:
- Correct ID matching enum value
- Human-readable name and short name
- Unit string
- Default value, min, max
- isAutomatable flag

Every param must have a display string in getParameterDisplay().

### Process Loop Structure

The per-sample loop should be restructured as:

```
for each sample:
  1. Get input samples (inL, inR)
  2. Get sidechain samples (detL, detR) — from SC input or internal
  3. Apply SC filters to detL, detR
  4. Detection: compute level from detL, detR (peak/RMS/blend)
  5. Envelope: attack/release/hold smoothing
  6. Gain computer: threshold/ratio/knee/range → reductionDb
  7. Stereo link: blend L/R GR based on link amount and law
  8. Apply gain to input: outL = inL * gainLinear, outR = inR * gainLinear
  9. Store output for feedback topology
  10. Wet/dry mix
  11. Apply makeup + output trim
  12. Write to output buffers
```

### SC Listen Mode

When `kSCListen > 0.5`, output the filtered sidechain signal instead of
the processed audio. This is for auditioning what the detector hears.

## Constraints

- C++17, no external deps beyond what's included
- Must pass all 10 tests in AestraCompPhase1Test.cpp
- Keep it in a single header file (AestraComp.h)
- Do NOT modify: PluginHost.h, PluginFactory.cpp, BuiltInPlugins.cpp, AestraEQ.h,
  the test file, CMakeLists.txt
- Follow existing code style (4-space indent, PascalCase classes, camelCase methods)
- All params must be atomic for thread safety (UI writes, audio reads)
- No blocking calls, no allocations in process()

## Build & Test

```bash
cmake -S /home/currentsuspect/Dev/Aestra -B /home/currentsuspect/Dev/Aestra/build \
  -DAESTRA_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /home/currentsuspect/Dev/Aestra/build --target AestraCompPhase1Test
/home/currentsuspect/Dev/Aestra/build/bin/AestraCompPhase1Test
```

All 10 tests must pass. If any fail, read the error output, fix the issue,
rebuild, and retest.

## Test Summary

| Test | What it checks |
|------|---------------|
| T1 | New params exist, have valid metadata |
| T2 | State save/load round-trips all params |
| T3 | Peak vs RMS detection produce different GR |
| T4 | Feed-forward vs feedback produce different output |
| T5 | Range limits max gain reduction |
| T6 | Sidechain HPF reduces GR on low-frequency content |
| T7 | Stereo link: linked L/R similar, unlinked L/R differ |
| T8 | Output trim applies correct dB gain |
| T9 | No zipper noise when ramping multiple params |
| T10 | Bypass still works (passthrough) |
