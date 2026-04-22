# Aestra Comp v2 — Phase 0: Foundation & Cleanup

## Goal
Fix all release blockers in the current AestraComp before building v2 features.
This phase makes the existing compressor correct, not broken. No new features.

## Source Files

| File | Purpose |
|------|---------|
| `AestraAudio/include/Plugin/AestraComp.h` | Main compressor (263 lines, single header) |
| `AestraAudio/src/Plugin/BuiltInPlugins.cpp` | Plugin metadata registration |
| `AestraAudio/src/Plugin/PluginFactory.cpp` | Plugin instantiation |
| `AestraAudio/include/Plugin/PluginHost.h` | IPluginInstance interface |
| `AestraAudio/include/Plugin/AestraEQ.h` | Contains BiquadFilter class (reusable for SC filters) |
| `AestraAudio/include/DSP/ContinuousParamBuffer.h` | Existing param buffer pattern (reference for smoothing) |

## Current Signal Flow (from AestraComp.h)

```
Input → abs(detL,detR) → max(levelL, levelR) → log10 → dB
→ envelope follower (attack/release coeff smoothing)
→ gain computer (threshold/ratio/knee)
→ gain = pow(10, (reductionDb + makeupGain) / 20)
→ out = in * (1 + (gain * makeupLinear - 1) * mix)    ← BUG: makeup applied twice
→ clamp(out, -1, 1)                                     ← BUG: hard clip
→ Output
```

## Bugs to Fix

### BUG-1: Double Makeup Gain
**Location:** AestraComp.h lines 96, 146, 147, 151-152

```cpp
// Line 96: makeupLinear converts makeup from dB to linear
const float makeupLinear = std::pow(10.0f, makeupGain / 20.0f);

// Line 146: gainDb already includes makeupGain
float gainDb = reductionDb + makeupGain;

// Line 147: gain is linear, already includes makeup
float gain = std::pow(10.0f, gainDb / 20.0f);

// Line 151-152: wet/dry formula applies makeupLinear AGAIN
float outL = inL * (1.0f + (gain * makeupLinear - 1.0f) * wetMix);
float outR = inR * (1.0f + (gain * makeupLinear - 1.0f) * wetMix);
```

**Fix:** Remove `makeupGain` from `gainDb` OR remove `makeupLinear` from the output formula.

Recommended fix (cleanest separation of concerns):
```cpp
// gainDb = pure gain reduction only
float gainDb = reductionDb;  // remove makeupGain from here
float gain = std::pow(10.0f, gainDb / 20.0f);

// makeup applied once at output
float outL = inL * (1.0f + (gain - 1.0f) * wetMix) * makeupLinear;
float outR = inR * (1.0f + (gain - 1.0f) * wetMix) * makeupLinear;
```

This keeps gain computation (ratio/knee) separate from output staging (makeup/mix).

### BUG-2: Hard Output Clamp
**Location:** AestraComp.h lines 155-156

```cpp
outL = std::clamp(outL, -1.0f, 1.0f);
outR = std::clamp(outR, -1.0f, 1.0f);
```

**Fix:** Remove both lines entirely. Output should not be clamped in normal operation.
A soft clipper on the output is a v2 nice-to-have, not a default behavior.

### BUG-3: Dead Lookahead State
**Location:** AestraComp.h line 253

```cpp
uint32_t m_lookaheadDelay = 0;
```

**Fix:** Remove `m_lookaheadDelay` member. Real lookahead will be added in Phase 2
with a proper circular buffer. Dead state causes confusion.

### BUG-4: Empty updateConstants()
**Location:** AestraComp.h line 248

```cpp
void updateConstants() {}
```

**Fix:** Remove this method. It's called in `initialize()` but does nothing.
If v2 needs precomputed constants, a new method with a clear name will be added.

### BUG-5: Stereo Always Linked
**Location:** AestraComp.h line 123

```cpp
envR = envL; // linked envelope for stereo coherence
```

**Fix:** For v1 cleanup, this is acceptable behavior (linked stereo is the default).
But document it clearly. Stereo link control (0-100%) is a Phase 1 feature.

### BUG-6: Parameter IDs Mismatch Risk
**Location:** AestraComp.h param enum vs getParameters() vector

The enum declares:
```cpp
enum Param : uint32_t {
    kThreshold = 0, kRatio, kAttack, kRelease, kMakeup, kKnee, kMix, kBypass,
    kParamCount
};
```

The `getParameters()` vector returns them in the same order with matching IDs.
Currently this is consistent. **No fix needed**, but verify after any enum changes.

### BUG-7: No Parameter Smoothing
**Location:** AestraComp.h lines 94-95 (attack/release coefficients)

```cpp
const float attackCoeff = std::exp(-1.0f / (m_sampleRate * attackTime));
const float releaseCoeff = std::exp(-1.0f / (m_sampleRate * releaseTime));
```

These are recomputed every block from raw atomic params. Changing attack/release
during playback will cause zipper noise.

**Fix for Phase 0:** Add one-pole smoothing to attack and release coefficients.
Not full param smoothing (that's Phase 1), but enough to prevent audible artifacts.

```cpp
// In process(), after computing raw coefficients:
const float smoothRate = 0.01f; // ~10ms smoothing at 48kHz block
m_attackCoeffSmoothed += (attackCoeff - m_attackCoeffSmoothed) * smoothRate;
m_releaseCoeffSmoothed += (releaseCoeff - m_releaseCoeffSmoothed) * smoothRate;
```

### BUG-8: Gain Jump on Ratio Change
**Location:** AestraComp.h line 87

```cpp
const float ratioVal = 1.0f + ratio * 19.0f;
```

When ratio changes, the gain computer output changes instantly.
Combined with no param smoothing, this causes audible gain jumps.

**Fix for Phase 0:** Apply gain change through a simple smoother.
The envelope follower already smooths the level, but the ratio itself
changes the transfer function instantaneously.

```cpp
// Smooth the ratio value itself
const float rawRatioVal = 1.0f + ratio * 19.0f;
m_ratioSmoothed += (rawRatioVal - m_ratioSmoothed) * 0.01f;
```

## Verification Tests

### V-1: Sine Wave Makeup Test
- Input: 1kHz sine at -12dB
- Settings: threshold -20dB, ratio 4:1, makeup +6dB
- Expected: output at approximately -12 + 6 = -6dB (with some compression)
- **Before fix:** output would be -12 + 6 + 6 = ~0dB (double makeup)
- **After fix:** output should match expected

### V-2: No Hard Clipping
- Input: 1kHz sine at 0dB
- Settings: threshold -40dB, ratio 20:1, makeup +24dB
- Expected: output can exceed 0dB (host handles clipping)
- **Before fix:** output hard-clamped at 0dB
- **After fix:** output goes above 0dB (float32 headroom)

### V-3: No Zipper Noise
- Automate attack from 0.1ms to 100ms over 1 second on a sustained signal
- Listen for clicks/pops
- **Before fix:** audible zipper noise
- **After fix:** smooth transition

### V-4: Bypass Transparent
- Bypass on vs off (with mix 100% and no compression)
- Should be sample-accurate passthrough
- No DC offset, no gain change

### V-5: Metadata Consistency
- `getParameters()` IDs match enum values
- `getParameterDisplay()` covers all params
- `saveState()`/`loadState()` round-trips correctly

## Phase 0 Exit Criteria
- [ ] Makeup gain applied exactly once
- [ ] No output hard clamp
- [ ] Dead lookahead state removed
- [ ] Empty updateConstants() removed
- [ ] Attack/release coefficient smoothing prevents zipper noise
- [ ] All V-1 through V-5 tests pass
- [ ] Plugin builds without warnings
- [ ] No change to feature set (cleanup only)
