# Aestra Comp v2 — Phase 1: Core DSP Rebuild

## Goal
Build the new detection, gain computer, envelope follower, and parameter system
on top of the cleaned-up Phase 0 foundation.

## Dependencies
- Phase 0 complete (double makeup fixed, no hard clamp, zipper smoothing in place)
- AestraEQ.h BiquadFilter class available for sidechain filtering
- IPluginInstance interface unchanged

---

## 1A: Parameter System

### 1.1 Expand Param Enum
Add new parameters to the enum in AestraComp.h:

```cpp
enum Param : uint32_t {
    // Existing (Phase 0)
    kThreshold = 0,
    kRatio,
    kAttack,
    kRelease,
    kMakeup,
    kKnee,
    kMix,
    kBypass,

    // New: Detection
    kDetectorMode,      // 0=Peak, 0.5=RMS, 1.0=Peak/RMS blend

    // New: Topology
    kTopology,          // 0=Feed-forward, 1=Feedback

    // New: Envelope
    kHold,              // 0-1 → 0-50ms hold time
    kAutoRelease,       // 0=Off, 1=On

    // New: Gain Computer
    kRange,             // 0-1 → 0 to -60dB max GR cap

    // New: Lookahead
    kLookahead,         // 0-1 → 0-20ms lookahead

    // New: Stereo
    kStereoLink,        // 0-1 → 0-100% link amount
    kStereoLinkLaw,     // 0=Max, 0.5=Average, 1=Energy

    // New: Sidechain
    kSCHPF,             // 0-1 → 20-500Hz HPF freq
    kSCLPF,             // 0-1 → 1k-20kHz LPF freq
    kSCListen,          // 0=Off, 1=On (audition SC signal)

    // New: Output
    kOutputTrim,        // 0-1 → -24dB to +24dB

    // New: Style
    kStyle,             // 0=Clean, 1=Punch, 2=Glue, 3=Smooth

    // New: Quality
    kQuality,           // 0=Live, 1=Normal, 2=High Quality

    kParamCount
};
```

### 1.2 Param Smoother Class
Create a `ParamSmoother` class for per-parameter exponential smoothing:

```cpp
class ParamSmoother {
public:
    void setTarget(float target);
    float getNext();     // returns next smoothed sample
    void setSmoothingTime(float timeSeconds, double sampleRate);
    void reset(float value);
private:
    float m_current = 0.0f;
    float m_target = 0.0f;
    float m_coeff = 0.0f;  // one-pole coefficient
};
```

Use this for: threshold, ratio, knee, range, makeup, output trim, mix, stereo link.
Attack/release already have their own envelope smoothing.

### 1.3 Update getParameters()
Add all new params to the vector with correct metadata.
Update getParameterDisplay() for all new params.
Update saveState()/loadState() to handle the new param count.
Bump kStateMagic version (0x434D5002 = 'CMP' v2).

---

## 1B: Detector

### 1.4 Peak Detector
Extract the existing peak detection into a reusable method:

```cpp
float detectPeak(float detL, float detR, float& envL, float& envR,
                 float attackCoeff, float releaseCoeff);
```

### 1.5 RMS Detector
Implement windowed RMS detection:

```cpp
class RMSDetector {
public:
    void setWindowSize(uint32_t samples, double sampleRate);
    float process(float sampleL, float sampleR);
private:
    std::vector<float> m_window;
    uint32_t m_writeIndex = 0;
    double m_sumSquares = 0.0;
    uint32_t m_windowSize = 0;
};
```

Use a power-of-2 circular buffer for the window.
Window size linked to attack time (shorter attack = smaller window).

### 1.6 Peak/RMS Blend
```cpp
float blend = getParameter(kDetectorMode); // 0=peak, 0.5=blend, 1=RMS
float peakVal = detectPeak(...);
float rmsVal = detectRMS(...);
float detected = peakVal * (1.0f - blend) + rmsVal * blend;
```

### 1.7 Sidechain Routing
- Internal: use `inputs[0], inputs[1]` (or `inputs[2], inputs[3]` if external SC connected)
- External: use `inputs[2], inputs[3]` (4-channel input)
- SC listen: route detection signal (post-filter) to output instead of processed audio

---

## 1C: Sidechain Filter

### 1.8-1.10 SC HPF + LPF
Reuse `BiquadFilter` from AestraEQ.h and `designBiquad()` cookbook:

```cpp
// In process(), before detection:
BiquadFilter m_scHPF[2]; // L/R
BiquadFilter m_scLPF[2]; // L/R

// Update coefficients when SC freq params change
void updateSCFilters() {
    float hpfFreq = 20.0f + getParameter(kSCHPF) * 480.0f; // 20-500Hz
    float lpfFreq = 1000.0f + getParameter(kSCLPF) * 19000.0f; // 1k-20kHz

    auto hpfCoeffs = designBiquad(FilterType::LowCut, hpfFreq, 0.0f, 0.707f, m_sampleRate);
    auto lpfCoeffs = designBiquad(FilterType::HighCut, lpfFreq, 0.0f, 0.707f, m_sampleRate);

    m_scHPF[0].setCoeffs(hpfCoeffs.b0, hpfCoeffs.b1, hpfCoeffs.b2,
                          hpfCoeffs.a0, hpfCoeffs.a1, hpfCoeffs.a2);
    m_scLPF[0].setCoeffs(lpfCoeffs.b0, lpfCoeffs.b1, lpfCoeffs.b2,
                          lpfCoeffs.a0, lpfCoeffs.a1, lpfCoeffs.a2);
    // Same for [1] (R channel)
}
```

Apply filters to sidechain signal before detection:
```cpp
float scL = m_scHPF[0].process(detL);
scL = m_scLPF[0].process(scL);
// Same for R
```

Note: `designBiquad()` returns FilterCoeffs with a0 already as the normalizer.
The AestraEQ BiquadFilter::setCoeffs() divides by a0, so pass a0=1.0f and
use the already-normalized coefficients directly. Verify the cookbook output
format matches what BiquadFilter expects.

---

## 1D: Gain Computer

### 1.11 Soft/Hard Knee
Replace the existing knee logic with a continuous 0-1 knee parameter:

```cpp
float computeGainReduction(float envDb, float thresholdDb, float ratioVal, float knee) {
    // knee: 0.0 = hard, 1.0 = fully soft (24dB wide)
    float kneeWidth = knee * 24.0f;
    float diff = envDb - thresholdDb;

    if (kneeWidth < 0.01f) {
        // Hard knee
        return (envDb > thresholdDb) ? -(envDb - thresholdDb) * (1.0f - 1.0f / ratioVal) : 0.0f;
    }

    if (std::abs(diff) < kneeWidth * 0.5f) {
        // Soft knee region — quadratic interpolation
        float kneeDiff = diff + kneeWidth * 0.5f;
        return -(1.0f - 1.0f / ratioVal) * kneeDiff * kneeDiff / (2.0f * kneeWidth);
    } else if (envDb > thresholdDb + kneeWidth * 0.5f) {
        // Above knee — full ratio
        float aboveKnee = envDb - thresholdDb - kneeWidth * 0.5f;
        return -aboveKnee * (1.0f - 1.0f / ratioVal) - (1.0f - 1.0f / ratioVal) * kneeWidth * 0.25f;
    }
    return 0.0f; // Below threshold
}
```

### 1.12 Threshold + Ratio
Keep existing dB-domain mapping:
- threshold: -60 to 0 dB
- ratio: 1:1 to 20:1 (with ∞:1 available at ratio = 1.0)

### 1.13 Range / Max GR
```cpp
float rangeDb = -getParameter(kRange) * 60.0f; // 0 to -60dB
float clampedReduction = std::max(reductionDb, rangeDb); // reductionDb is negative
```

### 1.14-1.15 Feed-Forward vs Feedback
```cpp
float detectionSample;
if (topology == FeedForward) {
    detectionSample = inputSample; // pre-gain
} else {
    detectionSample = m_prevOutput[ch]; // post-gain from previous sample
}
// ... compute gain ...
float outputSample = inputSample * gainLinear;
m_prevOutput[ch] = outputSample; // store for next feedback iteration
```

---

## 1E: Envelope Follower

### 1.16 Attack/Release
Keep existing approach but with proper coefficient computation:

```cpp
// Attack: time to reach 63.2% of target (one-pole time constant)
float attackTime = 0.0001f + getParameter(kAttack) * 0.0999f; // 0.1ms - 100ms
float releaseTime = 0.01f + getParameter(kRelease) * 0.99f;   // 10ms - 1000ms

float attackCoeff = std::exp(-1.0f / (m_sampleRate * attackTime));
float releaseCoeff = std::exp(-1.0f / (m_sampleRate * releaseTime));
```

### 1.17 Hold
```cpp
if (m_holdCounter[ch] > 0) {
    m_holdCounter[ch]--;
    // Don't release — hold current GR
    envelopeDb = m_heldLevel[ch];
} else if (detectionDb > envelopeDb) {
    // Attack
    envelopeDb = attackCoeff * envelopeDb + (1.0f - attackCoeff) * detectionDb;
    m_holdCounter[ch] = holdSamples; // reset hold counter
    m_heldLevel[ch] = envelopeDb;
} else {
    // Release
    envelopeDb = releaseCoeff * envelopeDb + (1.0f - releaseCoeff) * detectionDb;
}
```

### 1.18-1.19 Auto Release
```cpp
if (autoRelease > 0.5f) {
    // Program-dependent release: faster for transients, slower for sustained
    float grDepth = std::abs(reductionDb) / 60.0f; // 0-1 normalized
    float autoReleaseTime = 0.05f + grDepth * 0.95f; // 50ms to 1000ms based on GR depth
    releaseCoeff = std::exp(-1.0f / (m_sampleRate * autoReleaseTime));
}
```

---

## 1F: Stereo Link

### 1.20-1.22 Link Laws
```cpp
float linkedGR;
switch (static_cast<int>(getParameter(kStereoLinkLaw) * 2.0f)) {
    case 0: linkedGR = std::max(grL, grR); break;      // Max
    case 1: linkedGR = (grL + grR) * 0.5f; break;      // Average
    case 2: linkedGR = std::sqrt(grL*grL + grR*grR) * 0.707f; break; // Energy (RMS)
}

float linkAmount = getParameter(kStereoLink);
grL = grL * (1.0f - linkAmount) + linkedGR * linkAmount;
grR = grR * (1.0f - linkAmount) + linkedGR * linkAmount;
```

---

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `AestraAudio/include/Plugin/AestraComp.h` | Rewrite | Full v2 DSP implementation |
| `AestraAudio/include/DSP/ParamSmoother.h` | Create | Reusable param smoother class |
| `AestraAudio/include/DSP/RMSDetector.h` | Create | Windowed RMS detector |
| `Tests/AestraAudio/AestraCompPhase1Test.cpp` | Create | Phase 1 verification tests |
| `Tests/CMakeLists.txt` | Modify | Add Phase 1 test target |
| `AestraAudio/CMakeLists.txt` | Modify | Add new DSP headers if needed |

## Phase 1 Exit Criteria
- [ ] All new params defined, exposed, displayable, serializable
- [ ] Peak, RMS, and blend detection working
- [ ] Feed-forward and feedback topologies produce different results
- [ ] Soft knee produces smooth GR curve (no discontinuities)
- [ ] Range limits max GR correctly
- [ ] Hold delays release by correct duration
- [ ] Auto release adapts to GR depth
- [ ] Stereo link with max/average/energy laws works
- [ ] Sidechain HPF/LPF affect detection (testable with SC listen)
- [ ] Param smoothing eliminates zipper noise on all controls
- [ ] State save/load round-trips all new params
- [ ] Builds without warnings

---

## Quality Modes (deferred to Phase 3)
Quality modes will affect:
- Live: no lookahead, reduced smoothing, faster coefficients
- Normal: standard block processing
- High Quality: per-sample envelope, tighter lookahead

These are parameter adjustments, not separate DSP paths. Phase 1 builds the
foundation; Phase 3 adds the mode switching logic.
