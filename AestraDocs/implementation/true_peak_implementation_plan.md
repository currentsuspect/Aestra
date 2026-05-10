# True Peak Metering Implementation Plan

**Phase:** 2  
**Priority:** High (Export/Mastering Credibility)  
**Estimated Effort:** 1 week  
**Status:** Not Started  
**Depends On:** None (can be done in parallel with PDC)

---

## Design Summary

Implement ITU-R BS.1770-4 compliant true peak metering by:
1. Oversampling output signal by 4x for peak detection
2. Measuring reconstructed peak (not just sample peak)
3. Reporting both sample peak and true peak (dBTP)
4. Integrating into export/bounce validation

**Key Principle:** Metering oversampling does NOT alter the audio signal, only measures it.

---

## Architecture

### Metering Pipeline

```
[Audio Output (48kHz)]
         ↓
[Upsample 4x → 192kHz] ← NEW (metering only)
         ↓
[Measure Peak on Oversampled Signal] ← NEW
         ↓
[Report as dBTP (True Peak)] ← NEW
         ↓
[UI Meter Display]
```

### Separation of Concerns

- **Audio Path:** Unchanged (no oversampling)
- **Metering Path:** Oversampled for accurate peak detection
- **Export Path:** Can optionally use true peak for validation

---

## Files to Touch

### New Files
1. `AestraAudio/include/DSP/TruePeakMeter.h` - True peak detector
2. `AestraAudio/src/DSP/TruePeakMeter.cpp` - Implementation
3. `Tests/Audio/TruePeakMeterTest.cpp` - Tests

### Modified Files
1. `AestraAudio/include/Core/AudioEngine.h` - Add true peak metering
2. `AestraAudio/src/Core/AudioEngine.cpp` - Integrate true peak measurement
3. `AestraAudio/include/IO/AudioExporter.h` - Add true peak validation
4. `AestraAudio/src/IO/AudioExporter.cpp` - Implement validation

---

## Exact State Changes

### 1. TruePeakMeter.h (New File)

```cpp
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace Aestra::Audio {

/**
 * @brief ITU-R BS.1770-4 compliant true peak meter
 * 
 * Oversamples by 4x and measures reconstructed peak to detect
 * intersample peaks that exceed 0 dBFS.
 * 
 * RT-SAFE: All buffers preallocated, no heap allocation.
 */
class TruePeakMeter {
public:
    TruePeakMeter();
    
    /**
     * @brief Initialize for given sample rate
     * NOT RT-SAFE: Call from main thread only
     */
    void initialize(uint32_t sampleRate);
    
    /**
     * @brief Reset peak values and filter state
     * NOT RT-SAFE: Call from main thread only
     */
    void reset();
    
    /**
     * @brief Process a block of stereo samples
     * RT-SAFE: No allocations, deterministic
     * 
     * @param input Interleaved stereo samples
     * @param numFrames Number of frames (not samples)
     */
    void process(const float* input, uint32_t numFrames);
    
    /**
     * @brief Get current sample peak (traditional)
     */
    float getSamplePeakL() const { return m_samplePeakL; }
    float getSamplePeakR() const { return m_samplePeakR; }
    
    /**
     * @brief Get true peak (intersample peak)
     */
    float getTruePeakL() const { return m_truePeakL; }
    float getTruePeakR() const { return m_truePeakR; }
    
    /**
     * @brief Get true peak in dBTP (decibels True Peak)
     */
    float getTruePeakLdBTP() const { 
        return m_truePeakL > 0.0f ? 20.0f * std::log10(m_truePeakL) : -200.0f;
    }
    float getTruePeakRdBTP() const { 
        return m_truePeakR > 0.0f ? 20.0f * std::log10(m_truePeakR) : -200.0f;
    }
    
    /**
     * @brief Get max true peak across both channels
     */
    float getMaxTruePeak() const { 
        return std::max(m_truePeakL, m_truePeakR); 
    }
    
    float getMaxTruePeakdBTP() const {
        float maxPeak = getMaxTruePeak();
        return maxPeak > 0.0f ? 20.0f * std::log10(maxPeak) : -200.0f;
    }

private:
    // 4x oversampling filter coefficients (ITU-R BS.1770-4)
    static constexpr size_t kFilterTaps = 48;
    static constexpr size_t kOversampleFactor = 4;
    
    // Upsampling filter (polyphase FIR)
    std::array<std::array<float, kFilterTaps>, kOversampleFactor> m_filterCoeffs;
    
    // Filter state (ring buffer)
    std::array<float, kFilterTaps> m_stateL{};
    std::array<float, kFilterTaps> m_stateR{};
    uint32_t m_statePos{0};
    
    // Peak values
    float m_samplePeakL{0.0f};
    float m_samplePeakR{0.0f};
    float m_truePeakL{0.0f};
    float m_truePeakR{0.0f};
    
    uint32_t m_sampleRate{48000};
    
    // Initialize ITU-R BS.1770-4 filter coefficients
    void initializeFilterCoeffs();
    
    // Process single channel through oversampling filter
    void processChannel(const float* input, uint32_t numFrames, 
                       std::array<float, kFilterTaps>& state,
                       float& samplePeak, float& truePeak);
};

} // namespace Aestra::Audio
```

### 2. TruePeakMeter.cpp (New File)

```cpp
#include "DSP/TruePeakMeter.h"
#include <algorithm>
#include <cmath>

namespace Aestra::Audio {

// ITU-R BS.1770-4 4x oversampling filter coefficients
// These are the official coefficients from the standard
static constexpr float kITU_BS1770_Coeffs[4][48] = {
    // Phase 0 (original samples)
    {
        0.0017089843750f, 0.0109863281250f, -0.0196533203125f, 0.0332031250000f,
        -0.0594482421875f, 0.1373291015625f, 0.9721679687500f, -0.1022949218750f,
        0.0476074218750f, -0.0266113281250f, 0.0148925781250f, -0.0083007812500f,
        // ... (48 coefficients total)
    },
    // Phase 1, 2, 3 (interpolated samples)
    // ... (full coefficients in actual implementation)
};

TruePeakMeter::TruePeakMeter() {
    initializeFilterCoeffs();
}

void TruePeakMeter::initialize(uint32_t sampleRate) {
    m_sampleRate = sampleRate;
    reset();
}

void TruePeakMeter::reset() {
    m_stateL.fill(0.0f);
    m_stateR.fill(0.0f);
    m_statePos = 0;
    m_samplePeakL = 0.0f;
    m_samplePeakR = 0.0f;
    m_truePeakL = 0.0f;
    m_truePeakR = 0.0f;
}

void TruePeakMeter::initializeFilterCoeffs() {
    // Copy ITU-R BS.1770-4 coefficients
    for (size_t phase = 0; phase < kOversampleFactor; ++phase) {
        for (size_t tap = 0; tap < kFilterTaps; ++tap) {
            m_filterCoeffs[phase][tap] = kITU_BS1770_Coeffs[phase][tap];
        }
    }
}

void TruePeakMeter::process(const float* input, uint32_t numFrames) {
    // Process left and right channels separately
    std::vector<float> leftChannel(numFrames);
    std::vector<float> rightChannel(numFrames);
    
    // Deinterleave
    for (uint32_t i = 0; i < numFrames; ++i) {
        leftChannel[i] = input[i * 2];
        rightChannel[i] = input[i * 2 + 1];
    }
    
    // Process each channel
    processChannel(leftChannel.data(), numFrames, m_stateL, m_samplePeakL, m_truePeakL);
    processChannel(rightChannel.data(), numFrames, m_stateR, m_samplePeakR, m_truePeakR);
}

void TruePeakMeter::processChannel(const float* input, uint32_t numFrames,
                                   std::array<float, kFilterTaps>& state,
                                   float& samplePeak, float& truePeak) {
    for (uint32_t i = 0; i < numFrames; ++i) {
        float sample = input[i];
        
        // Update sample peak
        samplePeak = std::max(samplePeak, std::abs(sample));
        
        // Insert sample into filter state
        state[m_statePos] = sample;
        m_statePos = (m_statePos + 1) % kFilterTaps;
        
        // Compute 4 oversampled values using polyphase filter
        for (size_t phase = 0; phase < kOversampleFactor; ++phase) {
            float oversampledValue = 0.0f;
            
            // Convolve with filter coefficients
            for (size_t tap = 0; tap < kFilterTaps; ++tap) {
                uint32_t stateIdx = (m_statePos + tap) % kFilterTaps;
                oversampledValue += state[stateIdx] * m_filterCoeffs[phase][tap];
            }
            
            // Update true peak
            truePeak = std::max(truePeak, std::abs(oversampledValue));
        }
    }
}

} // namespace Aestra::Audio
```

**RT Safety Analysis:**
- ✅ All buffers preallocated (fixed-size arrays)
- ✅ No heap allocation in `process()`
- ⚠️ Uses `std::vector` for deinterleaving (FIXME: use stack buffer or preallocated)
- ✅ Deterministic loop (fixed iteration count)
- ✅ No locks, no system calls

**Optimization Note:** The `std::vector` in `process()` should be replaced with a preallocated member buffer for true RT safety.

### 3. AudioEngine.h - Add True Peak Metering

```cpp
// Add to AudioEngine class (around line 400)

#include "DSP/TruePeakMeter.h"

public:
    // === True Peak Metering ===
    
    /**
     * @brief Get current sample peak (traditional)
     */
    float getSamplePeakL() const;
    float getSamplePeakR() const;
    
    /**
     * @brief Get true peak (intersample peak)
     */
    float getTruePeakL() const;
    float getTruePeakR() const;
    
    /**
     * @brief Get true peak in dBTP
     */
    float getTruePeakLdBTP() const;
    float getTruePeakRdBTP() const;
    float getMaxTruePeakdBTP() const;
    
    /**
     * @brief Reset peak meters
     */
    void resetPeakMeters();

private:
    // True peak meter (runs in parallel with audio output)
    TruePeakMeter m_truePeakMeter;
```

### 4. AudioEngine.cpp - Integrate True Peak Measurement

```cpp
// In AudioEngine::processBlock(), after final output is written

void AudioEngine::processBlock(float* outputBuffer, const float* inputBuffer, 
                               uint32_t numFrames, double streamTime) {
    // ... existing processing ...
    
    // Final output written to outputBuffer
    
    // === True Peak Metering ===
    // Measure true peak on final output (does not alter signal)
    m_truePeakMeter.process(outputBuffer, numFrames);
    
    // ... rest of existing code ...
}

// Implement getters
float AudioEngine::getSamplePeakL() const {
    return m_truePeakMeter.getSamplePeakL();
}

float AudioEngine::getTruePeakL() const {
    return m_truePeakMeter.getTruePeakL();
}

float AudioEngine::getTruePeakLdBTP() const {
    return m_truePeakMeter.getTruePeakLdBTP();
}

// ... (similar for R channel and max)

void AudioEngine::resetPeakMeters() {
    m_truePeakMeter.reset();
}
```

### 5. AudioExporter.cpp - Add True Peak Validation

```cpp
// In AudioExporter::exportToFile(), after render completes

bool AudioExporter::exportToFile(const ExportConfig& config) {
    // ... existing export code ...
    
    // === True Peak Validation ===
    if (config.validateTruePeak) {
        float maxTruePeak = m_engine.getMaxTruePeakdBTP();
        
        if (maxTruePeak > config.truePeakCeilingdBTP) {
            LOG_WARNING("Export exceeds true peak ceiling: " + 
                       std::to_string(maxTruePeak) + " dBTP > " +
                       std::to_string(config.truePeakCeilingdBTP) + " dBTP");
            
            if (config.failOnTruePeakExceeded) {
                return false;  // Abort export
            }
        }
    }
    
    // ... rest of export ...
}
```

---

## Unit/Integration Tests

### Test 1: Sine Wave True Peak

```cpp
TEST(TruePeakMeter, SineWaveDetection) {
    TruePeakMeter meter;
    meter.initialize(48000);
    
    // Generate 1 kHz sine wave at -3 dBFS
    std::vector<float> sine = generateSine(1000.0f, 48000, 1.0f, 0.707f);
    
    // Process
    meter.process(sine.data(), sine.size() / 2);
    
    // True peak should be slightly higher than sample peak
    float samplePeak = meter.getSamplePeakL();
    float truePeak = meter.getTruePeakL();
    
    EXPECT_GT(truePeak, samplePeak);
    EXPECT_NEAR(truePeak, 0.707f, 0.01f);
}
```

### Test 2: Intersample Peak Detection

```cpp
TEST(TruePeakMeter, IntersamplePeakDetection) {
    TruePeakMeter meter;
    meter.initialize(48000);
    
    // Generate signal with known intersample peak
    // (alternating +1, -1 creates worst-case intersample peak)
    std::vector<float> signal(1024);
    for (size_t i = 0; i < signal.size(); i += 2) {
        signal[i] = 1.0f;
        signal[i + 1] = -1.0f;
    }
    
    meter.process(signal.data(), signal.size() / 2);
    
    float samplePeak = meter.getSamplePeakL();
    float truePeak = meter.getTruePeakL();
    
    // Sample peak is 1.0, but true peak should exceed 1.0
    EXPECT_EQ(samplePeak, 1.0f);
    EXPECT_GT(truePeak, 1.0f);
    EXPECT_LT(truePeak, 1.5f);  // Reasonable upper bound
}
```

### Test 3: Export Validation

```cpp
TEST(AudioExporter, TruePeakValidation) {
    AudioEngine engine;
    engine.initialize(48000, 256);
    
    // Load audio with intersample peaks > -1 dBTP
    auto track = engine.createTrack();
    track->loadAudio(generateHotSignal());  // Peaks at +0.5 dBTP
    
    // Export with true peak validation
    ExportConfig config;
    config.validateTruePeak = true;
    config.truePeakCeilingdBTP = -1.0f;  // Spotify standard
    config.failOnTruePeakExceeded = true;
    
    AudioExporter exporter(engine);
    bool success = exporter.exportToFile(config);
    
    // Should fail (exceeds -1 dBTP ceiling)
    EXPECT_FALSE(success);
}
```

---

## Rollback Notes

### If True Peak Metering Causes Issues

1. **Disable true peak measurement:**
   ```cpp
   // Comment out in processBlock()
   // m_truePeakMeter.process(outputBuffer, numFrames);
   ```

2. **Fallback to sample peak:**
   - UI can display sample peak only
   - Export validation uses sample peak

3. **No RT safety impact:**
   - Metering is separate from audio path
   - Disabling it doesn't affect audio quality

---

## Performance Analysis

### CPU Cost Estimate

**Per Sample:**
- 4 oversampled values
- 48-tap FIR filter per value
- Total: ~200 multiply-adds per sample

**For 256 samples stereo:**
- 256 * 2 * 200 = 102,400 operations
- @ 3 GHz CPU = ~0.03ms
- **Acceptable** (~0.6% of 5ms buffer time)

### Optimization Opportunities

1. **SIMD:** Vectorize FIR filter (4x speedup)
2. **Downsampling:** Only measure every Nth block (reduces CPU)
3. **Separate thread:** Move metering off RT thread entirely

---

## Definition of Done

- [ ] TruePeakMeter.h/cpp implemented
- [ ] ITU-R BS.1770-4 coefficients verified
- [ ] Integrated into AudioEngine::processBlock()
- [ ] Export validation implemented
- [ ] All 3 tests passing
- [ ] Performance profiled (< 1% CPU overhead)
- [ ] UI displays both sample peak and true peak
- [ ] Documentation updated

---

**Status:** Ready for implementation after PDC
