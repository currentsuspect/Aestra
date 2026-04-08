# Accepted Patterns

Optimizations that measurably improved memory behavior and passed all gates.
Recorded here so future rounds can reuse or build on them.

## Enable Memory Profiling Globally

**Where**: `AestraCore/CMakeLists.txt`
**What**: Added `AESTRA_ENABLE_MEMORY_PROFILING` to `target_compile_definitions`.
**Why it works**: The profiler infrastructure was already complete — macros,
`MemoryStats` struct, `recordMemoryAllocation()`/`recordMemoryDeallocation()`.
The only missing piece was the compile flag. Zero runtime overhead when profiler
is disabled (macros are no-ops), and minimal overhead when enabled (counter
increments).
**Round**: 01 (session 001)

## Wire AESTRA_MEMORY_ALLOC/FREE at Key Allocation Sites

**Where**: `AudioProcessor.cpp` (AudioBufferManager), `Filter.cpp` (resize),
`SampleRateConverter.cpp` (filter bank)
**What**: Added `AESTRA_MEMORY_ALLOC(size)` after each `new`/`make_unique`,
and `AESTRA_MEMORY_FREE(size)` in destructors.
**Why it works**: These are the primary allocation sites in audio-thread-adjacent
code. Wiring them gives visibility into when and how much memory is allocated
during configure and processing. Foundation for measuring and eliminating
hot-path allocations.
**Round**: 01 (session 001)

## AudioArena Bump Allocator with Atomic CAS

**Where**: `AestraCore/include/AestraMemory.h`
**What**: `AudioArena` class — lock-free bump allocator using atomic
compare-and-swap loop. Allocates `capacity + 63` bytes and aligns the start
pointer to 64 bytes (covers all common SIMD alignments: 16, 32, 64).
Peak tracking via separate atomic CAS. Reset is non-thread-safe by design
(called from idle thread).
**Why it works**: Bump allocation is O(1) with no fragmentation. The atomic
CAS loop allows concurrent allocations from multiple threads without locks.
4 MB global singleton (`GlobalAudioArena`) covers typical audio buffer needs.
**Round**: 02 (session 002)

## AudioBufferManager Migrated to Arena

**Where**: `AestraAudio/src/DSP/AudioProcessor.cpp`
**What**: `AudioBufferManager` constructor now uses `GlobalAudioArena::allocate()`
instead of `new float[]`. Destructor no longer calls `delete[]` — arena handles
cleanup via `reset()`.
**Why it works**: Eliminates the single `new[]` call in AudioBufferManager
construction. All audio buffer allocations now come from the arena, which
is O(1) lock-free bump allocation.
**Round**: 02 (session 002)
