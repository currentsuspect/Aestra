# Bottlenecks

Known memory allocation characteristics before any optimization work.

## Current State

### No Custom Allocator Exists

AestraCore has zero custom allocator, arena, or pool implementations.
All memory management uses standard library (`new`, `std::make_shared`,
`std::make_unique`, `std::vector`).

### Existing Patterns That Act as Allocation Pools

| Pattern | Location | Type | Quality |
|---------|----------|------|---------|
| `LockFreeRingBuffer<T, Size>` | `AestraCore/include/AestraThreading.h` | Fixed-size static | Good |
| `AudioBufferManager` | `AestraAudio/src/DSP/AudioProcessor.cpp` | Pre-allocated arena | Basic |
| `SamplerPlugin::m_voices` | `AestraAudio/include/Plugin/SamplerPlugin.h` | Fixed array pool | Good |
| `LockFreeSPSCQueue` | `AestraAudio/include/Playback/PatternPlaybackEngine.h` | Fixed-size static | Good |
| `GarbageCollector` | `AestraAudio/include/GarbageCollector.h` | Deferred deletion | Uses mutex |
| `SamplePool` | `AestraAudio/include/IO/SamplePool.h` | LRU cache with budget | Good |

### Memory Profiling — Inert

`AESTRA_MEMORY_ALLOC(size)` / `AESTRA_MEMORY_FREE(size)` macros are defined
in `AestraUnifiedProfiler.h` but have **zero call sites**. The infrastructure
exists but is unused.

### Known Allocation Sites in Hot-Adjacent Code

| Location | File | Allocation | Frequency |
|----------|------|-----------|-----------|
| `AudioBufferManager::AudioBufferManager()` | `AudioProcessor.cpp:76` | `new float[totalSize]` | Construction only |
| `Filter::OversampledBuffer::resize()` | `Filter.cpp:809` | `make_unique<float[]>` | On size change |
| `SampleRateConverter::getSharedFilterBank()` | `SampleRateConverter.cpp:42` | `make_shared<PolyphaseFilterBank>` | Cache miss only |

### Compiler Context

- GCC 15.2.1 with `-O3`
- Register allocator is extremely sensitive to local variable changes
  (see resampler lab findings)
