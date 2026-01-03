# **NOMAD DAW – Refined Implementation Report**

## *A Next-Generation Audio Engine Architecture for Low-Spec and High-Spec Systems*

By: Nomad Studios
Lead Architect: Dylan M.

---

## **0. Executive Summary**

Nomad’s v1 audio engine already handles decoding, playback, and limited streaming, but as you scale to more tracks, more formats, and more heavy sessions, the current architecture will:

* duplicate audio buffers across tracks
* allocate in the real-time callback
* resample with unnecessary temp buffers
* load preview audio separately
* stream WAV only
* burn RAM (full-buffer decoding)
* waste CPU (multiple copy passes)

This refined architecture transforms your design into a **professional-grade, scalable, low-latency engine** matching patterns used in Bitwig / Reaper / Kontakt:

1. **SamplePool** – global shared audio pool
2. **AudioSource API** – unified streaming + full-buffer access
3. **Voice model** – tracks become “voices” referencing samples
4. **RT-safe engine** – zero allocations, zero locks
5. **PreviewManager** – lightweight preview voice
6. **WaveformManager** – async visual caching
7. **Telemetry & tuning** – RAM/CPU visibility, auto-optimization

This architecture scales from a 2012 potato laptop to a Threadripper workstation.

---

## **1. SamplePool – Foundation of RAM Efficiency**

### *Status: Highest priority (Phase 1)*

## **1.1 Purpose**

Centralize audio ownership so tracks stop duplicating buffers.
This alone reduces RAM by **50–90%**.

## **1.2 Structures**

```cpp
struct SampleKey {
    std::string filePath;
    uint64_t modTime;
    uint32_t hash;
    bool operator==(const SampleKey&) const noexcept;
};

struct AudioBuffer {
    std::vector<float> data;   // interleaved floats
    int channels;
    int sampleRate;
    int64_t numFrames;

    // streaming metadata
    bool isStreaming;
    std::shared_ptr<IAudioSource> source;

    // lifecycle
    std::atomic<int> refCount{0};
    std::atomic<bool> ready{false};
    std::atomic<int> lastAccessTick{0};
};
```

## **1.3 SamplePool API**

```cpp
class SamplePool {
public:
    std::shared_ptr<AudioBuffer> acquire(const std::string& path);
    void release(const SampleKey& key);
    void garbageCollect();
    void setMemoryBudget(size_t bytes);
private:
    robin_hood::unordered_map<SampleKey, std::shared_ptr<AudioBuffer>> m_samples;
    size_t m_memoryBudget;
    size_t m_memoryCurrent;
};
```

## **1.4 Key Behaviors**

* Deduplicate: One file → one AudioBuffer
* Ref counting → safe use across tracks
* LRU eviction → discard unused samples
* Memory budget enforcement → potato-safe
* Fast lookup → robin_hood hashing

This turns Nomad into a memory-efficient DAW.

---

## **2. IAudioSource – Unified Audio Access Layer**

### *Status: Phase 2 (after SamplePool)*

The current engine has separate paths:

* Full-buffer (`m_audioData`)
* WAV streaming (`startWavStreaming`)
* MF decoding (blocking)
* Preview track (copies)

We unify these under a single interface:

```cpp
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    virtual uint64_t getLengthFrames() const = 0;
    virtual int      getSampleRate()  const = 0;
    virtual int      getChannels()    const = 0;

    // Real-time safe
    virtual void read(float* out, uint64_t startFrame, uint32_t frames) = 0;
};
```

## **2.1 Implementations**

### **BufferedSource**

For small samples:

```cpp
class BufferedSource : public IAudioSource {
    std::shared_ptr<AudioBuffer> buffer;
}
```

### **StreamingSource**

For larger files:

* Uses ring buffer
* Background thread fills upcoming audio chunks
* Zero disk access in callback
* Optionally uses miniaudio for MP3/FLAC/etc.

```cpp
class StreamingSource : public IAudioSource {
    RingBuffer<float> ring;
    std::atomic<uint64_t> ringStartFrame;
    std::atomic<uint64_t> availableFrames;
    StreamingThread worker;
}
```

---

## **3. Track = Voice**

### *Status: Phase 3 (after IAudioSource)*

You don’t want every Track to decode or own audio.
You want Tracks to be **playback controllers**.

```cpp
struct PlaybackVoice {
    std::shared_ptr<AudioBuffer> sample;
    std::shared_ptr<IAudioSource> source;
    double phaseFrames;
    float gain;
    bool active;
};
```

Tracks become:

```cpp
class Track {
private:
    PlaybackVoice m_voice;
public:
    void setSample(SampleHandle h); // via SamplePool
    void processAudio(float* out, int numFrames); // uses AudioSource
};
```

Benefits:

* Preview, Track, Sampler plugins all use the same voice system
* Multiple tracks can share a sample without re-decoding
* Future clip-based editing becomes simpler

---

## **4. Hard Real-Time Safety**

### *Status: Phase 3 (in parallel)*

## **4.1 Zero Allocations in Callback**

* Preallocate all temp buffers in Track constructor
* No `resize()`, no `new`, no vector growth
* No waveform building
* No reading from disk
* No locks

## **4.2 Atomic Track State**

```cpp
enum class TrackState { Idle, Active, PendingStop };
std::atomic<TrackState> state;
```

No mutexes.
State transitions happen in UI thread → commit in audio thread.

---

## **5. PreviewManager – Lightweight Previewing**

### *Status: Phase 4*

Preview should NEVER decode a full file again.
It should behave as:

* “Get sample from SamplePool”
* “Start a temporary PlaybackVoice”
* “Mix directly into preview bus”

```cpp
class PreviewManager {
    PlaybackVoice m_previewVoice;
public:
    void play(const std::string& path);
    void stop();
    void process(float* out, int frames);
};
```

Preview becomes instant and RAM-free.

---

## **6. WaveformManager – Async MinMax Caching**

### *Status: Phase 5*

Waveforms belong to SamplePool, not Tracks.

```cpp
struct WaveformCache {
    std::vector<int8_t> minVals;
    std::vector<int8_t> maxVals;
    std::atomic<bool> ready;
};
```

WaveformManager builds waveforms in background:

* converts min/max to 8-bit
* stores next to AudioBuffer
* builds only once per sample
* regenerates when file changes

Tracks simply request:

```cpp
const WaveformCache* wf = WaveformManager::get(sampleHandle);
```

Zero CPU in audio thread.

---

## **7. Telemetry & Auto-Tuning**

### *Status: Phase 6*

Expose engine metrics:

```cpp
struct EngineMetrics {
    size_t samplePoolRAM;
    size_t streamingBufferRAM;
    float  memoryPressure; // 0–100%
    int    activeStreams;
    int    cacheHits;
};
```

* Populated every second in UI thread
* Micro LED in DAW: green/yellow/red
* Stream target/chunk size auto-changes based on disk speed
* Reduce ahead-buffer on SSD
* Increase ahead-buffer on HDD for low-seek latency

This makes Nomad self-optimizing.

---

## **8. Implementation Timeline (Refined)**

| Phase | Subsystem                            | Duration | Impact                          |
| ----- | ------------------------------------ | -------- | ------------------------------- |
| 1     | SamplePool + ref counting            | 3–4 days | Massive RAM win                 |
| 2     | Universal Streaming via IAudioSource | 4–6 days | Low RAM & smooth playback       |
| 3     | RT safety + preallocation            | 1–2 days | Low CPU, no pops                |
| 4     | Voice model for Tracks               | 2–3 days | Future-proofing                 |
| 5     | PreviewManager                       | 1–2 days | Instant preview, no duplication |
| 6     | WaveformManager async                | 2 days   | No UI stutter                   |
| 7     | Telemetry & auto-tune                | 1–2 days | Diagnostics, stability          |

**Total = 14–20 days of deep work**, spaced over weeks as you build features.

---

## **9. Migration Strategy**

### *Critical: No rewrites. Only staged swap-ins.*

1. Keep old Track logic working while implementing SamplePool silently.
2. Swap Track::m_audioData → SamplePool inside loadAudioFile.
3. Replace streaming thread with StreamingSource under the hood.
4. Insert IAudioSource behind Track::copyAudioData.
5. Migrate preview to PreviewManager.
6. Migrate waveform building to new async system.
7. Remove old subsystems only after new ones are stable.

Nomad stays usable throughout.

---

## **10. Final Architecture Diagram**

```table
                          ┌────────────────────┐
                          │     SamplePool     │
                          │ (AudioBuffers +    │
                          │  ref-counting)     │
                          └─────────┬──────────┘
                                    │
             ┌──────────────────────┼──────────────────────┐
             │                      │                      │
   ┌─────────▼─────────┐   ┌────────▼────────┐    ┌────────▼────────┐
   │  BufferedSource   │   │ StreamingSource │    │ WaveformManager │
   │ (small files)     │   │ (large files)   │    │ (async, 8-bit)  │
   └─────────┬─────────┘   └────────┬────────┘    └────────┬────────┘
             │                      │                      │
        ┌────▼────┐            ┌────▼─────┐           ┌────▼─────┐
        │  Track  │            │ Preview  │           │  UI /    │
        │ Voice   │            │  Voice   │           │ Renderer │
        └────┬────┘            └────┬─────┘           └──────────┘
             │                      │
         ┌───▼──────────────┬───────▼───────────┐
         │    Mixer (RT)    │  TrackManager     │
         │  (zero alloc)    │  (multi-thread)   │
         └──────────────────┴───────────────────┘
```

---

## **11. Playback Sample Rate Handling (Actual vs Requested)**

* **WASAPI Shared** can run at a mix rate that differs from the requested/project rate. Always query the active backend for the *actual* stream sample rate and feed that into `TrackManager::setOutputSampleRate`.
* **SRC correctness** depends on using the real device/mix rate for:
  * resampling ratio inside `Track::copyAudioData` (track rate → output rate)
  * transport/playhead advance (positions use output sample rate)
* **SamplePool cache hits** must carry correct metadata (sampleRate, channels, numFrames). When a cached buffer is reused, reload these into the Track before computing duration or resampling ratios.
* **Preview vs. Track**: both use the same Track engine; the preview track must also be driven by the actual stream rate to avoid pitch/speed shifts when the device mixes at a different rate (e.g., 48k mix playing a 44.1k file).
