# Plugin Delay Compensation (PDC) Implementation Plan

**Phase:** 1 (Blocking)  
**Priority:** Critical  
**Estimated Effort:** 2-3 weeks  
**Status:** Not Started

---

## Design Summary

Implement sample-accurate plugin delay compensation by:
1. Tracking plugin latency per track
2. Computing max latency across all active tracks
3. Inserting delay lines to align faster tracks with slowest track
4. Publishing compensation state via immutable RT snapshots
5. Handling dynamic latency changes (plugin load/unload/bypass)

**Key Principle:** Compensation buffers are preallocated outside RT thread, RT thread only reads/writes fixed memory.

---

## Architecture

### Data Flow

```
[Plugin Reports Latency]
         ↓
[EffectChain::getTotalLatency()] ← already exists
         ↓
[AudioEngine::calculateLatencyCompensation()] ← NEW (main thread)
         ↓
[Create new GraphSnapshot with compensation delays] ← NEW
         ↓
[Publish snapshot atomically] ← existing pattern
         ↓
[RT thread reads compensation delays] ← NEW
         ↓
[Apply delay lines in renderTrack()] ← NEW
```

### State Ownership

**Main Thread (Non-RT):**
- Calculates compensation delays
- Allocates/resizes delay buffers
- Publishes new snapshots

**RT Thread (Audio Callback):**
- Reads compensation delays from snapshot
- Writes/reads from preallocated delay buffers
- Never allocates, never resizes

---

## Files to Touch

### New Files
1. `AestraAudio/include/DSP/DelayLine.h` - RT-safe ring buffer for compensation
2. `AestraAudio/src/DSP/DelayLine.cpp` - Implementation
3. `Tests/Audio/LatencyCompensationTest.cpp` - Comprehensive tests

### Modified Files
1. `AestraAudio/include/Core/AudioGraphState.h` - Add latency fields to TrackRTState
2. `AestraAudio/include/Core/AudioEngine.h` - Add calculateLatencyCompensation()
3. `AestraAudio/src/Core/AudioEngine.cpp` - Implement compensation logic
4. `AestraAudio/include/Plugin/EffectChain.h` - Add latency change notification
5. `AestraAudio/src/Plugin/EffectChain.cpp` - Trigger recalculation on changes

---

## Exact State Changes

### 1. AudioGraphState.h

```cpp
// Add to TrackRTState struct (around line 16)

struct TrackRTState {
    // ... existing fields ...
    
    // === Plugin Delay Compensation ===
    uint32_t pluginLatencySamples{0};        // Total latency from effect chain
    uint32_t compensationDelaySamples{0};    // Delay to apply for alignment
    bool compensationEnabled{true};          // Can be disabled per-track
    
    // Delay line for compensation (preallocated, RT-safe)
    // Sized to worst-case max latency (e.g., 8192 samples = ~170ms @ 48kHz)
    std::array<float, 16384> compensationBuffer{}; // Stereo interleaved
    uint32_t compensationWritePos{0};
    uint32_t compensationReadPos{0};
    
    // ... rest of existing fields ...
};

// Add to GraphSnapshot struct (around line 50)

struct GraphSnapshot {
    // ... existing fields ...
    
    // === Latency Compensation State ===
    uint32_t maxProjectLatencySamples{0};    // Slowest track latency
    bool latencyCompensationEnabled{true};   // Global enable/disable
    
    // ... rest of existing fields ...
};
```

**RT Safety Analysis:**
- ✅ All fields are POD types or fixed-size arrays
- ✅ No heap allocation in RT thread
- ✅ Write positions updated atomically per track
- ✅ Buffer size is compile-time constant

### 2. DelayLine.h (New File)

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace Aestra::Audio {

/**
 * @brief Fixed-capacity delay line for RT-safe plugin delay compensation
 * 
 * Uses a ring buffer with compile-time capacity. No allocations after construction.
 * Thread-safe for single writer, single reader (RT audio thread).
 */
template<typename T, size_t Capacity>
class DelayLine {
public:
    DelayLine() = default;
    
    /**
     * @brief Set delay in samples (must be <= Capacity)
     * NOT RT-SAFE: Call from main thread only
     */
    void setDelay(uint32_t delaySamples) noexcept {
        m_delay = (delaySamples <= Capacity) ? delaySamples : Capacity;
        reset();
    }
    
    /**
     * @brief Reset delay line to silence
     * NOT RT-SAFE: Call from main thread only
     */
    void reset() noexcept {
        m_buffer.fill(T{0});
        m_writePos = 0;
        m_readPos = 0;
    }
    
    /**
     * @brief Process a single sample through the delay line
     * RT-SAFE: No allocations, deterministic
     */
    inline T process(T input) noexcept {
        // Write input
        m_buffer[m_writePos] = input;
        
        // Calculate read position (writePos - delay)
        uint32_t readPos = (m_writePos + Capacity - m_delay) % Capacity;
        
        // Read delayed output
        T output = m_buffer[readPos];
        
        // Advance write position
        m_writePos = (m_writePos + 1) % Capacity;
        
        return output;
    }
    
    /**
     * @brief Process a block of samples
     * RT-SAFE: No allocations, deterministic
     */
    void processBlock(const T* input, T* output, uint32_t numSamples) noexcept {
        for (uint32_t i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }
    
    uint32_t getDelay() const noexcept { return m_delay; }
    uint32_t getCapacity() const noexcept { return Capacity; }
    
private:
    std::array<T, Capacity> m_buffer{};
    uint32_t m_writePos{0};
    uint32_t m_delay{0};
};

} // namespace Aestra::Audio
```

**RT Safety Analysis:**
- ✅ Fixed-size array (no heap allocation)
- ✅ All operations are O(1) and deterministic
- ✅ No locks, no system calls
- ✅ `setDelay()` and `reset()` are NOT RT-safe (called from main thread only)
- ✅ `process()` and `processBlock()` are RT-safe

### 3. AudioEngine.h

```cpp
// Add to AudioEngine class (around line 350)

public:
    // === Plugin Delay Compensation ===
    
    /**
     * @brief Calculate and apply plugin delay compensation across all tracks
     * 
     * Computes max latency, sets compensation delays, and publishes new snapshot.
     * NOT RT-SAFE: Call from main thread only.
     * 
     * Triggers:
     * - Plugin load/unload
     * - Plugin bypass toggle
     * - Effect chain reorder
     * - Track enable/disable
     */
    void calculateLatencyCompensation();
    
    /**
     * @brief Enable/disable global latency compensation
     */
    void setLatencyCompensationEnabled(bool enabled);
    bool isLatencyCompensationEnabled() const;
    
    /**
     * @brief Get current max project latency in samples
     */
    uint32_t getMaxProjectLatency() const;

private:
    // Latency compensation state (main thread only)
    bool m_latencyCompensationEnabled{true};
    uint32_t m_maxProjectLatency{0};
    bool m_latencyDirty{true};  // Recalculate on next safe opportunity
```

### 4. AudioEngine.cpp - Implementation

```cpp
// Add to AudioEngine.cpp (after processBlock implementation)

void AudioEngine::calculateLatencyCompensation() {
    if (!m_latencyCompensationEnabled) {
        m_maxProjectLatency = 0;
        m_latencyDirty = false;
        return;
    }
    
    // 1. Find max latency across all active tracks
    uint32_t maxLatency = 0;
    
    for (auto& track : m_tracks) {
        if (!track.enabled) continue;
        
        uint32_t trackLatency = 0;
        if (track.effectChain) {
            trackLatency = track.effectChain->getTotalLatency();
        }
        
        maxLatency = std::max(maxLatency, trackLatency);
    }
    
    m_maxProjectLatency = maxLatency;
    
    // 2. Calculate compensation delay for each track
    for (auto& track : m_tracks) {
        uint32_t trackLatency = 0;
        if (track.effectChain) {
            trackLatency = track.effectChain->getTotalLatency();
        }
        
        uint32_t compensationDelay = maxLatency - trackLatency;
        
        // Update track RT state (will be published in next snapshot)
        track.rtState.pluginLatencySamples = trackLatency;
        track.rtState.compensationDelaySamples = compensationDelay;
        
        // Reset delay line if compensation changed
        if (compensationDelay != track.rtState.compensationDelaySamples) {
            // Clear compensation buffer
            track.rtState.compensationBuffer.fill(0.0f);
            track.rtState.compensationWritePos = 0;
            track.rtState.compensationReadPos = 0;
        }
    }
    
    // 3. Publish new snapshot with updated compensation state
    publishGraphSnapshot();
    
    m_latencyDirty = false;
    
    LOG_INFO("PDC: Max latency = " + std::to_string(maxLatency) + " samples (" +
             std::to_string(maxLatency * 1000.0 / m_sampleRate) + " ms)");
}

void AudioEngine::setLatencyCompensationEnabled(bool enabled) {
    if (m_latencyCompensationEnabled != enabled) {
        m_latencyCompensationEnabled = enabled;
        m_latencyDirty = true;
        calculateLatencyCompensation();
    }
}

bool AudioEngine::isLatencyCompensationEnabled() const {
    return m_latencyCompensationEnabled;
}

uint32_t AudioEngine::getMaxProjectLatency() const {
    return m_maxProjectLatency;
}
```

### 5. AudioEngine.cpp - Apply Compensation in Render Path

```cpp
// Modify renderTrack() to apply compensation (around line 600-700)

void AudioEngine::renderTrack(TrackRTState& track, double* masterBuffer, uint32_t numFrames) {
    // ... existing track rendering code ...
    
    // === Apply Plugin Delay Compensation ===
    if (track.compensationEnabled && track.compensationDelaySamples > 0) {
        // Process through delay line (stereo interleaved)
        for (uint32_t i = 0; i < numFrames; ++i) {
            float L = track.preFaderBuffer[i * 2];
            float R = track.preFaderBuffer[i * 2 + 1];
            
            // Write to ring buffer
            uint32_t writeIdx = track.compensationWritePos * 2;
            track.compensationBuffer[writeIdx] = L;
            track.compensationBuffer[writeIdx + 1] = R;
            
            // Read from ring buffer (delayed)
            uint32_t readIdx = track.compensationReadPos * 2;
            track.preFaderBuffer[i * 2] = track.compensationBuffer[readIdx];
            track.preFaderBuffer[i * 2 + 1] = track.compensationBuffer[readIdx + 1];
            
            // Advance pointers
            track.compensationWritePos = (track.compensationWritePos + 1) % 8192;
            track.compensationReadPos = (track.compensationReadPos + 1) % 8192;
        }
    }
    
    // ... continue with fader, pan, sends, etc. ...
}
```

**RT Safety Analysis:**
- ✅ No allocations (buffer is preallocated)
- ✅ No locks
- ✅ Deterministic loop (fixed iteration count)
- ✅ Simple modulo arithmetic (fast on modern CPUs)
- ✅ Read/write positions are per-track (no contention)

### 6. EffectChain.cpp - Trigger Recalculation

```cpp
// Add to EffectChain.cpp (after addEffect, removeEffect, setBypass, etc.)

void EffectChain::addEffect(std::shared_ptr<PluginInstance> plugin, int position) {
    // ... existing code ...
    
    // Trigger latency recalculation
    if (m_engine) {
        m_engine->markLatencyDirty();
    }
}

void EffectChain::removeEffect(int position) {
    // ... existing code ...
    
    // Trigger latency recalculation
    if (m_engine) {
        m_engine->markLatencyDirty();
    }
}

void EffectChain::setBypass(int position, bool bypassed) {
    // ... existing code ...
    
    // Trigger latency recalculation (bypassed plugins may not contribute latency)
    if (m_engine) {
        m_engine->markLatencyDirty();
    }
}
```

---

## Unit/Integration Tests

### Test 1: Parallel Track Alignment

```cpp
// Tests/Audio/LatencyCompensationTest.cpp

TEST(LatencyCompensation, ParallelTracksAlign) {
    AudioEngine engine;
    engine.initialize(48000, 256);
    
    // Create two tracks
    auto track1 = engine.createTrack();
    auto track2 = engine.createTrack();
    
    // Track 1: No plugins (0 latency)
    // Track 2: Mock plugin with 512 samples latency
    auto mockPlugin = std::make_shared<MockPluginWithLatency>(512);
    track2->addPlugin(mockPlugin);
    
    // Load identical impulse on both tracks
    std::vector<float> impulse(1024, 0.0f);
    impulse[0] = 1.0f;  // Single impulse at start
    
    track1->loadAudio(impulse);
    track2->loadAudio(impulse);
    
    // Calculate compensation
    engine.calculateLatencyCompensation();
    
    // Verify compensation delays
    EXPECT_EQ(track1->getCompensationDelay(), 512);  // Delayed to match track2
    EXPECT_EQ(track2->getCompensationDelay(), 0);    // No delay needed
    
    // Render offline
    std::vector<float> output(2048 * 2);  // Stereo
    engine.renderOffline(output.data(), 2048);
    
    // Find impulse peaks in output
    int peak1 = findFirstPeak(output);
    int peak2 = findSecondPeak(output);
    
    // Both impulses should arrive at the same time (512 samples in)
    EXPECT_EQ(peak1, 512);
    EXPECT_EQ(peak2, 512);
}
```

### Test 2: Bypass Toggle Updates Compensation

```cpp
TEST(LatencyCompensation, BypassToggleUpdates) {
    AudioEngine engine;
    engine.initialize(48000, 256);
    
    auto track = engine.createTrack();
    auto plugin = std::make_shared<MockPluginWithLatency>(256);
    track->addPlugin(plugin);
    
    // Initial state: plugin active
    engine.calculateLatencyCompensation();
    uint32_t latencyActive = engine.getMaxProjectLatency();
    EXPECT_EQ(latencyActive, 256);
    
    // Bypass plugin
    track->setPluginBypass(0, true);
    engine.calculateLatencyCompensation();
    uint32_t latencyBypassed = engine.getMaxProjectLatency();
    EXPECT_EQ(latencyBypassed, 0);  // Bypassed plugins don't contribute
    
    // Un-bypass
    track->setPluginBypass(0, false);
    engine.calculateLatencyCompensation();
    uint32_t latencyRestored = engine.getMaxProjectLatency();
    EXPECT_EQ(latencyRestored, 256);
}
```

### Test 3: Null Test (Same Chain, Perfect Alignment)

```cpp
TEST(LatencyCompensation, NullTest) {
    AudioEngine engine;
    engine.initialize(48000, 256);
    
    // Two tracks with identical chains
    auto track1 = engine.createTrack();
    auto track2 = engine.createTrack();
    
    auto plugin1 = std::make_shared<MockPluginWithLatency>(128);
    auto plugin2 = std::make_shared<MockPluginWithLatency>(128);
    
    track1->addPlugin(plugin1);
    track2->addPlugin(plugin2);
    
    // Load identical audio
    std::vector<float> audio = generateWhiteNoise(4096);
    track1->loadAudio(audio);
    track2->loadAudio(audio);
    
    // Calculate compensation (should be 0 for both)
    engine.calculateLatencyCompensation();
    EXPECT_EQ(track1->getCompensationDelay(), 0);
    EXPECT_EQ(track2->getCompensationDelay(), 0);
    
    // Render
    std::vector<float> output(4096 * 2);
    engine.renderOffline(output.data(), 4096);
    
    // Tracks should sum perfectly (no phase cancellation)
    float rms = calculateRMS(output);
    EXPECT_GT(rms, 0.1f);  // Should NOT null (both tracks active)
    
    // Invert one track and re-render
    track2->setGain(-1.0f);
    engine.renderOffline(output.data(), 4096);
    
    // Now should null (within -120 dB)
    float nullRMS = calculateRMS(output);
    EXPECT_LT(nullRMS, 1e-6f);  // -120 dB threshold
}
```

### Test 4: Dynamic Latency Change (Plugin Load/Unload)

```cpp
TEST(LatencyCompensation, DynamicLatencyChange) {
    AudioEngine engine;
    engine.initialize(48000, 256);
    
    auto track = engine.createTrack();
    
    // Start with no plugins
    engine.calculateLatencyCompensation();
    EXPECT_EQ(engine.getMaxProjectLatency(), 0);
    
    // Add plugin with latency
    auto plugin = std::make_shared<MockPluginWithLatency>(512);
    track->addPlugin(plugin);
    engine.calculateLatencyCompensation();
    EXPECT_EQ(engine.getMaxProjectLatency(), 512);
    
    // Remove plugin
    track->removePlugin(0);
    engine.calculateLatencyCompensation();
    EXPECT_EQ(engine.getMaxProjectLatency(), 0);
}
```

---

## Rollback Notes

### If PDC Causes Issues

1. **Disable globally:**
   ```cpp
   engine.setLatencyCompensationEnabled(false);
   ```

2. **Revert code changes:**
   - Remove compensation logic from `renderTrack()`
   - Keep latency tracking (harmless)
   - Keep tests (for future retry)

3. **Fallback behavior:**
   - Engine works exactly as before PDC implementation
   - No RT safety regressions
   - Latency tracking remains for future use

### Known Risks

1. **Buffer size too small:**
   - Current: 16384 samples = ~340ms @ 48kHz
   - If plugins report >340ms latency, compensation will clip
   - Mitigation: Log warning, clamp to max

2. **Latency changes during playback:**
   - Could cause clicks if not handled
   - Mitigation: Fade compensation buffer on change

3. **CPU cost:**
   - Per-sample ring buffer read/write
   - Mitigation: Profile, optimize if needed (SIMD copy)

---

## Performance Analysis

### CPU Cost Estimate

**Per Track, Per Sample:**
- 2 array writes (L/R)
- 2 array reads (L/R)
- 2 modulo operations (write/read pos)
- Total: ~10 cycles/sample

**For 64 tracks @ 256 samples:**
- 64 * 256 * 10 = 163,840 cycles
- @ 3 GHz CPU = 0.05ms
- **Negligible impact** (<1% of 5ms buffer time)

### Memory Cost

**Per Track:**
- 16384 floats * 4 bytes = 65 KB
- 64 tracks = 4 MB total
- **Acceptable** for modern systems

---

## Definition of Done

- [x] Design reviewed and approved
- [ ] DelayLine.h implemented and unit tested
- [ ] AudioGraphState.h updated with compensation fields
- [ ] AudioEngine::calculateLatencyCompensation() implemented
- [ ] Compensation applied in renderTrack()
- [ ] EffectChain triggers recalculation on changes
- [ ] All 4 tests passing
- [ ] No RT violations (verified with ThreadSanitizer)
- [ ] Performance profiled (< 1% CPU overhead)
- [ ] Documentation updated
- [ ] Code review completed
- [ ] Merged to develop branch

---

## Next Steps

1. Review this plan with team
2. Implement DelayLine.h (1 day)
3. Update AudioGraphState.h (1 day)
4. Implement calculateLatencyCompensation() (2 days)
5. Apply compensation in render path (2 days)
6. Write and verify tests (3 days)
7. Profile and optimize (2 days)
8. Code review and merge (1 day)

**Total: ~12 days (2.5 weeks)**

---

**Status:** Ready for implementation
