# PreviewEngine Design Specification

## Goals

* **Dedicated preview path**: Play/stop previews without touching transport/TrackManager state.
* **No loop bleed**: Always stops when requested; auto-stops after N seconds with fade-out.
* **Correct SRC**: Always uses actual stream rate from AudioDeviceManager, handles mid-playback rate changes gracefully.
* **Reuse buffers**: Pull from SamplePool so decode is shared with tracks, no duplicate memory.
* **Thread-safe and RT-safe**: Zero allocations in audio callback, lock-free read path.
* **Clean UX**: Visual feedback when preview completes, cancel in-flight previews on new requests.

---

## Architecture

### PreviewEngine (singleton owned by NomadApp)

#### State Structure

```cpp
struct PreviewVoice {
    std::shared_ptr<AudioBuffer> buffer;  // From SamplePool
    double phaseFrames;                    // Current playback position
    double sampleRate;                     // Buffer's native sample rate
    uint32_t channels;                     // Always 2 (forced stereo)
    float gain;                            // Linear gain (0.0-1.0)
    double durationSeconds;                // Total duration
    double maxPlaySeconds;                 // Auto-stop cap (0 = play full)
    double elapsedSeconds;                 // Tracking for auto-stop
    std::atomic<bool> playing;             // Atomic flag for RT thread
    
    static constexpr double FADEOUT_SECONDS = 0.05;  // 50ms tail
};
```

#### Thread-Safe State Management

```cpp
// Control thread writes, audio thread reads (lock-free)
std::atomic<PreviewVoice*> activeVoice{nullptr};
std::mutex controlMutex;  // Only guards allocation/deallocation
std::atomic<double> outputSampleRate{48000.0};
```

**Pattern:**

* Control thread (play/stop) allocates/deallocates behind `controlMutex`
* Audio thread reads `activeVoice.load(std::memory_order_acquire)` without locks
* When stopping, set `playing = false`, then swap pointer to nullptr after fadeout

---

### API

#### Core Methods

```cpp
enum class PreviewResult {
    Success,      // Started immediately (buffer ready)
    Pending,      // Decode in progress (async)
    Failed        // Decode error or invalid file
};

PreviewResult play(const std::string& path, 
                   float gainDb = -6.0f,     // dB for UI-friendly control
                   double maxSeconds = 5.0);  // 0 = no limit

void stop();  // Immediate stop with fadeout

void setOutputSampleRate(double sr);  // Called each callback from ADM

void process(float* outL, float* outR, uint32_t frames);  // Mix preview into output

bool isPlaying() const;  // Thread-safe query

void setOnComplete(std::function<void(const std::string& path)> callback);
```

#### Global Settings

```cpp
void setGlobalPreviewVolume(float gainDb);  // User preference (-inf to +6dB)
float getGlobalPreviewVolume() const;
```

---

### Behavior Details

#### On `play(path, gainDb, maxSeconds)`

1. **Cancel in-flight preview**: If `activeVoice != nullptr`, call `stop()` to fade out existing preview
2. **Acquire buffer from SamplePool**:

   * Reuse `Track::loadAudioFile` lambda or shared `SampleLoader` helper
   * If decode is async, return `Pending` and subscribe to completion callback
   * If decode fails, return `Failed` and log warning
3. **Force stereo**:

   * Mono: duplicate channel to L+R
   * >2 channels: downmix to stereo (average all channels)
4. **Prepare voice**:

   * `phaseFrames = 0`
   * `sampleRate = buffer->getSampleRate()` (native rate)
   * `gain = dbToLinear(gainDb + globalPreviewVolume)`
   * `elapsedSeconds = 0`
   * `maxPlaySeconds = maxSeconds`
   * `playing = true`
5. **Atomically swap**: `activeVoice.store(newVoice, std::memory_order_release)`
6. **Return** `Success` (or `Pending` if async)

#### On `stop()`

1. **Lock control mutex**: `std::lock_guard lock(controlMutex)`
2. **Get current voice**: `auto* voice = activeVoice.load()`
3. **If voice exists**:
   * Set `voice->playing = false` (starts fadeout in audio thread)
   * Schedule deletion after fadeout completes (use flag or timer)
4. **Invoke completion callback** with path

#### On `setOutputSampleRate(sr)`

1. **Store atomically**: `outputSampleRate.store(sr, std::memory_order_release)`
2. **Handle mid-playback rate change**:
   * If `sr` changes and voice is playing, scale `phaseFrames` proportionally:

     ```cpp
     newPhase = phaseFrames * (newSR / oldSR)
     ```

   * Alternatively, stop preview on rate change (simpler, acceptable for device switches)

#### On `process(outL, outR, frames)`

1. **Load voice atomically**:

   ```cpp
   auto* voice = activeVoice.load(std::memory_order_acquire);
   if (!voice || !voice->playing) return;
   ```

2. **Copy scalars to avoid race**:

   ```cpp
   const double bufferRate = voice->sampleRate;
   const double streamRate = outputSampleRate.load(std::memory_order_relaxed);
   const double ratio = bufferRate / streamRate;  // Resample ratio
   const float gain = voice->gain;
   double phase = voice->phaseFrames;
   ```

3. **Fallback for unknown rate**: `if (streamRate <= 0) streamRate = 48000.0;`
4. **Resample and mix**:

   ```cpp
   for (uint32_t i = 0; i < frames; ++i) {
       const int32_t idx = static_cast<int32_t>(phase);
       if (idx >= voice->buffer->getNumFrames()) {
           voice->playing = false;  // End of buffer
           invokeCompletionCallback(voice->path);
           break;
       }
       
       // Linear interpolation (or reuse Track::interpolators for cubic)
       const float frac = phase - idx;
       const float sampleL = lerp(buffer[idx*2], buffer[(idx+1)*2], frac);
       const float sampleR = lerp(buffer[idx*2+1], buffer[(idx+1)*2+1], frac);
       
       // Apply gain with fadeout envelope
       float envelope = 1.0f;
       if (!voice->playing) {  // Fadeout active
           double fadeRemaining = FADEOUT_SECONDS - voice->fadeElapsed;
           envelope = std::max(0.0f, fadeRemaining / FADEOUT_SECONDS);
           voice->fadeElapsed += 1.0 / streamRate;
       }
       
       outL[i] += sampleL * gain * envelope;
       outR[i] += sampleR * gain * envelope;
       
       phase += ratio;
   }
   voice->phaseFrames = phase;
   ```

5. **Track elapsed time**:

   ```cpp
   voice->elapsedSeconds += frames / streamRate;
   if (voice->maxPlaySeconds > 0 && voice->elapsedSeconds >= voice->maxPlaySeconds) {
       voice->playing = false;  // Trigger fadeout
   }
   ```

6. **Cleanup after fadeout**:

   ```cpp
   if (!voice->playing && voice->fadeElapsed >= FADEOUT_SECONDS) {
       activeVoice.store(nullptr, std::memory_order_release);
       scheduleVoiceDeletion(voice);  // Delete on control thread
   }
   ```

---

## Integration Points

### AudioDeviceManager Callback (Source/Main.cpp)

```cpp
void audioCallback(float** output, uint32_t nFrames) {
    // 1. Update preview engine with actual stream rate
    double actualSR = audioDevice->getStreamSampleRate();
    if (actualSR <= 0) actualSR = audioDevice->getRequestedSampleRate();
    previewEngine.setOutputSampleRate(actualSR);
    
    // 2. Process tracks (existing)
    trackManager.processAudio(output[0], output[1], nFrames);
    
    // 3. Mix preview on top (adds to buffer, doesn't replace)
    previewEngine.process(output[0], output[1], nFrames);
}
```

### UI/FileBrowser Integration

```cpp
// Preview button callback
void onPreviewButtonClicked(const std::string& filePath) {
    auto result = previewEngine.play(filePath, -6.0f, 5.0);  // -6dB, 5s max
    
    switch (result) {
        case PreviewResult::Success:
            updatePreviewButton(filePath, PlaybackState::Playing);
            break;
        case PreviewResult::Pending:
            updatePreviewButton(filePath, PlaybackState::Loading);
            break;
        case PreviewResult::Failed:
            showError("Could not load preview");
            break;
    }
}

// Stop button or automatic cleanup
void onPreviewComplete(const std::string& path) {
    updatePreviewButton(path, PlaybackState::Stopped);
}

// Register callback
previewEngine.setOnComplete(onPreviewComplete);
```

### Auto-Stop on Track Playback

```cpp
// In TrackManager::play() or transport start
void startPlayback() {
    previewEngine.stop();  // Optional: avoid overlapping
    // ... existing playback logic
}
```

---

## Resampling Strategy

### Interpolation Method

* **Default**: Linear interpolation (fast, adequate for preview quality)
* **Optional**: Reuse `Track::interpolators` (cubic/sinc) if already implemented
* **Implementation**:

  ```cpp
  inline float lerp(float a, float b, float t) {
      return a + t * (b - a);
  }
  ```

### Ratio Calculation

```cpp
const double ratio = voice->sampleRate / outputSampleRate;
// Example: 44.1kHz file playing at 48kHz = 44100/48000 = 0.91875
```

---

## Buffer Ownership & Memory

### Shared Ownership

* **No copying**: Hold `std::shared_ptr<AudioBuffer>` from SamplePool
* **Lifetime**: Buffer kept alive while voice is active
* **SamplePool contract**: Ensure pool doesn't evict buffers that are still referenced

### Channel Handling

```cpp
// In loader or PreviewEngine::play()
if (buffer->channels == 1) {
    // Duplicate mono to stereo in-place or lazily in process()
    forceMonoToStereo(buffer);
} else if (buffer->channels > 2) {
    // Downmix to stereo: average all channels
    downmixToStereo(buffer);
}
// Result: buffer->channels always == 2
```

---

## Edge Cases & Error Handling

### Decode Failures

* **Return** `PreviewResult::Failed` from `play()`
* **Log** warning: `"PreviewEngine: Failed to load {path}"`
* **No state change**: Don't create voice, don't invoke callback

### Unknown Sample Rate

* **Fallback**: If `outputSampleRate == 0`, use `48000.0`
* **Log** once: `"PreviewEngine: Unknown stream rate, assuming 48kHz"`

### Device Changes Mid-Playback

* **Option A**: Stop preview on rate change (simple, acceptable)
* **Option B**: Scale `phaseFrames` proportionally (smoother):

  ```cpp
  newPhase = oldPhase * (newRate / oldRate)
  ```

### Spam Clicks on Preview Button

* **Cancel existing preview**: `play()` calls `stop()` internally before loading new file
* **Async decode**: If previous decode is pending, cancel it (requires SamplePool cancellation API)

### Mono Output Device

* **Unlikely**, but if device is mono: write stereo anyway, device driver will downmix
* **No special handling needed**

---

## Thread Safety Summary

### Control Thread (UI/Main)

* **Holds** `controlMutex` during `play()` and `stop()`
* **Allocates/deallocates** PreviewVoice
* **Swaps** `activeVoice` pointer atomically

### Audio Thread (RT Callback)

* **No locks**: Reads `activeVoice` with `std::memory_order_acquire`
* **No allocations**: All buffers pre-allocated
* **Atomic flags**: Uses `voice->playing` (`atomic<bool>`) for coordination

### Lock-Free Pattern

```cpp
// Control thread
{
    std::lock_guard lock(controlMutex);
    auto* newVoice = new PreviewVoice{...};
    auto* oldVoice = activeVoice.exchange(newVoice, std::memory_order_acq_rel);
    if (oldVoice) scheduleDelete(oldVoice);  // After fadeout
}

// Audio thread
auto* voice = activeVoice.load(std::memory_order_acquire);
if (voice && voice->playing) {
    // process...
}
```

---

## Logging & Telemetry

### Debug Logging (Optional)

```cpp
// On first play of a file
LOG_INFO("PreviewEngine: Playing '%s' (%.1f kHz, %.2f sec)", 
         path.c_str(), sampleRate / 1000.0, durationSeconds);

// On auto-stop
LOG_DEBUG("PreviewEngine: Auto-stopped after %.2f seconds", elapsedSeconds);

// On manual stop
LOG_DEBUG("PreviewEngine: Stopped by user");
```

### Metrics (Optional)

* Track: total previews played, average duration, decode failures
* Rate-limit logs in production to avoid spam

---

## Testing Strategy

### Unit Tests

1. **Phase advancement**: Verify `phaseFrames` increments correctly over multiple `process()` calls
2. **Auto-stop**: Confirm stops at `maxPlaySeconds` with fadeout
3. **Resampling**: Test various buffer rates (44.1k, 48k, 96k) against output rates
4. **Thread safety**: Spawn multiple threads calling `play()`/`stop()` while audio thread calls `process()`

### Integration Tests

1. **Device switch**: Change output device mid-preview, verify no crash/glitch
2. **Spam clicks**: Rapidly click preview button, confirm clean cancellation
3. **Memory**: Preview 100 files in sequence, check no leaks (valgrind/ASAN)

---

## Implementation Checklist

* [ ] Define `PreviewVoice` struct with atomic `playing` flag
* [ ] Implement lock-free `activeVoice` pointer swap
* [ ] Integrate with SamplePool for buffer acquisition
* [ ] Implement linear interpolation resampler
* [ ] Add 50ms fadeout envelope on stop
* [ ] Wire `process()` into AudioDeviceManager callback (after TrackManager)
* [ ] Connect UI preview buttons to `play()` API
* [ ] Add completion callback for UI state updates
* [ ] Handle device rate changes (stop or rescale phase)
* [ ] Write unit tests for phase/resampling/threading
* [ ] Add debug logging (rate-limited)
* [ ] Document global preview volume preference
* [ ] Test on real devices (macOS/Windows/Linux)

---

## Performance Notes

* **CPU**: Linear resampling adds ~5-10% overhead for one preview voice (negligible)
* **Memory**: Zero allocations in RT path; buffer shared with SamplePool
* **Latency**: No added latency (mixes in same callback as tracks)

---

## Future Enhancements (Out of Scope)

* Multi-voice previews (e.g., preview two files simultaneously)
* Pitch shifting during preview
* Looping mode for sustained previews
* Spectral visualization during preview
* Crossfade between sequential previews
